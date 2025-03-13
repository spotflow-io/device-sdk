use core::str;
use std::{
    path::Path,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
    time::Duration,
};

use crate::{
    connection::{
        twins::{DesiredProperties, DesiredPropertiesUpdatedCallback, TwinsClient},
        ConnectionImplementation,
    },
    ProcessSignalsSource,
};
use anyhow::{bail, Context, Result};
use tokio::{
    runtime::Runtime,
    sync::{mpsc, watch, Mutex},
};
use tokio_util::sync::CancellationToken;

use crate::cloud::drs::RegistrationResponse;
use crate::persistence::{
    self, sqlite::SdkConfiguration, sqlite_channel, CloseOption, CloudToDeviceMessage,
    ConfigurationStore, DeviceMessage, Producer, Store,
};

use crate::iothub::{
    token_handler::{RegistrationCommand, TokenHandler},
    twins::IotHubTwinsClient,
    IotHubConnection,
};

use super::{c2d::CloudToDeviceMessageGuard, Compression, MessageContext};

pub struct BaseConnection<T: ?Sized + Send + Sync> {
    configuration_store: ConfigurationStore,
    twins_client: IotHubTwinsClient,
    d2c_producer: Producer,
    c2d_consumer: Arc<Mutex<sqlite_channel::Receiver<CloudToDeviceMessage>>>,
    c2d_handler_registered: AtomicBool,
    signals_src: Option<Box<dyn ProcessSignalsSource>>,
    thread: Option<JoinHandle<()>>,
    runtime: Runtime,
    implementation: Option<Box<T>>,
    cancellation: CancellationToken,
}

impl BaseConnection<IotHubConnection> {
    // Startup
    // ================================================================================
    pub(super) fn init_ingress(
        config: SdkConfiguration,
        store_path: &Path,
        desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
        signals_src: Option<Box<dyn ProcessSignalsSource>>,
        initial_registration_response: Option<RegistrationResponse>,
        remote_access_allowed_for_all_ports: bool,
    ) -> Result<BaseConnection<dyn ConnectionImplementation + Send + Sync>> {
        // One thread is currently not enough, `runtime::Builder::new_current_thread` deadlocks when reconnect example is run.
        // We also force the number of threads to be at least 2 -- one worker thread plus one thread we spawn ourselves
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(1)
            .enable_all()
            .build()
            .context("Unable to build tokio runtime")?;

        let cancellation = CancellationToken::new();

        let store = rt.block_on(persistence::create(
            store_path,
            &config,
            cancellation.clone(),
        ))?;

        let (registration_watch, registration_command_sender) = rt.block_on(TokenHandler::init(
            config.instance_url,
            config.provisioning_token,
            config.registration_token,
            store.configuration_store.clone(),
            initial_registration_response,
        ))?;

        Ok(Self::start(
            rt,
            store,
            registration_watch,
            registration_command_sender,
            desired_properties_updated_callback,
            signals_src,
            remote_access_allowed_for_all_ports,
            cancellation,
        ))
    }

    #[allow(clippy::too_many_arguments)]
    fn start(
        rt: Runtime,
        store: Store,
        registration_watch: watch::Receiver<Option<RegistrationResponse>>,
        registration_command_sender: mpsc::UnboundedSender<RegistrationCommand>,
        desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
        signals_src: Option<Box<dyn ProcessSignalsSource>>,
        remote_access_allowed_for_all_ports: bool,
        cancellation: CancellationToken,
    ) -> BaseConnection<dyn ConnectionImplementation + Send + Sync> {
        let mut iothub = IotHubConnection::create(
            rt.handle().clone(),
            store.store,
            store.d2c_consumer,
            store.d2c_acknowledger,
            store.c2d_producer,
            store.twins_store,
            registration_watch,
            registration_command_sender,
            desired_properties_updated_callback,
            remote_access_allowed_for_all_ports,
            cancellation.clone(),
        );

        let connection_task = iothub.connect();

        let tokio_thread = thread::Builder::new()
            .name("Tokio MQTT thread".into())
            .spawn({
                let rt = rt.handle().clone();
                move || {
                    log::debug!("Tokio MQTT thread is starting.");

                    rt.block_on(async move {
                        let tasks = match connection_task.await {
                            Ok(tasks) => tasks,
                            Err(e) => {
                                log::error!("Failed setting up connection: {}", e);
                                return;
                            }
                        };
                        log::debug!("Connection is set up.");
                        for task in tasks {
                            if let Err(cause) = task.await {
                                log::error!("Task failed: {:?}", cause);
                            }
                        }
                    });
                    log::debug!("Tokio MQTT thread has finished.");
                }
            })
            .expect("Unable to spawn thread");

        BaseConnection {
            d2c_producer: store.d2c_producer,
            c2d_consumer: Arc::new(Mutex::new(store.c2d_consumer)),
            twins_client: iothub.twins_client().unwrap(),
            configuration_store: store.configuration_store,
            implementation: Some(Box::new(iothub)),
            c2d_handler_registered: AtomicBool::new(false),
            signals_src,
            thread: Some(tokio_thread),
            runtime: rt,
            cancellation,
        }
    }
}

impl BaseConnection<dyn ConnectionImplementation + Send + Sync> {
    // Public functionality
    // ================================================================================

    pub fn workspace_id(&self) -> Result<String> {
        self.runtime
            .block_on(self.configuration_store.load_workspace_id())
    }

    pub fn device_id(&self) -> Result<String> {
        self.runtime
            .block_on(self.configuration_store.load_device_id())
    }

    pub fn site_id(&self) -> Option<String> {
        self.configuration_store.site_id().map(str::to_owned)
    }

    pub fn pending_messages_count(&self) -> Result<usize> {
        self.runtime.block_on(self.d2c_producer.count())
    }

    // Potentially useful method, but the interface must be stabilized first
    #[allow(dead_code)]
    pub fn connection_error(&mut self) -> Option<Arc<dyn std::error::Error>> {
        self.implementation.as_mut().and_then(|i| i.error())
    }

    // Device to Cloud Messages
    // --------------------------------------------------------------------------------

    pub fn enqueue_message(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        message_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        let message = DeviceMessage {
            id: None,
            site_id: self.site_id(),
            stream_group: message_context.stream_group.clone(),
            stream: message_context.stream.clone(),
            batch_id,
            message_id,
            content: payload,
            close_option: CloseOption::None,
            compression: Compression::to_persisted_compression(&message_context.compression),
            batch_slice_id: None,
            chunk_id: None,
        };

        self.publish_message(message)
    }

    pub fn enqueue_message_advanced(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        batch_slice_id: Option<String>,
        message_id: Option<String>,
        chunk_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        let message = DeviceMessage {
            id: None,
            site_id: self.site_id(),
            stream_group: message_context.stream_group.clone(),
            stream: message_context.stream.clone(),
            batch_id,
            message_id,
            content: payload,
            close_option: CloseOption::None,
            compression: Compression::to_persisted_compression(&message_context.compression),
            batch_slice_id,
            chunk_id,
        };

        self.publish_message(message)
    }

    pub fn enqueue_batch_completion(
        &self,
        message_context: &MessageContext,
        batch_id: String,
    ) -> Result<()> {
        let message = DeviceMessage {
            id: None,
            site_id: self.site_id(),
            stream_group: message_context.stream_group.clone(),
            stream: message_context.stream.clone(),
            batch_id: Some(batch_id),
            message_id: None,
            content: Vec::new(),
            close_option: CloseOption::CloseOnly,
            compression: persistence::Compression::None,
            batch_slice_id: None,
            chunk_id: None,
        };

        self.publish_message(message)
    }

    pub fn enqueue_message_completion(
        &self,
        message_context: &MessageContext,
        batch_id: String,
        message_id: String,
    ) -> Result<()> {
        let message = DeviceMessage {
            id: None,
            site_id: self.site_id(),
            stream_group: message_context.stream_group.clone(),
            stream: message_context.stream.clone(),
            batch_id: Some(batch_id),
            message_id: Some(message_id),
            content: Vec::new(),
            close_option: CloseOption::CloseMessageOnly,
            compression: persistence::Compression::None,
            batch_slice_id: None,
            chunk_id: None,
        };

        self.publish_message(message)
    }

    pub fn wait_enqueued_messages_sent(&self) -> Result<()> {
        self.runtime.block_on(async {
            loop {
                let cnt = self.d2c_producer.count().await?;

                if cnt == 0 {
                    break;
                }

                if let Some(signals_src) = &self.signals_src {
                    signals_src.check_signals()?;
                }

                tokio::time::sleep(Duration::from_millis(200)).await;
            }
            Ok::<(), anyhow::Error>(())
        })?;
        Ok(())
    }

    pub fn send_message(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        message_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        self.enqueue_message(message_context, batch_id, message_id, payload)?;
        self.wait_enqueued_messages_sent()
    }

    pub fn send_message_advanced(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        batch_slice_id: Option<String>,
        message_id: Option<String>,
        chunk_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        self.enqueue_message_advanced(
            message_context,
            batch_id,
            batch_slice_id,
            message_id,
            chunk_id,
            payload,
        )?;
        self.wait_enqueued_messages_sent()
    }

    fn publish_message(&self, message: DeviceMessage) -> Result<()> {
        self.runtime.block_on(self.d2c_producer.add(message))
    }

    // Cloud to Device Messages
    // --------------------------------------------------------------------------------
    pub fn process_c2d<G>(&self, callback: G) -> Result<()>
    where
        G: Fn(&CloudToDeviceMessage) + Send + 'static,
    {
        if self
            .c2d_handler_registered
            .compare_exchange(false, true, Ordering::Relaxed, Ordering::Relaxed)
            .is_err()
        {
            bail!("Cloud to device message handler has already been registered.");
        }

        let consumer = self.c2d_consumer.clone();
        let runtime = self.runtime.handle().clone();
        let cancellation = self.cancellation.clone();
        // TODO clean this up
        // This eventually panics on shutdown
        thread::spawn(move || {
            runtime.block_on(async {
                // TODO Make this correct
                let mut consumer = consumer.lock().await;
                loop {
                    if cancellation.is_cancelled() {
                        break;
                    }
                    let msg = match consumer.recv(&None).await {
                        Ok(msg) => msg,
                        Err(e) => {
                            log::warn!("Processing of C2D messages failed: {:?}", e);
                            // If there is a transient issue a retry might help
                            // If there is a persistent issue let's not retry too aggressively
                            tokio::time::sleep(Duration::from_secs(30)).await;
                            continue;
                        }
                    };
                    callback(&msg);
                    if let Err(e) = consumer.ack(&msg).await {
                        // TODO add some retrying here, possibly prevent further processing
                        // We cannot remove the message from the store -- this will result in the message being retrieved again in the next iteration and subsequent restarts
                        log::warn!("Unable to remove C2D message to prevent duplicate processing, it will be processed again: {:?}", e);
                        tokio::time::sleep(Duration::from_secs(30)).await;
                    }
                }
            });
        });
        Ok(())
    }

    // Checks if a cloud-to-device message can be obtained without blocking
    pub fn pending_c2d(&self) -> Result<usize> {
        self.runtime.block_on(self.c2d_consumer.try_lock()?.count())
    }

    // Gets a cloud-to-device message and returns a guard that acknowledges the message when it is dropped
    // If process_c2d has been called dropping this guard will block indefinetly.
    pub fn get_c2d(&self, timeout: Duration) -> Result<CloudToDeviceMessageGuard<'_>> {
        let msg = self.runtime.block_on(async {
            let cancellation = CancellationToken::new();

            tokio::spawn({
                let token = cancellation.clone();
                async move {
                    tokio::time::sleep(timeout).await;
                    token.cancel();
                }
            });

            self.c2d_consumer
                .try_lock()?
                .recv(&Some(cancellation))
                .await
        })?;

        Ok(CloudToDeviceMessageGuard::new(
            msg,
            self.runtime.handle(),
            self.c2d_consumer.clone(),
        ))
    }

    // Twins
    // --------------------------------------------------------------------------------

    // Returns the latest twins
    pub fn desired_properties(&self) -> Result<DesiredProperties> {
        self.runtime
            .block_on(self.twins_client.get_desired_properties())
    }

    pub fn desired_properties_if_newer(&self, version: u64) -> Option<DesiredProperties> {
        self.runtime
            .block_on(self.twins_client.get_desired_properties_if_newer(version))
    }

    pub fn reported_properties(&self) -> Option<String> {
        self.runtime
            .block_on(self.twins_client.get_reported_properties())
    }

    pub fn wait_desired_properties_changed(&self) -> Result<DesiredProperties> {
        self.runtime
            .block_on(self.twins_client.desired_properties_changed())
    }

    pub fn update_reported_properties(&self, properties: &str) -> Result<()> {
        self.runtime
            .block_on(self.twins_client.set_reported_properties(properties))
    }

    pub fn patch_reported_properties(&self, patch: &str) -> Result<()> {
        self.runtime
            .block_on(self.twins_client.patch_reported_properties(patch))
    }

    pub fn any_pending_reported_properties_updates(&self) -> Result<bool> {
        self.runtime
            .block_on(self.twins_client.pending_reported_properties_updates())
    }

    pub fn wait_properties_ready(&self) -> Result<()> {
        self.runtime
            .block_on(self.twins_client.wait_properties_ready())
    }
}

impl<T: ?Sized + Send + Sync> Drop for BaseConnection<T> {
    fn drop(&mut self) {
        log::debug!("Base connection is being dropped");
        drop(self.implementation.take());

        // Join the thread where all async tasks were run and other processor threads (such as c2d)
        // Only the MQTT loop and Sender are blocking the join of the main thread, other tasks will be dropped (possibly while they're awaiting) when these two finish and the thread is joined.
        log::debug!("Waiting for the execution thread to be joined");
        crate::utils::thread::join(&mut self.thread);

        log::debug!("Base connection is dropped");
    }
}
