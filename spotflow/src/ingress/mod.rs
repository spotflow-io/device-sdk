use std::panic::RefUnwindSafe;
use std::time::Duration;
use std::{path::Path, sync::Arc};

use anyhow::Result;
use base::BaseConnection;
use c2d::CloudToDeviceMessageGuard;

use crate::cloud::drs::RegistrationResponse;
pub use crate::connection::twins::DesiredProperties;
pub use crate::connection::twins::DesiredPropertiesUpdatedCallback;
use crate::persistence::sqlite::SdkConfiguration;

mod base;
mod builder;
pub mod c2d;

pub use builder::DeviceClientBuilder;
pub use builder::ProvisioningOperation;
pub use builder::ProvisioningOperationDisplayHandler;
pub use c2d::CloudToDeviceMessage;

use crate::connection::ConnectionImplementation;

use crate::{persistence, ProcessSignalsSource};

/// The compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
#[derive(Clone, Debug)]
pub enum Compression {
    /// Compress the message using the fastest compression algorithm settings.
    Fastest,
    /// Compress the message using the algorithm settings that produce the smallest size.
    /// Beware that this may be significantly slower than the fastest compression.
    /// We recommend to test the performance of your application with this setting before using it in production.
    SmallestSize,
}

impl Compression {
    fn to_persisted_compression(compression: &Option<Compression>) -> persistence::Compression {
        match compression {
            Some(Compression::Fastest) => persistence::Compression::BrotliFastest,
            Some(Compression::SmallestSize) => persistence::Compression::BrotliSmallestSize,
            None => persistence::Compression::None,
        }
    }
}

/// A set of options for sending [Messages](https://docs.spotflow.io/send-data/#message) to
/// a [Stream](https://docs.spotflow.io/send-data/#stream).
#[derive(Clone, Debug, Default)]
pub struct MessageContext {
    stream_group: Option<String>,
    stream: Option<String>,
    compression: Option<Compression>,
}

impl MessageContext {
    // Create a new instance of [`MessageContext`] with the provided [Stream Group](https://docs.spotflow.io/send-data/#stream-group)
    // and [Stream](https://docs.spotflow.io/send-data/#stream).
    #[must_use]
    pub fn new(stream_group: Option<String>, stream: Option<String>) -> Self {
        Self {
            stream_group,
            stream,
            compression: None,
        }
    }

    /// Get the [Stream Group](https://docs.spotflow.io/send-data/#stream-group) where
    /// [Messages](https://docs.spotflow.io/send-data/#message) will be sent to.
    #[must_use]
    pub fn stream_group(&self) -> Option<&str> {
        self.stream_group.as_deref()
    }

    /// Set the [Stream Group](https://docs.spotflow.io/send-data/#stream-group) where
    /// [Messages](https://docs.spotflow.io/send-data/#message) will be sent to.
    pub fn set_stream_group(&mut self, stream_group: Option<String>) {
        self.stream_group = stream_group;
    }

    /// Get the [Stream](https://docs.spotflow.io/send-data/#stream) where
    /// [Messages](https://docs.spotflow.io/send-data/#message) will be sent to.
    #[must_use]
    pub fn stream(&self) -> Option<&str> {
        self.stream.as_deref()
    }

    /// Set the [Stream](https://docs.spotflow.io/send-data/#stream) where
    /// [Messages](https://docs.spotflow.io/send-data/#message) will be sent to.
    pub fn set_stream(&mut self, stream: Option<String>) {
        self.stream = stream;
    }

    /// Get the compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
    #[must_use]
    pub fn compression(&self) -> Option<Compression> {
        self.compression.clone()
    }

    /// Set the compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
    pub fn set_compression(&mut self, compression: Option<Compression>) {
        self.compression = compression;
    }
}

/// A client communicating with the Platform.
///
/// Create its instance using [`DeviceClientBuilder::build`].
///
/// The client stores all outgoing communication to the local database file and then sends it in a background thread asynchronously.
/// Thanks to that, it works even when the connection is unreliable.
/// Similarly, the client also stores all ingoing communication to the local database file and deletes it
/// only after the application processes it.
#[derive(Clone)]
pub struct DeviceClient {
    connection: Arc<BaseConnection<dyn ConnectionImplementation + Send + Sync>>,
}

impl DeviceClient {
    /// Starts an ingress and saves the provided tokens and URLs to a state file. If the provided file does not exist this function creates it.
    /// It also makes sure that both desired and reported properties of the Device Twin are available.
    fn new<F>(
        config: SdkConfiguration,
        path: &Path,
        method_handler: Option<F>,
        desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
        signals_src: Option<Box<dyn ProcessSignalsSource>>,
        initial_registration_response: Option<RegistrationResponse>,
    ) -> Result<DeviceClient>
    where
        F: Fn(String, &[u8]) -> (i32, Vec<u8>) + Send + Sync + RefUnwindSafe + 'static,
    {
        let connection = BaseConnection::init_ingress(
            config,
            path,
            method_handler,
            desired_properties_updated_callback,
            signals_src,
            initial_registration_response,
        )?;

        connection.wait_properties_ready()?;

        let connection = Arc::new(connection);

        Ok(DeviceClient { connection })
    }

    /// Get the ID of the [Workspace](https://docs.spotflow.io/manage-access/workspaces/) to which the
    /// [Device](https://docs.spotflow.io/connect-devices/#device) belongs.
    pub fn workspace_id(&self) -> Result<String> {
        self.connection.workspace_id()
    }

    /// The [Device ID](https://docs.spotflow.io/connect-devices/#device-id). Note that the value
    /// might differ from the one requested in [`DeviceClientBuilder::new`] if the technician overrides it during the approval
    /// of the [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
    pub fn device_id(&self) -> Result<String> {
        self.connection.device_id()
    }

    /// Enqueue a [Message](https://docs.spotflow.io/send-data/#message) to
    /// be sent to the Platform.
    ///
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) doesn't have a
    /// [Message ID Autofill Pattern](https://docs.spotflow.io/send-data/#message-id-autofill-pattern),
    /// you must provide the `message_id`.
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) groups
    /// [Messages](https://docs.spotflow.io/send-data/#message) into
    /// [Batches](https://docs.spotflow.io/send-data/#batch) and doesn't have a
    /// [Batch ID Autofill Pattern](https://docs.spotflow.io/send-data/#batch-id-autofill-pattern),
    /// you must provide the `batch_id`. See [User Guide](https://docs.spotflow.io/send-data/) for
    /// more details.
    ///
    /// The method returns right after it saves the [Message](https://docs.spotflow.io/send-data/#message) to
    /// the queue in the local database file. A background thread asynchronously sends the messages from the queue to the Platform.
    /// You can check the number of pending messages in the queue using [`DeviceClient::pending_messages_count`].
    pub fn enqueue_message(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        message_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        self.connection
            .enqueue_message(message_context, batch_id, message_id, payload)
    }

    /// Enqueue a [Message](https://docs.spotflow.io/send-data/#message) to
    /// be sent to the Platform.
    ///
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) doesn't have a
    /// [Message ID Autofill Pattern](https://docs.spotflow.io/send-data/#message-id-autofill-pattern),
    /// you must provide the `message_id`.
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) groups
    /// [Messages](https://docs.spotflow.io/send-data/#message) into
    /// [Batches](https://docs.spotflow.io/send-data/#batch) and doesn't have a
    /// [Batch ID Autofill Pattern](https://docs.spotflow.io/send-data/#batch-id-autofill-pattern),
    /// you must provide the `batch_id`. See [User Guide](https://docs.spotflow.io/send-data/) for
    /// more details.
    /// Optionally, you can provide also `batch_slice_id` to use Batch Slices and `chunk_id` to use Message Chunking.
    ///
    /// The method returns right after it saves the [Message](https://docs.spotflow.io/send-data/#message) to
    /// the queue in the local database file. A background thread asynchronously sends the messages from the queue to the Platform.
    /// You can check the number of pending messages in the queue using [`DeviceClient::pending_messages_count`].
    pub fn enqueue_message_advanced(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        batch_slice_id: Option<String>,
        message_id: Option<String>,
        chunk_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        self.connection.enqueue_message_advanced(
            message_context,
            batch_id,
            batch_slice_id,
            message_id,
            chunk_id,
            payload,
        )
    }

    /// Enqueue the manual completion of the current [Batch](https://docs.spotflow.io/send-data/#batch) to
    /// be sent to the Platform.
    ///
    /// The Platform also completes the previous [Batch](https://docs.spotflow.io/send-data/#batch) automatically
    /// when the new one starts. Therefore, you might not need to call this method at all.
    /// See [User Guide](https://docs.spotflow.io/send-data/) for more details.
    ///
    /// The method returns right after it saves the batch-completing [Message](https://docs.spotflow.io/send-data/#message) to
    /// the queue in the local database file. A background thread asynchronously sends the messages from the queue to the Platform.
    /// You can check the number of pending messages in the queue using [`DeviceClient::pending_messages_count`].
    pub fn enqueue_batch_completion(
        &self,
        message_context: &MessageContext,
        batch_id: String,
    ) -> Result<()> {
        self.connection
            .enqueue_batch_completion(message_context, batch_id)
    }

    /// Enqueue the manual completion of the current [Message](https://docs.spotflow.io/send-data/#message) to
    /// be sent to the Platform. Use this methods when Message Chunking is used.
    ///
    /// The method returns right after it saves the message-completing Message to the queue in the local database file.
    /// A background thread asynchronously sends the Messages from the queue to the Platform.
    /// You can check the number of pending messages in the queue using [`DeviceClient::pending_messages_count`].
    pub fn enqueue_message_completion(
        &self,
        message_context: &MessageContext,
        batch_id: String,
        message_id: String,
    ) -> Result<()> {
        self.connection
            .enqueue_message_completion(message_context, batch_id, message_id)
    }

    /// Send a [Message](https://docs.spotflow.io/send-data/#message) to
    /// the Platform.
    ///
    /// **Warning:** This method blocks the current thread until the Message (and all the Messages enqueued before it)
    /// is sent to the Platform. If your Device doesn't have a stable Internet connection, consider using `spotflow_client_enqueue_message` instead.
    ///
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) doesn't have a
    /// [Message ID Autofill Pattern](https://docs.spotflow.io/send-data/#message-id-autofill-pattern),
    /// you must provide `message_id`.
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) groups
    /// [Messages](https://docs.spotflow.io/send-data/#message) into
    /// [Batches](https://docs.spotflow.io/send-data/#batch) and doesn't have a
    /// [Batch ID Autofill Pattern](https://docs.spotflow.io/send-data/#batch-id-autofill-pattern),
    /// you must provide `batch_id`. See [User Guide](https://docs.spotflow.io/send-data/) for
    /// more details.
    pub fn send_message(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        message_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        self.connection
            .send_message(message_context, batch_id, message_id, payload)
    }

    /// Send a [Message](https://docs.spotflow.io/send-data/#message) to
    /// the Platform.
    ///
    /// **Warning:** This method blocks the current thread until the Message (and all the Messages enqueued before it)
    /// is sent to the Platform. If your Device doesn't have a stable Internet connection, consider using `spotflow_client_enqueue_message_advanced` instead.
    ///
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) doesn't have a
    /// [Message ID Autofill Pattern](https://docs.spotflow.io/send-data/#message-id-autofill-pattern),
    /// you must provide `message_id`.
    /// If the [Stream](https://docs.spotflow.io/send-data/#stream) groups
    /// [Messages](https://docs.spotflow.io/send-data/#message) into
    /// [Batches](https://docs.spotflow.io/send-data/#batch) and doesn't have a
    /// [Batch ID Autofill Pattern](https://docs.spotflow.io/send-data/#batch-id-autofill-pattern),
    /// you must provide `batch_id`. See [User Guide](https://docs.spotflow.io/send-data/) for
    /// more details.
    /// Optionally, you can provide also `batch_slice_id` to use Batch Slices and `chunk_id` to use Message Chunking.
    pub fn send_message_advanced(
        &self,
        message_context: &MessageContext,
        batch_id: Option<String>,
        batch_slice_id: Option<String>,
        message_id: Option<String>,
        chunk_id: Option<String>,
        payload: Vec<u8>,
    ) -> Result<()> {
        self.connection.send_message_advanced(
            message_context,
            batch_id,
            batch_slice_id,
            message_id,
            chunk_id,
            payload,
        )
    }

    /// Get the number of [Messages](https://docs.spotflow.io/send-data/#message) that
    /// have been persisted in the local database file but haven't been sent to the Platform yet.
    pub fn pending_messages_count(&self) -> Result<usize> {
        self.connection.pending_messages_count()
    }

    /// Block the current thread until all the [Messages](https://docs.spotflow.io/send-data/#message) that
    /// have been previously enqueued are sent to the Platform.
    pub fn wait_enqueued_messages_sent(&self) -> Result<()> {
        self.connection.wait_enqueued_messages_sent()
    }

    /// Get the current [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties).
    ///
    /// Only the latest version is returned, any versions between the last obtained one and the current one are skipped.
    pub fn desired_properties(&self) -> Result<DesiredProperties> {
        self.connection.desired_properties()
    }

    /// Get the current [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties)
    /// if their version is higher than `version`. Otherwise, return `None`.
    ///
    /// Only the latest version is returned, any versions between the last obtained one and the current one are skipped.
    #[must_use]
    pub fn desired_properties_if_newer(&self, version: u64) -> Option<DesiredProperties> {
        self.connection.desired_properties_if_newer(version)
    }

    /// Enqueue an update of the [Reported Properties](https://docs.spotflow.io/configure-devices/#reported-properties)
    /// to be sent to the Platform.
    ///
    /// This method saves these Reported Properties persistently in the local database file.
    /// The update will be sent asynchronously when possible.
    /// The update may be sent later depending on Internet connectivity and other factors.
    /// To be sure that it has been sent to the Platform, call [`DeviceClient::any_pending_reported_properties_updates`].
    pub fn update_reported_properties(&self, properties: &str) -> Result<()> {
        self.connection.update_reported_properties(properties)
    }

    /// Get whether are there any updates to [Reported Properties](https://docs.spotflow.io/configure-devices/#reported-properties)
    /// that are yet to be sent to the Platform.
    pub fn any_pending_reported_properties_updates(&self) -> Result<bool> {
        self.connection.any_pending_reported_properties_updates()
    }

    /// **Warning**: Don't use, the interface for Cloud-to-Device Messages hasn't been finalized yet.
    #[deprecated]
    #[doc(hidden)]
    pub fn process_c2d<G>(&self, callback: G) -> Result<()>
    where
        G: Fn(&CloudToDeviceMessage) + Send + 'static,
    {
        self.connection.process_c2d(callback)
    }

    /// **Warning**: Don't use, the interface for Cloud-to-Device Messages hasn't been finalized yet.
    #[deprecated]
    #[doc(hidden)]
    pub fn pending_c2d(&self) -> Result<usize> {
        self.connection.pending_c2d()
    }

    /// **Warning**: Don't use, the interface for Cloud-to-Device Messages hasn't been finalized yet.
    #[deprecated]
    #[doc(hidden)]
    pub fn get_c2d(&self, timeout: Duration) -> Result<CloudToDeviceMessageGuard<'_>> {
        self.connection.get_c2d(timeout)
    }

    /// **Warning**: Deprecated, don't use.
    #[deprecated]
    #[doc(hidden)]
    pub fn wait_desired_properties_changed(&self) -> Result<DesiredProperties> {
        self.connection.wait_desired_properties_changed()
    }

    /// **Warning**: Deprecated, don't use.
    #[deprecated]
    #[doc(hidden)]
    #[must_use]
    pub fn reported_properties(&self) -> Option<String> {
        self.connection.reported_properties()
    }

    /// **Warning**: Deprecated, don't use.
    #[deprecated]
    #[doc(hidden)]
    pub fn patch_reported_properties(&self, patch: &str) -> Result<()> {
        self.connection.patch_reported_properties(patch)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn check_if_send<T: Send>() {}
    fn check_if_sync<T: Sync>() {}

    #[test]
    fn traits() {
        check_if_sync::<DeviceClient>();
        check_if_send::<DeviceClient>();
    }
}
