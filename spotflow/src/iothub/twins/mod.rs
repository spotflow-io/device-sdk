use std::{collections::VecDeque, sync::Arc};

use crate::connection::twins::{DesiredProperties, DesiredPropertiesUpdatedCallback, TwinsClient};
use crate::persistence::twins::{
    ReportedPropertiesUpdate, ReportedPropertiesUpdateType, Twin, TwinUpdate, Twins,
};
use anyhow::{anyhow, bail, Context, Result};
use async_trait::async_trait;
use json_patch::merge;
use log::trace;
use tokio::sync::{mpsc, watch, Mutex};

use crate::persistence::{sqlite_channel, TwinsStore};

use super::handlers::twins::PropertiesUpdateError;

use self::update_callback_dispatcher::DesiredPropertiesUpdatedCallbackDispatcher;

mod update_callback_dispatcher;

#[derive(Clone)]
pub(crate) struct InitializationWaiter {
    desired_rx: watch::Receiver<bool>,
    reported_rx: watch::Receiver<bool>,
}

impl InitializationWaiter {
    pub(crate) async fn wait(&mut self) -> Result<()> {
        trace!("Waiting for Device Twin to be initialized");

        Self::wait_rx(&mut self.desired_rx).await?;
        Self::wait_rx(&mut self.reported_rx).await?;

        trace!("Device Twin is initialized");

        Ok(())
    }

    async fn wait_rx(rx: &mut watch::Receiver<bool>) -> Result<()> {
        while !*rx.borrow_and_update() {
            rx.changed().await?;
        }

        Ok(())
    }
}

#[derive(Debug)]
pub(crate) struct DeviceTwin {
    store: TwinsStore,
    desired: Option<Twin>,
    reported: Option<Twin>,
    desired_properties_updates: VecDeque<TwinUpdate>,
    desired_initialized_tx: watch::Sender<bool>,
    reported_initialized_tx: watch::Sender<bool>,
    desired_properties_update_callback_dispatcher:
        Option<DesiredPropertiesUpdatedCallbackDispatcher>,
}

impl DeviceTwin {
    pub(super) async fn init(
        store: TwinsStore,
        desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
    ) -> DeviceTwin {
        let desired = store.load_desired_properties().await.unwrap_or_default();
        let reported = store.load_reported_properties().await.unwrap_or_default();

        let (desired_initialized_tx, _) = watch::channel(desired.is_some());
        let (reported_initialized_tx, _) = watch::channel(reported.is_some());

        let desired_properties_update_callback_dispatcher = desired_properties_updated_callback
            .map(DesiredPropertiesUpdatedCallbackDispatcher::new);

        DeviceTwin {
            store,
            desired,
            reported,
            desired_properties_updates: VecDeque::new(),
            desired_initialized_tx,
            reported_initialized_tx,
            desired_properties_update_callback_dispatcher,
        }
    }

    async fn set_reported_properties(
        &mut self,
        version: u64,
        properties: serde_json::Value,
    ) -> Result<()> {
        if let Some(ref twin) = self.reported {
            if version < twin.version {
                log::debug!(
                    "Ignoring reported properties update to version {} because we are at {}",
                    version,
                    twin.version
                );
                return Ok(());
            }
        }

        log::debug!("Setting reported properties to version {version}");
        self.reported = Some(Twin {
            version,
            properties,
        });
        let reported = self
            .reported
            .as_ref()
            .expect("Reported properties value has just been assigned but is missing");

        self.store.save_reported_properties(reported).await?;

        self.notify_reported_properties_updated();

        Ok(())
    }

    async fn set_desired_properties(
        &mut self,
        version: u64,
        properties: serde_json::Value,
    ) -> Result<()> {
        if let Some(ref twin) = self.desired {
            if version < twin.version {
                log::debug!(
                    "Ignoring desired properties update to version {} because we are at {}",
                    version,
                    twin.version
                );
                return Ok(());
            }
        }

        log::debug!("Setting desired properties to version {version}");
        self.desired = Some(Twin {
            version,
            properties,
        });
        let desired = self
            .desired
            .as_mut()
            .expect("Desired properties value has just been assigned but is missing");

        while let Some(update) = self.desired_properties_updates.pop_front() {
            desired.update(&update)?;
        }

        log::trace!("Current desired properties:\n{:#?}", desired.properties);

        self.store.save_desired_properties(desired).await?;

        self.notify_desired_properties_updated()?;

        Ok(())
    }

    pub(super) async fn update_desired_properties(
        &mut self,
        version: u64,
        update: &str,
    ) -> Result<(), PropertiesUpdateError> {
        let update: TwinUpdate = serde_json::from_str(update).map_err(|e| anyhow!(e))?;
        if update.version != Some(version) {
            return Err(PropertiesUpdateError::Other(anyhow!(
                "Mismatched version in path ({}) and in body ({}).",
                version,
                update
                    .version
                    .map_or(String::from("Missing"), |x| x.to_string()),
            )));
        }
        log::trace!("Received desired properties update: {:#?}", update);
        match &mut self.desired {
            None => {
                self.desired_properties_updates.push_back(update);
            }
            Some(twin) => {
                if version == twin.version + 1 {
                    log::debug!("Applying desired properties patch to version {version}.");
                    merge(&mut twin.properties, &update.patch);
                    twin.version = version;
                    log::trace!("Current desired properties:\n{:#?}", twin.properties);

                    self.store.save_desired_properties(twin).await?;

                    self.notify_desired_properties_updated()?;
                } else {
                    log::info!("Unable to apply Desired Properties patch of version {} because we are at {}.", version, twin.version);
                    return Err(PropertiesUpdateError::PatchVersionMismatch {
                        current_version: twin.version,
                        patch_version: version,
                    });
                }
            }
        }

        Ok(())
    }

    pub(super) async fn update_reported_properties(&mut self, update: &str) -> Result<()> {
        let update: TwinUpdate = serde_json::from_str(update).context(
            "Unable to deserialize JSON representation of reported properties to update",
        )?;
        log::trace!("Received reported properties update: {:#?}", update);
        match &mut self.reported {
            None => {
                bail!("Reported properties cannot be updated yet because they are not loaded.");
            }
            Some(twin) => {
                let version = twin.version + 1;
                log::debug!("Applying reported properties patch to version {version}");
                merge(&mut twin.properties, &update.patch);
                twin.version = version;
                log::trace!("Current reported properties:\n{:#?}", twin.properties);

                self.store.save_reported_properties(twin).await?;

                self.notify_reported_properties_updated();
            }
        }

        Ok(())
    }

    fn notify_desired_properties_updated(&self) -> Result<()> {
        self.desired_initialized_tx.send_replace(true);

        if let Some(dispatcher) = &self.desired_properties_update_callback_dispatcher {
            let desired = self
                .desired
                .as_ref()
                .expect("Desired Properties should have been initialized");

            dispatcher.dispatch(DesiredProperties {
                version: desired.version,
                values: desired.properties.to_string(),
            })?;
        }

        Ok(())
    }

    fn notify_reported_properties_updated(&self) {
        self.reported_initialized_tx.send_replace(true);
    }

    pub(super) fn desired_properties(&self) -> &Option<Twin> {
        &self.desired
    }

    pub(super) fn reported_properties(&self) -> &Option<Twin> {
        &self.reported
    }

    pub(crate) async fn set_twins(&mut self, twins: Twins) -> Result<()> {
        log::trace!("Received twins:\n{:#?}", twins);

        self.set_desired_properties(twins.desired.version, twins.desired.properties)
            .await?;
        self.set_reported_properties(twins.reported.version, twins.reported.properties)
            .await?;

        Ok(())
    }

    pub(crate) fn create_initialization_waiter(&self) -> InitializationWaiter {
        InitializationWaiter {
            desired_rx: self.desired_initialized_tx.subscribe(),
            reported_rx: self.reported_initialized_tx.subscribe(),
        }
    }
}

#[derive(Debug)]
pub struct IotHubTwinsClient {
    twins: Arc<Mutex<DeviceTwin>>,
    get_twins: mpsc::Sender<()>,
    reported_properties_updates: sqlite_channel::Sender<ReportedPropertiesUpdate>,
    desired_properties_changed: Mutex<watch::Receiver<u64>>,
}

#[async_trait]
impl TwinsClient for IotHubTwinsClient {
    async fn get_twins(&self) {
        // This is best effort.
        _ = self.get_twins.send(()).await;
    }

    async fn set_reported_properties(&self, patch: &str) -> Result<()> {
        let patch = ReportedPropertiesUpdate {
            id: None,
            update_type: ReportedPropertiesUpdateType::Full,
            patch: serde_json::from_str(patch)?,
        };

        self.reported_properties_updates.send(&patch).await?;

        Ok(())
    }

    async fn patch_reported_properties(&self, patch: &str) -> Result<()> {
        let patch = ReportedPropertiesUpdate {
            id: None,
            update_type: ReportedPropertiesUpdateType::Patch,
            patch: serde_json::from_str(patch)?,
        };

        self.reported_properties_updates.send(&patch).await?;

        Ok(())
    }

    async fn get_desired_properties(&self) -> Result<DesiredProperties> {
        self.desired_properties_changed
            .lock()
            .await
            .borrow_and_update();
        self.twins
            .lock()
            .await
            .desired_properties()
            .as_ref()
            .map(|t| DesiredProperties {
                version: t.version,
                values: t.properties.to_string(),
            })
            .ok_or_else(|| {
                anyhow!(
                    "Desired Properties haven't been initialized yet, although they should have."
                )
            })
    }

    async fn get_desired_properties_if_newer(&self, version: u64) -> Option<DesiredProperties> {
        self.desired_properties_changed
            .lock()
            .await
            .borrow_and_update();
        self.twins
            .lock()
            .await
            .desired_properties()
            .as_ref()
            .and_then(|t| {
                if t.version > version {
                    Some(DesiredProperties {
                        version: t.version,
                        values: t.properties.to_string(),
                    })
                } else {
                    None
                }
            })
    }

    async fn get_reported_properties(&self) -> Option<String> {
        self.twins
            .lock()
            .await
            .reported_properties()
            .as_ref()
            .map(|t| t.properties.to_string())
    }

    async fn desired_properties_changed(&self) -> Result<DesiredProperties> {
        log::trace!("Awaiting desired properties change");
        self.desired_properties_changed
            .lock()
            .await
            .changed()
            .await?;
        log::trace!("Received new desired properties");

        let twin_guard = self.twins.lock().await;

        let desired = twin_guard
            .desired_properties()
            .as_ref()
            .expect("After receiving change notification desired properties must not be None");

        Ok(DesiredProperties {
            version: desired.version,
            values: desired.properties.to_string(),
        })
    }

    async fn pending_reported_properties_updates(&self) -> Result<bool> {
        let count = self.reported_properties_updates.count().await?;
        Ok(count > 0)
    }

    async fn wait_properties_ready(&self) -> Result<()> {
        let guard = self.twins.lock().await;
        let mut waiter = guard.create_initialization_waiter();
        drop(guard);

        // The waiter must be awaited separately to prevent a deadlock when the Device Twin lock is held
        waiter.wait().await
    }
}

impl IotHubTwinsClient {
    pub async fn init(
        store: TwinsStore,
        get_twins: mpsc::Sender<()>,
        reported_properties_updates: sqlite_channel::Sender<ReportedPropertiesUpdate>,
        desired_properties_changed: watch::Receiver<u64>,
        desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
    ) -> Self {
        let device_twins = DeviceTwin::init(store, desired_properties_updated_callback).await;
        IotHubTwinsClient {
            twins: Arc::new(Mutex::new(device_twins)),
            get_twins,
            reported_properties_updates,
            desired_properties_changed: Mutex::new(desired_properties_changed),
        }
    }

    pub(crate) async fn set_twins(&self, twins: Twins) -> Result<()> {
        self.twins.lock().await.set_twins(twins).await
    }

    pub(crate) async fn update_desired_properties(
        &self,
        version: u64,
        payload: &str,
    ) -> Result<(), PropertiesUpdateError> {
        self.twins
            .lock()
            .await
            .update_desired_properties(version, payload)
            .await
    }

    pub(crate) async fn update_reported_properties(&self, patch: &str) -> Result<()> {
        self.twins
            .lock()
            .await
            .update_reported_properties(patch)
            .await
    }
}

impl Clone for IotHubTwinsClient {
    fn clone(&self) -> Self {
        let desired_properties_changed = loop {
            if let Ok(desired_properties_changed) = self.desired_properties_changed.try_lock() {
                break desired_properties_changed.clone();
            }
        };

        Self {
            twins: self.twins.clone(),
            get_twins: self.get_twins.clone(),
            reported_properties_updates: self.reported_properties_updates.clone(),
            desired_properties_changed: Mutex::new(desired_properties_changed),
        }
    }
}
#[cfg(test)]
mod tests {
    use crate::persistence::twins::{TwinUpdate, Twins};

    #[test]
    fn deserialize_twins() {
        let twins = r#"{"desired":{"foo":"bar","ahoj":"bye","next":"next","$version":10},"reported":{"$version":1}}"#;
        let twins: Twins = serde_json::from_str(twins).expect("Unable to deserialize twins");
        assert_eq!(twins.desired.version, 10);
        assert_eq!(twins.reported.version, 1);

        assert!(twins.desired.properties.is_object());
        assert_eq!(twins.desired.properties.as_object().unwrap().len(), 3);
        assert_eq!(
            twins
                .desired
                .properties
                .as_object()
                .unwrap()
                .get("foo")
                .unwrap()
                .as_str()
                .unwrap(),
            "bar"
        );

        assert!(twins.reported.properties.is_object());
        assert_eq!(twins.reported.properties.as_object().unwrap().len(), 0);
    }

    #[test]
    fn update_twin() {
        let twins = r#"{"desired":{"foo":"bar","lorem":"ipsum","ahoj":"bye","next":"next","$version":10},"reported":{"$version":1}}"#;
        let mut twins: Twins = serde_json::from_str(twins).expect("Unable to deserialize twins");
        assert_eq!(twins.desired.version, 10);

        let update = r#"{"ahoj":"hi","next":42,"foo":null,"$version":11}"#;
        let update: TwinUpdate =
            serde_json::from_str(update).expect("Unable to deserialize update");

        twins.desired.update(&update).unwrap();
        let result = r#"{"lorem":"ipsum","ahoj":"hi","next":42}"#;
        let result: serde_json::Value =
            serde_json::from_str(result).expect("Unable to deserialize expected JSON result");

        assert_eq!(twins.desired.properties, result);
        assert_eq!(twins.desired.version, 11);
    }
}
