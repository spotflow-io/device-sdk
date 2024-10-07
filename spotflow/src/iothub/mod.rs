use std::{future::Future, panic::RefUnwindSafe, pin::Pin, sync::Arc, time::Duration};

use crate::cloud::drs::{ConnectionStringType, RegistrationResponse};
use crate::connection::{
    twins::{DesiredPropertiesUpdatedCallback, TwinsClient},
    ConnectionImplementation, JoinHandleVec,
};
use anyhow::{anyhow, bail, Context, Result};
use rumqttc::{AsyncClient, ConnectionError, MqttOptions, TlsConfiguration, Transport};
use token_handler::{RegistrationCommand, RegistrationCommandSender};
use tokio::{
    runtime::Handle,
    sync::{
        mpsc, oneshot,
        watch::{self, Receiver},
    },
};
use tokio_util::sync::CancellationToken;

use eventloop::EventLoop;
use handlers::{
    c2d::CloudToDeviceHandler,
    direct_method::DirectMethodHandler,
    twins::{TwinsHandler, TwinsMiddleware},
};
use sender::Sender;
use topics::publish_topic;

use crate::persistence::{
    sqlite::SqliteStore, sqlite_channel, twins::ReportedPropertiesUpdate, Acknowledger,
    CloudToDeviceMessage, Consumer, TwinsStore,
};
// use spotflow_connection::twins::TwinsClient;
use twins::IotHubTwinsClient;

mod eventloop;
mod handlers;
mod json_diff;
mod query;
mod sender;
pub mod token_handler;
mod topics;
pub mod twins;

#[derive(Debug, Clone)]
pub enum State {
    Ready,
    // Make own custom error which implements clone and get rid of Arc
    // Create watch to notify users of errors as they happen
    ConnectionError(Arc<ConnectionError>),
}

#[derive(Debug)]
pub struct OnlineConnection {
    client: AsyncClient,
    state: watch::Receiver<State>,
}

pub struct IotHubConnection<F> {
    runtime: Handle,
    store: SqliteStore,
    d2c_consumer: Option<Consumer>,
    d2c_acknowledger: Option<Acknowledger>,
    c2d_producer: Option<sqlite_channel::Sender<CloudToDeviceMessage>>,
    twins_store: TwinsStore,
    registration_watch: Receiver<Option<RegistrationResponse>>,
    registration_command_sender: RegistrationCommandSender,
    cancellation: CancellationToken,
    method_handler: Option<F>,
    desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,

    connection_receiver: Option<oneshot::Receiver<OnlineConnection>>,
    twins_client: Option<IotHubTwinsClient>,
}

impl<F> IotHubConnection<F> {
    #[allow(clippy::too_many_arguments)]
    pub fn create(
        runtime: Handle,
        store: SqliteStore,
        d2c_consumer: Consumer,
        d2c_acknowledger: Acknowledger,
        c2d_producer: sqlite_channel::Sender<CloudToDeviceMessage>,
        twins_store: TwinsStore,
        registration_watch: Receiver<Option<RegistrationResponse>>,
        registration_command_sender: mpsc::UnboundedSender<RegistrationCommand>,
        method_handler: Option<F>,
        desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
        cancellation: CancellationToken,
    ) -> Self
    where
        F: Fn(String, &[u8]) -> (i32, Vec<u8>) + Send + RefUnwindSafe + 'static,
    {
        IotHubConnection {
            runtime,
            store,
            d2c_consumer: Some(d2c_consumer),
            d2c_acknowledger: Some(d2c_acknowledger),
            c2d_producer: Some(c2d_producer),
            twins_store,
            registration_watch,
            registration_command_sender,
            cancellation,
            method_handler,
            desired_properties_updated_callback,

            connection_receiver: None,
            twins_client: None,
        }
    }

    async fn connect_iothub(
        registration_watch: &mut watch::Receiver<Option<RegistrationResponse>>,
    ) -> Result<(AsyncClient, rumqttc::EventLoop)> {
        while registration_watch.borrow_and_update().is_none() {
            log::trace!("Awaiting first registration");
            registration_watch.changed().await.expect(
                "Registrator worker stopped running before receiving first successful registration",
            );
        }

        log::debug!("First registration is done");
        let registration = {
            let registration = registration_watch.borrow();
            registration
                .as_ref()
                .expect("Registration worker must not send None")
                .clone()
        };

        let device_id = registration
            .iot_hub_device_id()
            .context("Unable to parse device ID from SAS token from DRS")?;
        let iothub = &registration.iot_hub_host_name;
        if registration.connection_string_type != ConnectionStringType::SharedAccessSignature {
            bail!(
                "Registration connection string type must be `SharedAccessSignature` but it's `{:?}`.",
                registration.connection_string_type
            );
        }

        let username = format!("{iothub}/{device_id}/?api-version=2018-06-30");
        let password = registration
            .sas()
            .context("Unable to parse SAS token from DRS response")?;
        // let password = format!("{}", registration.connection_string);

        let mut options = MqttOptions::new(device_id, iothub, 8883);
        options.set_keep_alive(Duration::from_secs(5 * 60));
        options.set_credentials(username, password);
        options.set_transport(Transport::Tls(TlsConfiguration::Native));
        options.set_clean_session(false);
        options.set_manual_acks(true);
        // We cannot guarantee data won't be sent twice because IoT Hub supports only MQTT QoS 1.
        // Ingress cannot currently deduplicate messages that aren't next to each other
        options.set_inflight(1);

        Ok(AsyncClient::new(options, 10))
    }

    // pub fn twins_client(&self) -> Result<Box<dyn spotflow_connection::twins::TwinsClient>> {
    //     Ok(Box::new(self.twins_client.as_ref().unwrap().clone()))
    // }
    pub fn twins_client(&self) -> Result<IotHubTwinsClient> {
        Ok(self
            .twins_client
            .as_ref()
            .ok_or(anyhow!("Connection was not established yet."))?
            .clone())
    }
}

impl<F: Fn(String, &[u8]) -> (i32, Vec<u8>) + Send + Sync + RefUnwindSafe + 'static>
    ConnectionImplementation for IotHubConnection<F>
{
    fn connect(&mut self) -> Pin<Box<dyn Future<Output = Result<JoinHandleVec>> + Send>> {
        let (response_sender, response_receiver) = mpsc::channel(100);
        let (desired_properties_sender, desired_properties_receiver) = mpsc::channel(100);
        let (reported_properties_sender, reported_properties_receiver) =
            sqlite_channel::channel::<ReportedPropertiesUpdate>(self.store.clone());
        let (get_twins_sender, get_twins_receiver) = mpsc::channel(100);
        let (desired_properties_changed_sender, desired_properties_changed_receiver) =
            watch::channel(0);
        let (conn_sender, conn_receiver) = oneshot::channel();

        let twins_client = self.runtime.block_on(IotHubTwinsClient::init(
            self.twins_store.clone(),
            get_twins_sender,
            reported_properties_sender,
            desired_properties_changed_receiver,
            self.desired_properties_updated_callback.take(),
        ));

        self.connection_receiver = Some(conn_receiver);
        self.twins_client = Some(twins_client.clone());

        let connection_task = {
            let cancellation = self.cancellation.clone();
            let mut registration_watch = self.registration_watch.clone();
            let registration_command_sender = self.registration_command_sender.clone();
            let method_handler = self.method_handler.take();
            let d2c_acknowledger = self.d2c_acknowledger.take().unwrap();
            let d2c_consumer = self.d2c_consumer.take().unwrap();
            let c2d_producer = self.c2d_producer.take().unwrap();
            async move {
                log::debug!("Registering to the platform");
                let (client, rumqttc_eventloop) =
                    Self::connect_iothub(&mut registration_watch).await?;
                log::debug!("Getting device ID");
                let device_id = rumqttc_eventloop.options.client_id();

                log::debug!("Building eventloop");

                let mut ingress_eventloop = EventLoop::new(
                    rumqttc_eventloop,
                    registration_watch.clone(),
                    registration_command_sender,
                    d2c_acknowledger,
                    cancellation.clone(),
                );

                log::debug!("Building and registering handlers");

                // Register handlers for incoming publish packets
                let twins_handler = TwinsHandler::new(desired_properties_sender, response_sender);
                ingress_eventloop.register_async_handler(twins_handler);

                let c2d_handler =
                    CloudToDeviceHandler::new(client.clone(), &device_id, c2d_producer);
                ingress_eventloop.register_async_handler(c2d_handler);

                if let Some(method_handler) = method_handler {
                    let method_handler = DirectMethodHandler::new(client.clone(), method_handler);
                    ingress_eventloop.register_handler(method_handler);
                }

                let connection_state_rx = ingress_eventloop.subscribe_to_state();

                // This is done before starting the eventloop so that this is the first thing that's in the queue.
                log::debug!("Subscribing to topics");
                let subscribe_task = ingress_eventloop.subscribe_all(client.clone()).await;

                log::debug!("Starting IotHub event loop");
                let mqtt_client_task = tokio::spawn(async move {
                    log::debug!("MQTT task is starting.");
                    ingress_eventloop.run().await;
                    log::debug!("MQTT task has ended.");
                });

                log::debug!("Awaiting acknowledgment of subscriptions");
                subscribe_task.wait().await?;

                let publish_topic = publish_topic(&device_id);

                let mut twins_middleware = TwinsMiddleware::new(
                    client.clone(),
                    twins_client.clone(),
                    get_twins_receiver,
                    reported_properties_receiver,
                    desired_properties_receiver,
                    desired_properties_changed_sender,
                    response_receiver,
                    connection_state_rx.clone(),
                    cancellation.clone(),
                );

                let twins_task = tokio::spawn(async move {
                    log::debug!("Twins task is starting.");
                    twins_middleware.process().await;
                    log::debug!("Twins task has ended.");
                });

                // Request twins and wait until a response arrives
                twins_client.get_twins().await;
                twins_client.desired_properties_changed().await?;

                let mut sender = Sender::new(
                    client.clone(),
                    registration_watch.clone(),
                    publish_topic,
                    d2c_consumer,
                    cancellation.child_token(),
                );

                let sender_task = tokio::spawn(async move {
                    log::debug!("Sender task is starting.");
                    sender.process_saved().await;
                    log::debug!("Sender task has ended.");
                });

                conn_sender
                    .send(OnlineConnection {
                        client,
                        state: connection_state_rx,
                    })
                    .map_err(|_| {
                        anyhow!("Nothing is listening for estabilishing MQTT connection.")
                    })?;

                Ok(vec![mqtt_client_task, sender_task, twins_task])
            }
        };

        Box::pin(connection_task)
    }

    fn error(&mut self) -> Option<Arc<dyn std::error::Error>> {
        self.connection_receiver
            .as_mut()
            .unwrap()
            .try_recv()
            .ok()
            .and_then(|o| match &*o.state.borrow() {
                State::Ready => None,
                State::ConnectionError(e) => {
                    let cast: Arc<dyn std::error::Error> = e.to_owned();
                    Some(cast)
                }
            })
    }
}

impl<F> Drop for IotHubConnection<F> {
    fn drop(&mut self) {
        if let Ok(online_connection) = self.connection_receiver.as_mut().unwrap().try_recv() {
            self.runtime.block_on(async {
                // Give ingress a fair chance to send any buffered messages and the Disconnect packet
                // When it sends disconnect it will cancel the token on its own
                // If it takes too long just cancel it directly and the loop will stop before Disconnect is sent

                let cancel = async {
                    // If this returns an error we ignore it and cancel directly after this task times out
                    _ = online_connection.client.disconnect().await;
                    self.cancellation.cancelled().await;
                };

                if tokio::time::timeout(Duration::from_secs(1), cancel)
                    .await
                    .is_err()
                {
                    log::warn!("Connection was not closed within timeout after disconnect was called. Cancelling execution of the SDK.");
                    self.cancellation.cancel();
                }
            });
        } else {
            log::info!("Connection was not properly set up before shutdown, cancelling all execution of the SDK.");
            self.cancellation.cancel();
        }
    }
}
