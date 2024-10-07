use std::collections::HashMap;
use std::{path::Path, str::FromStr};

use crate::cloud::dps::{ProvisioningToken, RegistrationToken};
use anyhow::{Context, Result};
use http::Uri;
use sqlite::SdkConfiguration;
use sqlite_channel::{Receiver, Sender};
use tokio::{
    select,
    sync::{mpsc, watch},
};
use tokio_util::sync::CancellationToken;
use twins::Twin;

use self::sqlite::SqliteStore;

pub mod c2d;
pub mod sqlite;
pub mod sqlite_channel;
pub mod twins;

pub struct Store {
    pub store: SqliteStore,
    pub d2c_producer: Producer,
    pub d2c_consumer: Consumer,
    pub d2c_acknowledger: Acknowledger,
    pub configuration_store: ConfigurationStore,
    pub c2d_producer: Sender<CloudToDeviceMessage>,
    pub c2d_consumer: Receiver<CloudToDeviceMessage>,
    pub twins_store: TwinsStore,
}

#[derive(Debug)]
pub struct Producer {
    inner: SqliteStore,
    sender: watch::Sender<i32>,
}

#[derive(Debug)]
pub struct Consumer {
    receiver: mpsc::Receiver<DeviceMessage>,
}

#[derive(Debug)]
pub struct Acknowledger {
    inner: SqliteStore,
}

#[derive(Debug, Clone)]
pub struct ConfigurationStore {
    inner: SqliteStore,
    site_id: Option<String>,
}

impl Producer {
    pub async fn add(&self, mut msg: DeviceMessage) -> Result<()> {
        let id = self
            .inner
            .store_message(&msg)
            .await
            .context("Unable to store device to cloud message")?;
        msg.id = Some(id);
        self.sender
            .send(id)
            .context("Unable to send notification of new message")?;

        Ok(())
    }

    pub async fn count(&self) -> Result<usize> {
        self.inner.message_count().await
    }
}

impl Consumer {
    pub async fn get_message(&mut self) -> Option<DeviceMessage> {
        self.receiver.recv().await
    }
}

impl Acknowledger {
    pub async fn remove_oldest(&self) -> Result<()> {
        self.inner.remove_oldest_message().await
    }
}

#[allow(dead_code)] // Not all the load methods are currently used, but we'll keep the interface "round" for now
impl ConfigurationStore {
    pub async fn load_instance_url(&self) -> Result<Uri> {
        let url = self.inner.load_instance_url().await?;
        Uri::from_str(&url).context("Unable to parse the Platform instance URL from configuration.")
    }

    pub async fn load_provisioning_token(&self) -> Result<ProvisioningToken> {
        self.inner.load_provisioning_token().await
    }

    pub async fn load_registration_token(&self) -> Result<RegistrationToken> {
        self.inner.load_registration_token().await
    }

    pub async fn save_provisioning_token(&self, token: &ProvisioningToken) -> Result<()> {
        self.inner.save_provisioning_token(token).await
    }

    pub async fn save_registration_token(&self, token: &RegistrationToken) -> Result<()> {
        self.inner.save_registration_token(token).await
    }

    pub async fn load_requested_device_id(&self) -> Option<String> {
        self.inner.load_requested_device_id().await.ok().flatten()
    }

    pub async fn load_workspace_id(&self) -> Result<String> {
        self.inner.load_workspace_id().await
    }

    pub async fn save_workspace_id(&self, workspace_id: &str) -> Result<()> {
        self.inner.save_workspace_id(workspace_id).await
    }

    pub async fn load_device_id(&self) -> Result<String> {
        self.inner.load_device_id().await
    }

    pub async fn save_device_id(&self, device_id: &str) -> Result<()> {
        self.inner.save_device_id(device_id).await
    }

    pub fn site_id(&self) -> Option<&str> {
        self.site_id.as_deref()
    }
}

#[derive(Debug, Clone)]
pub struct TwinsStore {
    inner: SqliteStore,
}

impl TwinsStore {
    pub async fn load_desired_properties(&self) -> Result<Option<Twin>> {
        self.inner.load_desired_properties().await
    }

    pub async fn load_reported_properties(&self) -> Result<Option<Twin>> {
        self.inner.load_reported_properties().await
    }

    pub async fn save_desired_properties(&self, twin: &Twin) -> Result<()> {
        self.inner.save_desired_properties(twin).await
    }

    pub async fn save_reported_properties(&self, twin: &Twin) -> Result<()> {
        self.inner.save_reported_properties(twin).await
    }
}

pub async fn create(
    store_path: &Path,
    config: &SdkConfiguration,
    cancellation_token: CancellationToken,
) -> Result<Store> {
    let sqlite = SqliteStore::init(store_path, config).await?;

    Ok(start(sqlite, config, cancellation_token))
}

fn start(
    sqlite: SqliteStore,
    config: &SdkConfiguration,
    cancellation_token: CancellationToken,
) -> Store {
    let (message_sender, message_receiver) = mpsc::channel(100);
    let (latest_msg_id_sender, mut latest_msg_id_receiver) = watch::channel(-1);

    {
        let sqlite = sqlite.clone();

        tokio::spawn(async move {
            let mut last_id = -1;
            loop {
                let messages = sqlite
                    .list_messages_after(last_id)
                    .await
                    .expect("Unable to load saved device messages");

                if !messages.is_empty() {
                    log::trace!(
                        "At least {} messages were persisted and are ready to be sent",
                        messages.len()
                    );
                    last_id = messages
                        .last()
                        .expect("We checked that the vec is not empty")
                        .id
                        .expect("ID is not empty after being stored in store");

                    for msg in messages {
                        select!(
                            () = cancellation_token.cancelled() => {
                                // Cancelled
                                return;
                            },
                            sent = message_sender.send(msg) => {
                                if sent.is_err() {
                                    // No more receivers
                                    log::debug!("There is no one listening for messages to be sent. Finishing sender publisher.");
                                    return;
                                }
                            },
                        );
                    }
                } else if *latest_msg_id_receiver.borrow_and_update() == last_id {
                    select!(
                        () = cancellation_token.cancelled() => {
                            // Cancelled
                            return;
                        },
                        read = latest_msg_id_receiver.changed() => {
                            if read.is_err() {
                                // No more updates are coming
                                return;
                            }
                            // else we start running the loop again
                        },
                    );
                }
            }
        });
    }

    let producer = Producer {
        inner: sqlite.clone(),
        sender: latest_msg_id_sender,
    };

    let consumer = Consumer {
        receiver: message_receiver,
    };

    let acknowledger = Acknowledger {
        inner: sqlite.clone(),
    };

    let (c2d_producer, c2d_consumer) = sqlite_channel::channel(sqlite.clone());

    let token_store = ConfigurationStore {
        inner: sqlite.clone(),
        site_id: config.site_id.clone(),
    };

    let twins_store = TwinsStore {
        inner: sqlite.clone(),
    };

    Store {
        store: sqlite,
        d2c_producer: producer,
        d2c_consumer: consumer,
        d2c_acknowledger: acknowledger,
        configuration_store: token_store,
        c2d_producer,
        c2d_consumer,
        twins_store,
    }
}

#[derive(Debug)]
pub struct DeviceMessage {
    pub id: Option<i32>,
    pub site_id: Option<String>,
    pub stream_group: Option<String>,
    pub stream: Option<String>,
    pub batch_id: Option<String>,
    pub message_id: Option<String>,
    pub content: Vec<u8>,
    pub close_option: CloseOption,
    pub compression: Compression,
    pub batch_slice_id: Option<String>,
    pub chunk_id: Option<String>,
}

/// **Warning**: Don't use, the interface for Cloud-to-Device Messages hasn't been finalized yet.
#[doc(hidden)]
#[derive(Debug)]
pub struct CloudToDeviceMessage {
    pub(crate) id: Option<i32>,
    pub content: Vec<u8>,
    pub properties: HashMap<String, String>,
}

impl CloudToDeviceMessage {
    #[must_use]
    pub fn new(content: Vec<u8>, properties: HashMap<String, String>) -> Self {
        CloudToDeviceMessage {
            id: None,
            content,
            properties,
        }
    }
}

#[derive(Copy, Clone, Debug, sqlx::Type)]
pub enum CloseOption {
    None,
    Close,
    CloseOnly,
    CloseMessageOnly,
}

#[derive(Copy, Clone, Debug, sqlx::Type)]
pub enum Compression {
    None,
    BrotliFastest,
    BrotliSmallestSize,
}
