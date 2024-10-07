use std::sync::Arc;

use crate::cloud::drs::RegistrationResponse;
use crate::persistence::{CloseOption, Compression, Consumer, DeviceMessage};
use anyhow::{bail, Context, Result};
use brotli::{enc::BrotliEncoderParams, BrotliCompress};
use rumqttc::{AsyncClient, QoS};
use serde::Deserialize;
use serde_json::json;
use tokio::{select, sync::watch};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

#[derive(Debug)]
pub(super) struct Sender {
    mqtt: AsyncClient,
    registration_watch: watch::Receiver<Option<RegistrationResponse>>,
    topic: String,
    message_queue: Consumer,
    cancellation: CancellationToken,
}

impl Sender {
    pub(super) fn new(
        mqtt: AsyncClient,
        registration_watch: watch::Receiver<Option<RegistrationResponse>>,
        topic: String,
        message_queue: Consumer,
        cancellation: CancellationToken,
    ) -> Self {
        Self {
            mqtt,
            registration_watch,
            topic,
            message_queue,
            cancellation,
        }
    }

    pub(super) async fn process_saved(&mut self) {
        loop {
            select!(
                () = self.cancellation.cancelled() => break,
                // At this point we panic. I don't know what else to do as this is core functionality.
                // In a better world I will let the user know that the SDK stopped working and they need to restart or something.
                // For now this should panic on our own thread (not on user's thread) and cascade to the SDK itself which will probably return Error when the user tries to send more messages.
                Some(msg) = self.message_queue.get_message() => self.publish_iothub(msg).await.unwrap(),
            );
        }
    }

    async fn publish_iothub(&self, msg: DeviceMessage) -> Result<()> {
        fn encode_property(key: &str, value: &str) -> String {
            let value = urlencoding::encode(value);
            format!("{key}={value}")
        }

        let id = msg
            .id
            .expect("We have a saved message without an ID. This should never happen.");

        let mut properties = Vec::new();

        if let Some(stream_group) = &msg.stream_group {
            properties.push(encode_property("stream-group-name", stream_group));
        } else {
            log::info!(
                "The Stream Group of Message {} is not specified, \
                the default Stream Group of the current Workspace will be filled in by the Platform.",
                id
            );
        }

        if let Some(stream) = &msg.stream {
            properties.push(encode_property("stream-name", stream));
        } else {
            log::info!(
                "The Stream of Message {} is not specified, \
                the default Stream of the current Stream Group will be filled in by the Platform.",
                id
            );
        }

        if let Some(site_id) = &msg.site_id {
            properties.push(encode_property("site-id", site_id));
        }

        if let Some(batch_id) = &msg.batch_id {
            properties.push(encode_property("batch-id", batch_id));
        }

        if let Some(batch_slice_id) = &msg.batch_slice_id {
            properties.push(encode_property("batch-slice-id", batch_slice_id));
        }

        if let Some(message_id) = &msg.message_id {
            properties.push(encode_property("message-id", message_id));
        }

        if let Some(chunk_id) = &msg.chunk_id {
            properties.push(encode_property("chunk-id", chunk_id));
        }

        let content = match (
            msg.content.is_empty(),
            get_compression_quality(msg.compression),
        ) {
            (false, Some(compression_quality)) => {
                log::trace!("Compressing message {}", id);
                let compressed_content = compress_message(&msg.content, compression_quality)?;

                if compressed_content.len() < msg.content.len() {
                    properties.push(String::from("content-encoding=br"));
                    compressed_content
                } else {
                    log::trace!(
                        "Compressing message {} would not decrease its size (original: {}B, compressed: {}B), sending uncompressed",
                        id, msg.content.len(), compressed_content.len());
                    msg.content
                }
            }
            _ => msg.content,
        };

        let content = if is_file_upload(&content) {
            log::trace!("Sending message {} through file upload", id);
            properties.push(String::from("has-externalized-payload=true"));
            let blob_name = loop {
                match self.publish_file(content.as_ref()) {
                    Ok(name) => break name,
                    Err(e) => log::error!("Failed uploading file: {:?}", e),
                }
            };
            format!(r#"{{"link":"{blob_name}"}}"#).into_bytes()
        } else {
            content
        };

        match &msg.close_option {
            CloseOption::None => {}
            CloseOption::Close => {
                properties.push(String::from("complete-batch=true"));
            }
            CloseOption::CloseOnly => {
                properties.push(String::from("complete-batch=true"));
                properties.push(String::from("ignore-payload=true"));
            }
            CloseOption::CloseMessageOnly => {
                properties.push(String::from("complete-message=true"));
                properties.push(String::from("ignore-payload=true"));
            }
        }

        let properties = properties.join("&");

        let topic = format!("{}{}", &self.topic, properties);

        log::trace!("Sending message {}", id);
        let res = self
            .mqtt
            .publish(topic.to_string(), QoS::AtLeastOnce, false, content)
            .await;

        if res.is_err() {
            // We were not able to send the message.
            // This should only happen when MQTT AsyncClient has already closed its eventloop, which in turn should only happen during ingress shutdown
            // The following if should never be true
            if !self.cancellation.is_cancelled() {
                log::error!("Unable to publish message even though the client is not stopping");
                bail!("rumqttc event loop has closed its request queue even though the client has not cancelled its own token.");
            }
            log::trace!("Message not sent during shutdown");
            return Ok(());
        }

        log::trace!("Message sent {}", id);

        Ok(())
    }

    fn publish_file(&self, content: &[u8]) -> Result<String> {
        let registration = self.registration_watch.borrow();
        let registration = registration
            .as_ref()
            .expect("Registration worker must not send None");
        let host_name = &registration.iot_hub_host_name;
        let device_id = &registration.iot_hub_device_id()?;
        let auth_header = &registration
            .sas()
            .context("Unable to parse SAS token during file upload")?;

        let init_uri =
            format!("https://{host_name}/devices/{device_id}/files?api-version=2020-03-13");
        let complete_uri = format!(
            "https://{host_name}/devices/{device_id}/files/notifications?api-version=2020-03-13"
        );

        let connector =
            Arc::new(native_tls::TlsConnector::new().expect("Unable to build TLS connector"));
        let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

        let blob_name = Uuid::new_v4().to_string();

        let init: FileUploadInit = agent
            .post(&init_uri)
            .set("Content-Type", "application/json")
            .set("Authorization", auth_header)
            .send_json(json!({
                "blobName": &blob_name,
            }))
            .context("Failed sending request to initiate file upload")?
            .into_json()
            .context("Failed parsing response during file upload initation")?;

        let blob_sas = format!(
            "https://{}/{}/{}{}",
            init.host_name, init.container_name, init.blob_name, init.sas_token
        );

        agent
            .put(&blob_sas)
            .set("x-ms-blob-type", "BlockBlob")
            .send_bytes(content)
            .context("Failed uploading file to blob")?;

        agent
            .post(&complete_uri)
            .set("Content-Type", "application/json")
            .set("Authorization", auth_header)
            .send_json(json!({
                "correlationId": init.correlation_id,
                "isSuccess": true,
                "statusCode": 200,
                "statusDescription": "Done",
            }))
            .context("Failed sending request to complete file upload")?;

        Ok(blob_name)
    }
}

fn get_compression_quality(compression: Compression) -> Option<i32> {
    match compression {
        Compression::None => None,
        Compression::BrotliFastest => Some(1),
        Compression::BrotliSmallestSize => Some(11),
    }
}

fn compress_message(content: &Vec<u8>, quality: i32) -> Result<Vec<u8>, anyhow::Error> {
    let brotli_params = BrotliEncoderParams {
        quality,
        ..Default::default()
    };

    let mut compressed_content = Vec::new();
    BrotliCompress(
        &mut content.as_slice(),
        &mut compressed_content,
        &brotli_params,
    )?;

    Ok(compressed_content)
}

fn is_file_upload(content: &[u8]) -> bool {
    // The limit is 256 KiB for telemetry messages including headers
    // This is coarse but should work well enough
    content.len() > 250_000
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct FileUploadInit {
    correlation_id: String,
    host_name: String,
    container_name: String,
    blob_name: String,
    sas_token: String,
}
