use async_trait::async_trait;
use rumqttc::{AsyncClient, Publish};

use super::super::query;
use super::super::topics::c2d_topic;
use crate::persistence::{sqlite_channel, CloudToDeviceMessage};

use super::AsyncHandler;

pub(crate) struct CloudToDeviceHandler {
    client: AsyncClient,
    c2d_prefix: String,
    producer: sqlite_channel::Sender<CloudToDeviceMessage>,
}

impl CloudToDeviceHandler {
    pub(crate) fn new(
        client: AsyncClient,
        device_id: &str,
        producer: sqlite_channel::Sender<CloudToDeviceMessage>,
    ) -> Self {
        CloudToDeviceHandler {
            client,
            c2d_prefix: c2d_topic(device_id),
            producer,
        }
    }
}

#[async_trait]
impl AsyncHandler for CloudToDeviceHandler {
    fn prefix(&self) -> Vec<&str> {
        vec![&self.c2d_prefix]
    }

    async fn handle(&mut self, publish: &Publish) {
        // The topic should be formatted like this:
        // devices/{device_id}/messages/devicebound/{property_bag}

        let topic = &publish.topic;
        log::debug!("Received C2D message on topic {topic}");

        let Some(properties) = publish.topic.strip_prefix(&self.c2d_prefix) else {
            // Ignore malformed requests
            return;
        };

        let Ok(properties) = query::parse(properties).inspect_err(|e| {
            log::error!("Failed parsing cloud to device message topic `{topic}`: {e:?}");
        }) else {
            return;
        };

        let msg = CloudToDeviceMessage::new(
            publish.payload.to_vec(),
            properties
                .into_iter()
                .map(|(k, v)| (k, v.unwrap_or_default()))
                .collect(),
        );

        if let Err(e) = self.producer.send(&msg).await {
            // Not much we can do about this. The message will be ignored and lost.
            log::error!(
                "Cannot store a cloud-to-device message. It will not be processed: {}",
                e
            );
        }
        // This may return an errored result which we ignore. If this fails then the MQTT has already shut down. We will shut down soon too
        _ = self.client.ack(publish).await;
    }
}
