use std::{
    collections::HashSet,
    sync::Arc,
    time::{Duration, Instant},
};

use anyhow::{anyhow, Result};
use rumqttc::{
    AsyncClient, ConnectReturnCode, ConnectionError, Event, Outgoing, Packet, QoS, SubscribeFilter,
    SubscribeReasonCode,
};
use tokio::{
    select,
    sync::{broadcast, watch},
};
use tokio_util::sync::CancellationToken;

use super::token_handler::{RegistrationCommand, RegistrationCommandSender, RegistrationWatch};
use super::topics;
use crate::persistence::Acknowledger;

use super::{
    handlers::{AsyncHandler, Handler},
    State,
};

pub(super) struct SubscriptionTask {
    receiver: broadcast::Receiver<usize>,
    total: usize,
}

impl SubscriptionTask {
    pub(super) async fn wait(mut self) -> Result<()> {
        let mut acked = 0;
        while acked < self.total {
            // We need to run this on the runtime because there is nothing currently running there.
            // Therefore this would deadlock otherwise since the event loop is not running
            acked += self.receiver.recv().await.map_err(|_|
                anyhow!("Channel for subscribe acknowledgements was closed before startup finished, possibly because subscription failed.")
            )?;
            log::debug!("Subscription {acked}/{} acknowledged", self.total);
        }

        Ok(())
    }
}

pub(super) struct EventLoop {
    device_id: String,
    state: watch::Sender<State>,
    pending_d2c: HashSet<u16>,
    suback_sender: broadcast::Sender<usize>,
    registration_watch: RegistrationWatch,
    registration_command_sender: RegistrationCommandSender,
    acknowledger: Acknowledger,
    cancellation: CancellationToken,
    rumqttc_eventloop: rumqttc::EventLoop,
    publish_handlers: Vec<Box<dyn Handler + Send + Sync>>,
    async_publish_handlers: Vec<Box<dyn AsyncHandler + Send + Sync>>,
}

impl EventLoop {
    pub(super) fn new(
        rumqttc_eventloop: rumqttc::EventLoop,
        registration_watch: RegistrationWatch,
        registration_command_sender: RegistrationCommandSender,
        acknowledger: Acknowledger,
        cancellation: CancellationToken,
    ) -> Self {
        let (suback_sender, _) = broadcast::channel(10);
        let (state_sender, _) = watch::channel(State::Ready);

        let registration = registration_watch.borrow();
        let device_id = registration
            .as_ref()
            .expect("Registration worker must not send None")
            .iot_hub_device_id()
            .expect("Unable to parse device ID from SAS token from DRS")
            .to_owned();
        drop(registration);

        EventLoop {
            device_id,
            state: state_sender,
            suback_sender,

            pending_d2c: HashSet::new(),
            publish_handlers: Vec::new(),
            async_publish_handlers: Vec::new(),

            acknowledger,
            rumqttc_eventloop,
            registration_watch,
            registration_command_sender,
            cancellation,
        }
    }

    pub(super) fn subscribe_to_state(&self) -> watch::Receiver<State> {
        self.state.subscribe()
    }

    pub(super) fn register_handler(&mut self, handler: impl Handler + Send + Sync + 'static) {
        self.publish_handlers.push(Box::new(handler));
    }

    pub(super) fn register_async_handler(
        &mut self,
        handler: impl AsyncHandler + Send + Sync + 'static,
    ) {
        self.async_publish_handlers.push(Box::new(handler));
    }

    pub(super) async fn subscribe_all(&mut self, client: AsyncClient) -> SubscriptionTask {
        let receiver = self.suback_sender.subscribe();

        let sync_prefixes = self.publish_handlers.iter().flat_map(|h| h.prefix());
        let async_prefixes = self.async_publish_handlers.iter().flat_map(|h| h.prefix());

        let filters = sync_prefixes
            .chain(async_prefixes)
            .map(|prefix| SubscribeFilter {
                path: format!("{prefix}#"),
                qos: QoS::AtLeastOnce,
            })
            .collect::<Vec<_>>();

        let sub_cnt = filters.len();

        client
            .subscribe_many(filters)
            .await
            .expect("rumqttc has closed eventloop before SDK started.");

        SubscriptionTask {
            receiver,
            total: sub_cnt,
        }
    }

    pub(super) async fn run(&mut self) {
        loop {
            select! {
                () = self.cancellation.cancelled() => {
                    log::debug!("Stopping MQTT because of cancellation");
                    break;
                },
                notification = self.rumqttc_eventloop.poll() => self.process_notification(notification).await,
            }
        }
    }

    async fn process_notification(&mut self, notification: Result<Event, ConnectionError>) {
        match notification {
            Ok(event) => {
                match event {
                    Event::Incoming(inner) => self.process_incoming_message(inner).await,
                    Event::Outgoing(inner) => self.process_outgoing_message(inner),
                };
            }
            Err(e) => {
                log::debug!("Error in MQTT: {e:?}");
                let e = Arc::new(e);
                self.state.send_replace(State::ConnectionError(e.clone()));
                if self.cancellation.is_cancelled() {
                    log::info!("Shutting down during errored state because of cancellation.");
                    return;
                }
                // This panics if the TokenHandler has already failed
                if self
                    .registration_watch
                    .has_changed()
                    .expect("Unable to get registration updates")
                {
                    log::debug!("Updating IoT Hub authentication.");
                    let (username, _) = self
                        .rumqttc_eventloop
                        .options
                        .credentials()
                        .expect("rumqtt must have configured credentials");
                    let registration = self.registration_watch.borrow_and_update();
                    self.rumqttc_eventloop.options.set_credentials(
                        username,
                        registration
                            .as_ref()
                            .expect("Registration worker must not send None")
                            .sas()
                            .expect("Unable to parse SAS token from buffered DRS response during reconnect")
                    );
                } else {
                    // Request a refresh of the device registration to obtain a new SAS token for IoT Hub
                    if let ConnectionError::ConnectionRefused(
                        ConnectReturnCode::NotAuthorized
                        | ConnectReturnCode::BadUserNamePassword
                        | ConnectReturnCode::ServiceUnavailable,
                    ) = e.as_ref()
                    {
                        match self.registration_command_sender.send(
                            RegistrationCommand::RefreshRegistration {
                                time: Instant::now(),
                            },
                        ) {
                            Ok(()) => log::debug!("Requesting IoT Hub authentication refresh."),
                            Err(e) => log::error!(
                                "Unable to request IoT Hub authentication refresh: {e:?}"
                            ),
                        }
                    }
                    log::debug!("5 second backoff for eventloop to self-heal.");
                    // We will wait and hope everything will sort itself out
                    tokio::time::sleep(Duration::from_secs(5)).await;
                }
            }
        }
    }

    async fn process_incoming_message(&mut self, packet: Packet) {
        log::trace!("Received = {:?}", packet);
        self.state.send_replace(State::Ready);
        match packet {
            Packet::Publish(publish) => {
                for handler in &mut self.async_publish_handlers {
                    for prefix in handler.prefix() {
                        if publish.topic.starts_with(prefix) {
                            handler.handle(&publish).await;
                            return;
                        }
                    }
                }
                for handler in &mut self.publish_handlers {
                    for prefix in handler.prefix() {
                        if publish.topic.starts_with(prefix) {
                            handler.handle(&publish);
                            return;
                        }
                    }
                }
                log::warn!(
                    "Ignoring message received on unexpected topic {:?}",
                    &publish.topic,
                );
            }
            Packet::PubAck(ack) => {
                if self.pending_d2c.contains(&ack.pkid) {
                    // We are going to assume that IoT Hub confirms messages in order.
                    // This seems to not be a hard requirement by MQTT but seems to be safe to do.
                    // We are sending messages in order they were saved in SQLite and we depend on AUTOINCREMEMNT.
                    log::trace!("Got acknowledgment for device-to-cloud message");
                    if let Err(e) = self.acknowledger.remove_oldest().await {
                        log::error!("Unable to remove acknowledged device-to-cloud message. This or subsequent messages may be duplicated and received at a later time. Inner: {}", e);
                    }
                }
                // Else we got PUBACK for stuff like reported properties update -- we can ignore these here
            }
            Packet::SubAck(ack) => {
                if ack
                    .return_codes
                    .iter()
                    .any(|r| *r == SubscribeReasonCode::Failure)
                {
                    // We just log here but we ignore the subscription failure. Worst case we do not receive C2D or other messages but the rest of SDK will continue working
                    log::warn!("Unable to subscribe to some topics");
                }
                // Ignore errors -- if this fails the client has already shut down, it is not interested in this, and the event loop will soon shutdown too
                let new_subscription_cnt = ack.return_codes.len();
                log::debug!("Subscribed to {new_subscription_cnt} additional topics");
                _ = self.suback_sender.send(new_subscription_cnt);
            }
            Packet::UnsubAck(_) => todo!(),
            Packet::Connect(_) => unreachable!("Client is responsible for connection initiation"),
            Packet::PubRec(_) | Packet::PubRel(_) | Packet::PubComp(_) => {
                unreachable!("Azure IoT Hub does not support QoS 2")
            }
            Packet::Subscribe(_) | Packet::Unsubscribe(_) => {
                unreachable!("Only the client can subscribe to topics")
            }
            Packet::Disconnect => unreachable!("Only the client sends disconnect"),
            // Packet::ConnAck(_) => {}, // Ignore ConnAck; rumqttc will handle everything for us
            // Packet::PingReq => {},
            // Packet::PingResp => {},
            _ => {}
        }
    }

    fn process_outgoing_message(&mut self, packet: Outgoing) {
        log::trace!("Sending = {:?}", packet);
        match packet {
            // If we sent disconnect we are shutting down so the task may end
            // No more MQTT messages may be processed anyway
            Outgoing::Disconnect => {
                log::debug!("Stopping MQTT because of disconnect packet");
                self.cancellation.cancel();
            }
            Outgoing::Publish(publish, topic) => {
                if topic.starts_with(&topics::publish_topic(&self.device_id)) {
                    self.pending_d2c.insert(publish);
                }
                // Else this is request-response type of exchange such as reported properties update
                // We do not care about packet IDs or anything like that
            }
            #[allow(clippy::match_same_arms)]
            Outgoing::Subscribe(_) => {
                // We counted subscriptions beforehand so we do not want to count them now.
                // We cannot subscribe reliably during runtime.
            }
            Outgoing::Unsubscribe(_) => todo!(),
            Outgoing::PubRec(_) | Outgoing::PubComp(_) | Outgoing::PubRel(_) => {
                unreachable!("Azure IoT Hub does not support QoS 2")
            }
            Outgoing::AwaitAck(_) => {
                log::warn!("MQTT is blocking until an out-of-order message is acknowledged.");
            }
            Outgoing::PubAck(_) | Outgoing::PingReq | Outgoing::PingResp => {}
        }
    }
}
