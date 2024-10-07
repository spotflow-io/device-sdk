use std::{collections::HashMap, sync::Arc};

use crate::connection::twins::TwinsClient;
use anyhow::{bail, Context, Result};
use async_trait::async_trait;
use rumqttc::{AsyncClient, Publish};
use thiserror::Error;
use tokio::{
    select,
    sync::{mpsc, watch, Mutex},
};
use tokio_util::sync::CancellationToken;

use super::super::query;
use super::super::{json_diff, State};
use super::super::{topics, twins::IotHubTwinsClient};
use super::AsyncHandler;
use crate::persistence::sqlite_channel;
use crate::persistence::twins::{ReportedPropertiesUpdate, ReportedPropertiesUpdateType, Twins};

pub(crate) struct TwinsHandler {
    response_channel: mpsc::Sender<Publish>,
    desired_properties_updates: mpsc::Sender<Publish>,
}

impl TwinsHandler {
    pub(crate) fn new(
        desired_properties_updates: mpsc::Sender<Publish>,
        response_channel: mpsc::Sender<Publish>,
    ) -> Self {
        TwinsHandler {
            response_channel,
            desired_properties_updates,
        }
    }
}

#[async_trait]
impl AsyncHandler for TwinsHandler {
    fn prefix(&self) -> Vec<&str> {
        vec![
            topics::TWIN_RESPONSE_PREFIX,
            topics::UPDATE_DESIRED_PROPERTIES_PREFIX,
        ]
    }

    async fn handle(&mut self, publish: &Publish) {
        match &publish.topic {
            topic if topic.starts_with(topics::UPDATE_DESIRED_PROPERTIES_PREFIX) => {
                // If the receiver was dropped no one is interested in the message and this will most likely be shut down soon
                _ = self.desired_properties_updates.send(publish.clone()).await;
            }
            topic if topic.starts_with(topics::TWIN_RESPONSE_PREFIX) => {
                // If the receiver was dropped no one is interested in the message and this will most likely be shut down soon
                _ = self.response_channel.send(publish.clone()).await;
            }
            topic => {
                log::error!("Unhandled topic {}", topic);
            }
        }
    }
}

#[derive(Debug)]
enum ResponseType {
    PatchReportedProperties(ReportedPropertiesUpdate),
    GetTwins,
}

#[derive(Debug, Error)]
pub enum PropertiesUpdateError {
    #[error("Unexpected patch version: current version is {current_version}, patch version is {patch_version}")]
    PatchVersionMismatch {
        current_version: u64,
        patch_version: u64,
    },
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

#[derive(Debug)]
pub(crate) struct TwinsMiddleware {
    mqtt_client: AsyncClient,
    requests: Arc<Mutex<HashMap<String, ResponseType>>>,
    twins: IotHubTwinsClient,
    cancellation: CancellationToken,
    was_disconnected: bool,

    reported_properties_updates: sqlite_channel::Receiver<ReportedPropertiesUpdate>,
    get_twins: mpsc::Receiver<()>,
    desired_properties_updates: mpsc::Receiver<Publish>,
    desired_properties_changed: watch::Sender<u64>,
    response_channel: mpsc::Receiver<Publish>,
    connection_state_rx: watch::Receiver<State>,
}

impl TwinsMiddleware {
    // I didn't come up with a way to split this nicely. Having a builder doesn't feel helpful.
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn new(
        client: AsyncClient,
        twins: IotHubTwinsClient,
        get_twins: mpsc::Receiver<()>,
        reported_properties_updates: sqlite_channel::Receiver<ReportedPropertiesUpdate>,
        desired_properties_updates: mpsc::Receiver<Publish>,
        desired_properties_changed: watch::Sender<u64>,
        response_channel: mpsc::Receiver<Publish>,
        connection_state_rx: watch::Receiver<State>,
        cancellation: CancellationToken,
    ) -> Self {
        TwinsMiddleware {
            mqtt_client: client,
            requests: Arc::new(Mutex::new(HashMap::new())),
            twins,
            cancellation,
            was_disconnected: false,
            get_twins,
            reported_properties_updates,
            desired_properties_updates,
            desired_properties_changed,
            response_channel,
            connection_state_rx,
        }
    }

    pub(crate) async fn process(&mut self) {
        loop {
            if let Err(e) = select!(
                () = self.cancellation.cancelled() => break,
                Some(()) = self.get_twins.recv() => {
                    self.get_twins().await.context("Receiving complete twins failed")
                }
                Ok(msg) = self.reported_properties_updates.recv(&None) => {
                    self.update_reported_properties(msg).await.context("Updating reported properties failed")
                }
                Some(update) = self.desired_properties_updates.recv() => {
                    self.update_desired_properties_handler(&update).await.context("Updating desired properties failed")
                }
                Some(response) = self.response_channel.recv() => {
                    self.handle_response(&response).await.context("Handling twin response to our request failed")
                }
                Ok(state) = Self::receive_state_update(&mut self.connection_state_rx) => {
                    self.handle_connection_state_change(&state).await.context("Handling connection state change failed")
                }
            ) {
                log::error!("Failed processing twin message: {:?}", e);
            }
        }
    }

    async fn set_twins(&self, payload: &[u8]) -> Result<()> {
        let payload = std::str::from_utf8(payload).context("Error parsing twins as UTF8.")?;
        let twins: Twins =
            serde_json::from_str(payload).context("Unable to deserialize twins from JSON.")?;
        let version = twins.desired.version;
        self.twins.set_twins(twins).await?;
        self.desired_properties_changed
            .send(version)
            .context("Client is already dropped and not waiting for desired properties updates.")?;

        Ok(())
    }

    async fn update_reported_properties(&self, update: ReportedPropertiesUpdate) -> Result<()> {
        let patch = match update.update_type {
            ReportedPropertiesUpdateType::Patch => update.patch.to_string(),
            ReportedPropertiesUpdateType::Full => {
                let current = self
                    .twins
                    .get_reported_properties()
                    .await
                    .unwrap_or_else(|| String::from("{}"));
                json_diff::diff(&current, &update.patch.to_string())?
            }
        };
        let rid = uuid::Uuid::new_v4().to_string();
        self.requests
            .lock()
            .await
            .insert(rid.clone(), ResponseType::PatchReportedProperties(update));

        log::debug!("Updating reported properties with request ID {rid}");
        self.mqtt_client
            .try_publish(
                topics::patch_reported_properties(&rid),
                rumqttc::QoS::AtLeastOnce,
                false,
                patch.as_bytes(),
            )
            .context("Unable to enqueue publish to update reported properties")?;

        if let Err(e) = self.twins.update_reported_properties(&patch).await {
            log::warn!("There was an error during updating local copy of reported properties. Requesting full copy. Original error: {:?}", e);
            self.get_twins().await.context("Error during requesting full twin update because of failed local reported properties update")?;
        }

        Ok(())
    }

    async fn get_twins(&self) -> Result<()> {
        let rid = uuid::Uuid::new_v4().to_string();
        self.requests
            .lock()
            .await
            .insert(rid.clone(), ResponseType::GetTwins);
        log::debug!("Requesting device twins with request ID {rid}");
        self.mqtt_client
            .try_publish(
                topics::get_twins(&rid),
                rumqttc::QoS::AtLeastOnce,
                false,
                Vec::new(),
            )
            .context("Unable to enqueue publish to request device twins")?;

        Ok(())
    }

    pub(crate) async fn update_desired_properties(
        &self,
        version: u64,
        payload: &str,
    ) -> Result<(), PropertiesUpdateError> {
        self.twins
            .update_desired_properties(version, payload)
            .await?;
        self.desired_properties_changed
            .send(version)
            .context("Client is already dropped and not waiting for desired properties updates.")?;
        Ok(())
    }

    async fn update_desired_properties_handler(&self, publish: &Publish) -> Result<()> {
        // The topic should be formatted like this:
        // $iothub/twin/PATCH/properties/desired/?$version={new version}

        let topic = &publish.topic;

        log::debug!("Received device twin desired properties update on topic {topic}");
        let Ok(parts): Result<[_; 6], _> = topic.split('/').collect::<Vec<_>>().try_into() else {
            bail!("Received message on invalid topic '{topic}'.");
        };

        let Some(properties) = parts[5].strip_prefix('?') else {
            bail!("Received message with malformed properties '{}'.", parts[5]);
        };

        let properties = query::parse(properties).context(format!(
            "Failed parsing twin desired properties update topic `{topic}`"
        ))?;
        let version = match properties.get("$version") {
            Some(Some(version)) => match version.parse() {
                Ok(v) => v,
                Err(e) => {
                    bail!(
                        "Twin update was malformed: Unable to parse version number: {:?}",
                        e
                    );
                }
            },
            _ => {
                bail!(
                    "Failed parsing twin desired properties update topic `{}`: Missing version property.",
                    topic,
                );
            }
        };

        let payload = std::str::from_utf8(publish.payload.as_ref())?;

        if let Err(PropertiesUpdateError::PatchVersionMismatch { .. }) =
            self.update_desired_properties(version, payload).await
        {
            log::info!("Received invalid desired properties update. Requesting full twin update.");
            self.get_twins().await?;
        }

        Ok(())
    }

    async fn handle_response(&self, publish: &Publish) -> Result<()> {
        // The topic should be formatted like this:
        // $iothub/twin/res/{status}/?$rid={request id}
        let topic = &publish.topic;

        log::debug!("Received device twin desired properties or reported properties change result on topic {topic}");
        let Ok(parts): Result<[_; 5], _> = topic.split('/').collect::<Vec<_>>().try_into() else {
            bail!("Received message on an invalid topic '{topic}'.");
        };

        let status = parts[3];
        if status.parse::<usize>().is_err() {
            bail!("Received message on an invalid topic '{topic}'.");
        }

        let Some(properties) = parts[4].strip_prefix('?') else {
            bail!("Received message with malformed properties '{}'.", parts[4]);
        };

        // Skip leading question mark
        let properties = query::parse(properties).context(format!(
            "Failed parsing twin response message topic `{topic}`"
        ))?;

        let request_id = properties.get("$rid").cloned().flatten().context(format!(
            "Request ID is missing in twin response on topic `{topic}`"
        ))?;

        match self.requests.lock().await.remove(&request_id) {
            None => {
                log::warn!("Ignoring response to request `{}`", request_id);
            }
            Some(ResponseType::GetTwins) => self
                .set_twins(publish.payload.as_ref())
                .await
                .context("Failed setting twins")?,
            Some(ResponseType::PatchReportedProperties(update)) => self
                .reported_properties_updates
                .ack(&update)
                .await
                .context("Failed removing reported properties update request")?,
        }

        Ok(())
    }

    async fn receive_state_update(
        connection_state_rx: &mut watch::Receiver<State>,
    ) -> Result<State> {
        connection_state_rx.changed().await?;
        let state = connection_state_rx.borrow_and_update().clone();

        Ok(state)
    }

    async fn handle_connection_state_change(&mut self, state: &State) -> Result<()> {
        match state {
            State::Ready => {
                if self.was_disconnected {
                    log::info!("Reconnected. Requesting full twin update.");
                    self.get_twins().await?;
                    self.was_disconnected = false;
                }
            }
            State::ConnectionError(_) => {
                self.was_disconnected = true;
            }
        }

        Ok(())
    }
}
