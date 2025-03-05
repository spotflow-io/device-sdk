use std::ffi::CString;
use std::panic::AssertUnwindSafe;
use std::ptr::null_mut;

use anyhow::{anyhow, bail, Result};
use libc::{c_char, c_void, size_t};
use spotflow::{
    DesiredProperties, DeviceClient, DeviceClientBuilder, ProvisioningOperationDisplayHandler,
};

use crate::dps::{DisplayProvisioningOperationCallback, ProvisioningOperation};
use crate::error::{update_last_error, CResult};
use crate::marshall::Marshall;
use crate::{
    buffer_to_slice, call_safe_with_result, call_safe_with_unit_result, drop_ptr, ensure_logging,
    obj_to_ptr, ptr_to_mut, ptr_to_ref, ptr_to_str, ptr_to_str_option, store_to_ptr,
};

use self::twins::DesiredPropertiesUpdatedCallback;

mod c2d;
mod twins;

/// The compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
#[repr(C)]
pub enum Compression {
    /// Do not compress the message.
    SpotflowCompressionNone = 0,
    /// Compress the message using the fastest compression algorithm settings.
    SpotflowCompressionFastest,
    /// Compress the message using the algorithm settings that produce the smallest size.
    /// Beware that this may be significantly slower than the fastest compression.
    /// We recommend to test the performance of your application with this setting before using it in production.
    SpotflowCompressionSmallestSize,
}

impl Compression {
    fn to_ingress_compression_option(&self) -> Option<spotflow::Compression> {
        match self {
            Compression::SpotflowCompressionNone => None,
            Compression::SpotflowCompressionFastest => Some(spotflow::Compression::Fastest),
            Compression::SpotflowCompressionSmallestSize => {
                Some(spotflow::Compression::SmallestSize)
            }
        }
    }
}

/// A set of options for sending [Messages](https://docs.spotflow.io/send-data/#message) to
/// a [Stream](https://docs.spotflow.io/send-data/#stream). This object is
/// managed by the Device SDK. Create its instance using @ref spotflow_message_context_create and delete it using
/// @ref spotflow_message_context_destroy.
pub struct MessageContext {
    inner: spotflow::MessageContext,
}

/// A set of options that specify how to connect to the Platform. This object is managed by the Device SDK.
/// Create its instance using @ref spotflow_client_options_create and delete it using @ref spotflow_client_options_destroy.
/// After you configure all the options, pass the address of @ref spotflow_client_options_t to
/// @ref spotflow_client_start.
pub struct ClientOptions {
    device_id: Option<String>,
    provisioning_token: String,
    database_file: String,
    site_id: Option<String>,
    instance: Option<String>,
    display_provisioning_operation_callback: DisplayProvisioningOperationCallback,
    display_provisioning_operation_context: *mut c_void,
    desired_properties_updated_callback: DesiredPropertiesUpdatedCallback,
    desired_properties_updated_context: *mut c_void,
    remote_access_allowed: bool,
}

struct DisplayProvisioningOperationCallbackHolder {
    // This definition must be kept in sync with `DisplayProvisioningOperationCallback` until
    // https://github.com/mozilla/cbindgen/issues/326 is fixed (we'll be able to remove the `Option` then)
    callback: extern "C" fn(operation: *const ProvisioningOperation, context: *mut c_void),
    context: *mut c_void,
}

impl ProvisioningOperationDisplayHandler for DisplayProvisioningOperationCallbackHolder {
    fn display(
        &self,
        provisioning_operation: &spotflow::ProvisioningOperation,
    ) -> anyhow::Result<()> {
        let provisioning_operation =
            ProvisioningOperation::marshall(provisioning_operation.clone());
        (self.callback)(&provisioning_operation, self.context);

        Ok(())
    }
}

struct DesiredPropertiesUpdatedCallbackHolder {
    // This definition must be kept in sync with `DesiredPropertiesUpdatedCallback` until
    // https://github.com/mozilla/cbindgen/issues/326 is fixed (we'll be able to remove the `Option` then)
    callback: extern "C" fn(desired_properties: *const c_char, version: u64, context: *mut c_void),
    context: *mut c_void,
}

// It's the responsibility of the caller to synchronize access to the context
unsafe impl Send for DesiredPropertiesUpdatedCallbackHolder {}
unsafe impl Sync for DesiredPropertiesUpdatedCallbackHolder {}

impl spotflow::DesiredPropertiesUpdatedCallback for DesiredPropertiesUpdatedCallbackHolder {
    fn properties_updated(&self, properties: DesiredProperties) -> Result<()> {
        let values = CString::new(properties.values)?;
        (self.callback)(values.as_ptr(), properties.version, self.context);

        Ok(())
    }
}

/// Create an object that stores the connection options. The created object of type @ref spotflow_client_options_t
/// is managed by the Device SDK. After you configure all the options (using either this function or with the additional
/// functions listed below), pass the address of @ref spotflow_client_options_t to @ref spotflow_client_start.
/// Delete @ref spotflow_client_options_t using @ref spotflow_client_options_destroy.
///
/// @see spotflow_client_options_set_database_file
///      spotflow_client_options_set_provisioning_token
///      spotflow_client_options_set_device_id
///      spotflow_client_options_set_instance
///      spotflow_client_options_set_display_provisioning_operation_callback
///      spotflow_client_options_set_remote_access_allowed_for_all_ports
///
/// @param options (Output) The pointer to the @ref spotflow_client_options_t object that will be created by this function.
/// @param device_id (Optional) The [ID of the Device](https://docs.spotflow.io/connect-devices/#device-id) you
///                  are running the code from. Use `NULL` if you don't want to specify it. See @ref spotflow_client_options_set_device_id.
/// @param provisioning_token The [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token)
///                           that will @ref spotflow_client_start use to start [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning).
///                           See @ref spotflow_client_options_set_provisioning_token.
/// @param database_file The path to the local database file where the Device SDK stores the connection credentials and
///                     temporarily persists incoming and outgoing messages. See @ref spotflow_client_options_set_database_file.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid (or `NULL` if it's required).
#[no_mangle]
pub extern "C" fn spotflow_client_options_create(
    options: *mut *mut ClientOptions,
    device_id: *const c_char,
    provisioning_token: *const c_char,
    database_file: *const c_char,
) -> CResult {
    let result = call_safe_with_result(|| {
        ensure_logging();

        let device_id = unsafe { ptr_to_str_option(device_id) }?.map(|s| s.to_string());
        let provisioning_token = unsafe { ptr_to_str(provisioning_token) }?.to_string();
        let database_file = unsafe { ptr_to_str(database_file) }?.to_string();

        let options = ClientOptions {
            device_id,
            provisioning_token,
            database_file,
            site_id: None,
            instance: None,
            display_provisioning_operation_callback: None,
            display_provisioning_operation_context: null_mut(),
            desired_properties_updated_callback: None,
            desired_properties_updated_context: null_mut(),
            remote_access_allowed: false,
        };

        Ok(options)
    });

    match result {
        Err(err) => err,
        Ok(result_options) => {
            let options_ptr = obj_to_ptr(result_options);
            unsafe { store_to_ptr(options, options_ptr) }
        }
    }
}

/// Set the path to the local database file where the Device SDK stores the connection credentials and temporarily persists
/// incoming and outgoing messages. @ref spotflow_client_start creates the file if it doesn't exist.
///
/// The file must end with the suffix `".db"`, for example, `"spotflow.db"`.
/// If you don't use an absolute path, the file is created relative to the current working directory.
///
/// @param options The @ref spotflow_client_options_t object.
/// @param database_file The path to the local database file.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_database_file(
    options: *mut ClientOptions,
    database_file: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.database_file = unsafe { ptr_to_str(database_file) }?.to_string();
        Ok(())
    })
}

/// Set the [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token) that
/// will @ref spotflow_client_start use to start [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning).
///
/// See [Get Started](https://docs.spotflow.io/get-started) for instructions
/// [how to create a Provisioning Token](https://docs.spotflow.io/get-started#3-create-provisioning-token).
///
/// @param options The @ref spotflow_client_options_t object.
/// @param provisioning_token The [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token).
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_provisioning_token(
    options: *mut ClientOptions,
    provisioning_token: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.provisioning_token = unsafe { ptr_to_str(provisioning_token) }?.to_string();
        Ok(())
    })
}

/// Set the ID of the [Site](https://docs.spotflow.io/connect-devices/#site) the
/// [Device](https://docs.spotflow.io/connect-devices/#device) is located at.
///
/// @param options The @ref spotflow_client_options_t object.
/// @param site_id (Optional) The [ID of the Site](https://docs.spotflow.io/connect-devices/#site).
///                Use `NULL` if you don't want to specify it.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
/*pub*/
unsafe extern "C" fn spotflow_client_options_set_site_id(
    options: *mut ClientOptions,
    site_id: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.site_id = unsafe { ptr_to_str_option(site_id) }?.map(|s| s.to_string());
        Ok(())
    })
}

/// Set the unique [Device ID](https://docs.spotflow.io/connect-devices/#device-id) you
/// are running the code from. If you don't specify it here, you'll need to either store it in the
/// [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token),
/// or choose it during the approval of the
/// [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
///
/// Make sure that no two [Devices](https://docs.spotflow.io/connect-devices/#device) in
/// the same [Workspace](https://docs.spotflow.io/manage-access/workspaces/) use the same ID.
/// Otherwise, unexpected errors can occur during the communication with the Platform.
///
/// @param options The @ref spotflow_client_options_t object.
/// @param device_id (Optional) The [ID of the Device](https://docs.spotflow.io/connect-devices/#device-id).
///                  Use `NULL` if you don't want to specify it.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_device_id(
    options: *mut ClientOptions,
    device_id: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.device_id = unsafe { ptr_to_str_option(device_id) }?.map(|s| s.to_string());
        Ok(())
    })
}

/// Set the URI/hostname of the Platform instance where the
/// [Device](https://docs.spotflow.io/connect-devices/#device) will connect to.
///
/// If your company uses a dedicated instance of the Platform, such as `acme.spotflow.io`, specify it here.
/// The default value is `api.eu1.spotflow.io`.
///
/// @param options The @ref spotflow_client_options_t object.
/// @param instance (Optional) The domain of the Platform instance. Use `NULL` if you want to keep the default value.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_instance(
    options: *mut ClientOptions,
    instance: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.instance = unsafe { ptr_to_str_option(instance) }?.map(|s| s.to_string());
        Ok(())
    })
}

/// Set the function that displays the details of the
/// [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation) when
/// @ref spotflow_client_start is performing [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning).
/// See [Get Started](https://docs.spotflow.io/get-started#3-create-provisioning-token) for hands-on experience with this process.
///
/// @param options The @ref spotflow_client_options_t object.
/// @param callback (Optional) The function that displays the details of the
///                 [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
///                 Use `NULL` if you don't want to specify it.
/// @param context (Optional) The context that will be passed to `callback`. Use `NULL`
///                if you don't want to specify it.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_display_provisioning_operation_callback(
    options: *mut ClientOptions,
    callback: DisplayProvisioningOperationCallback,
    context: *mut c_void,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.display_provisioning_operation_callback = callback;
        options.display_provisioning_operation_context = context;

        Ok(())
    })
}

/// Set the function that is called right after @ref spotflow_client_start with the current version of the
/// [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties) and then whenever the
/// [Device](https://docs.spotflow.io/connect-devices/#device) receives their update from the Platform.
/// The [Device configuration tutorial](https://docs.spotflow.io/configure-devices/tutorial-configure-device#1-start-device)
/// shows how to use this option. The function is called in a separate thread, so make sure that you properly synchronize
/// access to your shared resources. The functions working with @ref spotflow_client_t are thread-safe
/// (apart from @ref spotflow_client_destroy), so it's safe to call them in the function.
///
/// @param options The @ref spotflow_client_options_t object.
/// @param callback (Optional) The function that is called with each update of the
///                 [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties).
///                 Use `NULL` if you don't want to specify it.
/// @param context (Optional) The context that will be passed to `callback`. It will be used from a different thread,
///                so make sure that it's properly synchronized. Use `NULL` if you don't want to specify it.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_desired_properties_updated_callback(
    options: *mut ClientOptions,
    callback: DesiredPropertiesUpdatedCallback,
    context: *mut c_void,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.desired_properties_updated_callback = callback;
        options.desired_properties_updated_context = context;

        Ok(())
    })
}

/// Allow the Device to accept remote access requests for all ports.
///
/// @param options The @ref spotflow_client_options_t object.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_set_remote_access_allowed_for_all_ports(
    options: *mut ClientOptions,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_mut(options) }?;
        options.remote_access_allowed = true;

        Ok(())
    })
}

/// Destroy the @ref spotflow_client_options_t object.
///
/// @param options The @ref spotflow_client_options_t object to destroy.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_options_destroy(options: *mut ClientOptions) {
    drop_ptr(options);
}

/// Start communicating with the Platform. The created object of type @ref spotflow_client_t is managed by
/// the Device SDK. Delete it using @ref spotflow_client_destroy.
///
/// If the [Device](https://docs.spotflow.io/connect-devices/#device) is
/// not yet registered in the Platform, or its
/// [Registration Token](https://docs.spotflow.io/connect-devices/#registration-token) is
/// expired, this method performs [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning)
/// and waits for the approval.
/// [Get Started](https://docs.spotflow.io/get-started) shows this process in practice.
///
/// If the [Registration Token](https://docs.spotflow.io/connect-devices/#registration-token) from
/// the last run is still valid, this method succeeds even without the connection to the Internet. The Device SDK will
/// store all outgoing communication in the local database file and send it once it connects to the Platform.
///
/// @param client (Output) The pointer to the @ref spotflow_client_t object that will be created by this function.
/// @param options The options that specify how to connect to the Platform.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub extern "C" fn spotflow_client_start(
    client: *mut *mut DeviceClient,
    options: *const ClientOptions,
) -> CResult {
    let result = call_safe_with_result(|| {
        ensure_logging();

        let options = unsafe { ptr_to_ref(options) }?;

        let mut builder = DeviceClientBuilder::new(
            options.device_id.clone(),
            options.provisioning_token.clone(),
            options.database_file.clone(),
        );

        if let Some(site_id) = &options.site_id {
            builder = builder.with_site_id(site_id.clone());
        };

        if let Some(instance) = &options.instance {
            builder = builder.with_instance(instance.clone());
        };

        if let Some(callback) = options.display_provisioning_operation_callback {
            let callback = DisplayProvisioningOperationCallbackHolder {
                callback,
                context: options.display_provisioning_operation_context,
            };

            let callback = Box::new(callback) as Box<dyn ProvisioningOperationDisplayHandler>;

            builder = builder.with_display_provisioning_operation_callback(callback);
        }

        if let Some(callback) = options.desired_properties_updated_callback {
            let callback = DesiredPropertiesUpdatedCallbackHolder {
                callback,
                context: options.desired_properties_updated_context,
            };

            let callback =
                Box::new(callback) as Box<dyn spotflow::DesiredPropertiesUpdatedCallback>;

            builder = builder.with_desired_properties_updated_callback(callback);
        }

        if options.remote_access_allowed {
            builder = builder.with_remote_access_allowed_for_all_ports();
        }

        builder.build()
    });

    match result {
        Err(err) => err,
        Ok(new_client) => {
            let new_client_ptr = obj_to_ptr(new_client);
            unsafe { store_to_ptr(client, new_client_ptr) }
        }
    }
}

/// Create an object that stores the options for sending [Messages](https://docs.spotflow.io/send-data/#message) to
/// the Platform.
///
/// Initialize the @ref spotflow_message_context_t object: set the most important fields to the provided values and
/// the other fields to default values. The created object of type @ref spotflow_message_context_t is managed by the
/// Device SDK. You can configure the object either using this function or using the additional functions listed below.
/// Delete it using @ref spotflow_message_context_destroy.
///
/// @see spotflow_message_context_set_stream_group
///      spotflow_message_context_set_stream
///      spotflow_message_context_set_compression
///
/// @param message_context (Output) The pointer to the @ref spotflow_message_context_t object that will be created by this function.
/// @param stream_group (Optional) The [Stream Group](https://docs.spotflow.io/send-data/#stream-group)
///                     the [Message](https://docs.spotflow.io/send-data/#message) will be sent to.
///                     If `NULL`, the Platforms directs the Messages to the default Stream Group of the current
///                     [Workspace](https://docs.spotflow.io/manage-access/workspaces/).
/// @param stream (Optional) The [Stream](https://docs.spotflow.io/send-data/#stream)
///               the [Message](https://docs.spotflow.io/send-data/#message) will be sent to.
///               If `NULL`, the Platform directs the Messages into the default Stream of the given Stream Group.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub extern "C" fn spotflow_message_context_create(
    message_context: *mut *mut MessageContext,
    stream_group: *const c_char,
    stream: *const c_char,
) -> CResult {
    let result = call_safe_with_result(|| {
        ensure_logging();

        let stream_group = unsafe { ptr_to_str_option(stream_group) }?.map(|s| s.to_string());
        let stream = unsafe { ptr_to_str_option(stream) }?.map(|s| s.to_string());

        let message_context = MessageContext {
            inner: spotflow::MessageContext::new(stream_group, stream),
        };

        Ok(message_context)
    });

    match result {
        Err(err) => err,
        Ok(result_message_context) => {
            let message_context_ptr = obj_to_ptr(result_message_context);
            unsafe { store_to_ptr(message_context, message_context_ptr) }
        }
    }
}

/// Set the [Stream Group](https://docs.spotflow.io/send-data/#stream-group) where
/// [Messages](https://docs.spotflow.io/send-data/#message) will be sent to.
///
/// @param message_context The @ref spotflow_message_context_t object.
/// @param stream_group The [Stream Group](https://docs.spotflow.io/send-data/#stream-group).
///                     If `NULL`, the Platforms directs the Messages to the default Stream Group of the current
///                     [Workspace](https://docs.spotflow.io/manage-access/workspaces/).
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_message_context_set_stream_group(
    message_context: *mut MessageContext,
    stream_group: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let message_context = unsafe { ptr_to_mut(message_context) }?;
        let stream_group = unsafe { ptr_to_str_option(stream_group) }?.map(|s| s.to_string());

        message_context.inner.set_stream_group(stream_group);

        Ok(())
    })
}

/// Set the [Stream](https://docs.spotflow.io/send-data/#stream) where
/// [Messages](https://docs.spotflow.io/send-data/#message) will be sent to.
///
/// @param message_context The @ref spotflow_message_context_t object.
/// @param stream The [Stream](https://docs.spotflow.io/send-data/#stream).
///               If `NULL`, the Platform directs the Messages into the default Stream of the given Stream Group.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_message_context_set_stream(
    message_context: *mut MessageContext,
    stream: *const c_char,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let message_context = unsafe { ptr_to_mut(message_context) }?;
        let stream = unsafe { ptr_to_str_option(stream) }?.map(|s| s.to_string());

        message_context.inner.set_stream(stream);

        Ok(())
    })
}

/// Set the compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
///
/// @param message_context The @ref spotflow_message_context_t object.
/// @param compression The compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_message_context_set_compression(
    message_context: *mut MessageContext,
    compression: Compression,
) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let message_context = unsafe { ptr_to_mut(message_context) }?;
        let compression = compression.to_ingress_compression_option();

        message_context.inner.set_compression(compression);

        Ok(())
    })
}

/// Destroy the @ref spotflow_message_context_t object.
///
/// @param message_context The @ref spotflow_message_context_t object to destroy.
#[no_mangle]
pub unsafe extern "C" fn spotflow_message_context_destroy(message_context: *mut MessageContext) {
    drop_ptr(message_context);
}

/// Enqueue a [Message](https://docs.spotflow.io/send-data/#message) to
/// be sent to the Platform.
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
///
/// The method returns right after it saves the [Message](https://docs.spotflow.io/send-data/#message) to
/// the queue in the local database file. A background thread asynchronously sends the messages from the queue to the Platform.
/// You can check the number of pending messages in the queue using @ref spotflow_client_get_pending_messages_count.
///
/// @param client The @ref spotflow_client_t object.
/// @param message_context The options that specify how to send the [Message](https://docs.spotflow.io/send-data/#message).
/// @param batch_id (Optional) The ID of the [Batch](https://docs.spotflow.io/send-data/#batch) the
///                 [Message](https://docs.spotflow.io/send-data/#message) is a part of.
///                 Use `NULL` if you don't want to specify it.
/// @param message_id (Optional) The ID of the [Message](https://docs.spotflow.io/send-data/#message).
///                   Use `NULL` if you don't want to specify it.
/// @param buffer The buffer that contains the [Message](https://docs.spotflow.io/send-data/#message).
/// @param length The length of the buffer in bytes.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              persisting the message.
#[no_mangle]
pub extern "C" fn spotflow_client_enqueue_message(
    client: *mut DeviceClient,
    message_context: *const MessageContext,
    batch_id: *const c_char,
    message_id: *const c_char,
    buffer: *const u8,
    length: size_t,
) -> CResult {
    {
        let client = AssertUnwindSafe(client);

        call_safe_with_unit_result(|| {
            ensure_logging();

            let client = unsafe { ptr_to_ref(*client) }?;
            let message_context = unsafe { ptr_to_ref(message_context) }?;
            let batch_id = unsafe { ptr_to_str_option(batch_id) }?.map(str::to_owned);
            let message_id = unsafe { ptr_to_str_option(message_id) }?.map(str::to_owned);
            let payload = unsafe { buffer_to_slice(buffer, length)?.to_vec() };

            client.enqueue_message(&message_context.inner, batch_id, message_id, payload)
        })
    }
}

/// Enqueue a [Message](https://docs.spotflow.io/send-data/#message) to
/// be sent to the Platform.
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
///
/// The method returns right after it saves the [Message](https://docs.spotflow.io/send-data/#message) to
/// the queue in the local database file. A background thread asynchronously sends the messages from the queue to the Platform.
/// You can check the number of pending messages in the queue using @ref spotflow_client_get_pending_messages_count.
///
/// @param client The @ref spotflow_client_t object.
/// @param message_context The options that specify how to send the [Message](https://docs.spotflow.io/send-data/#message).
/// @param batch_id (Optional) The ID of the [Batch](https://docs.spotflow.io/send-data/#batch) the
///                 [Message](https://docs.spotflow.io/send-data/#message) is a part of.
///                 Use `NULL` if you don't want to specify it.
/// @param batch_slice_id (Optional) The ID of the Batch Slice the [Message](https://docs.spotflow.io/send-data/#message) is
///                       a part of. Use `NULL` if you dont' want to specify it.
/// @param message_id (Optional) The ID of the [Message](https://docs.spotflow.io/send-data/#message).
///                   Use `NULL` if you don't want to specify it.
/// @param chunk_id (Optional) The ID of the Chunk of the [Message](https://docs.spotflow.io/send-data/#message).
///                 Use `NULL` if you don't want to specify it.
/// @param buffer The buffer that contains the [Message](https://docs.spotflow.io/send-data/#message).
/// @param length The length of the buffer in bytes.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              persisting the message.
#[no_mangle]
pub extern "C" fn spotflow_client_enqueue_message_advanced(
    client: *mut DeviceClient,
    message_context: *const MessageContext,
    batch_id: *const c_char,
    batch_slice_id: *const c_char,
    message_id: *const c_char,
    chunk_id: *const c_char,
    buffer: *const u8,
    length: size_t,
) -> CResult {
    {
        let client = AssertUnwindSafe(client);

        call_safe_with_unit_result(|| {
            ensure_logging();

            let client = unsafe { ptr_to_ref(*client) }?;
            let message_context = unsafe { ptr_to_ref(message_context) }?;
            let batch_id = unsafe { ptr_to_str_option(batch_id) }?.map(str::to_owned);
            let batch_slice_id = unsafe { ptr_to_str_option(batch_slice_id) }?.map(str::to_owned);
            let message_id = unsafe { ptr_to_str_option(message_id) }?.map(str::to_owned);
            let chunk_id = unsafe { ptr_to_str_option(chunk_id) }?.map(str::to_owned);
            let payload = unsafe { buffer_to_slice(buffer, length)?.to_vec() };

            client.enqueue_message_advanced(
                &message_context.inner,
                batch_id,
                batch_slice_id,
                message_id,
                chunk_id,
                payload,
            )
        })
    }
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
/// You can check the number of pending messages in the queue using @ref spotflow_client_get_pending_messages_count.
///
/// @param client The @ref spotflow_client_t object.
/// @param message_context The options that specify how to send the batch-completing [Message](https://docs.spotflow.io/send-data/#message).
/// @param batch_id The ID of the [Batch](https://docs.spotflow.io/send-data/#batch) that will be completed.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              persisting the message.
#[no_mangle]
pub extern "C" fn spotflow_client_enqueue_batch_completion(
    client: *mut DeviceClient,
    message_context: *const MessageContext,
    batch_id: *const c_char,
) -> CResult {
    let client = AssertUnwindSafe(client);

    call_safe_with_unit_result(|| {
        ensure_logging();

        let client = unsafe { ptr_to_ref(*client) }?;
        let message_context = unsafe { ptr_to_ref(message_context) }?;

        let batch_id = if batch_id.is_null() {
            bail!("Batch ID must be provided when completing a batch.");
        } else {
            unsafe { ptr_to_str(batch_id) }?.to_owned()
        };

        client.enqueue_batch_completion(&message_context.inner, batch_id)
    })
}

/// Enqueue the manual completion of the current [Message](https://docs.spotflow.io/send-data/#message) to
/// be sent to the Platform. Use this methods when Message Chunking is used.
///
/// The method returns right after it saves the message-completing Message to the queue in the local database file.
/// A background thread asynchronously sends the Messages from the queue to the Platform.
/// You can check the number of pending messages in the queue using @ref spotflow_client_get_pending_messages_count.
///
/// @param client The @ref spotflow_client_t object.
/// @param message_context The options that specify how to send the message-completing [Message](https://docs.spotflow.io/send-data/#message).
/// @param batch_id The ID of the [Batch](https://docs.spotflow.io/send-data/#batch) that
///                 the [Message](https://docs.spotflow.io/send-data/#message) belongs to.
/// @param message_id The ID of the [Message](https://docs.spotflow.io/send-data/#message).
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              persisting the message.
#[no_mangle]
pub extern "C" fn spotflow_client_enqueue_message_completion(
    client: *mut DeviceClient,
    message_context: *const MessageContext,
    batch_id: *const c_char,
    message_id: *const c_char,
) -> CResult {
    let client = AssertUnwindSafe(client);

    call_safe_with_unit_result(|| {
        ensure_logging();

        let client = unsafe { ptr_to_ref(*client) }?;
        let message_context = unsafe { ptr_to_ref(message_context) }?;

        let batch_id = if batch_id.is_null() {
            bail!("Batch ID must be provided when completing a Message.");
        } else {
            unsafe { ptr_to_str(batch_id) }?.to_owned()
        };

        let message_id = if message_id.is_null() {
            bail!("Message ID must be provided when completing a Message.");
        } else {
            unsafe { ptr_to_str(message_id) }?.to_owned()
        };

        client.enqueue_message_completion(&message_context.inner, batch_id, message_id)
    })
}

/// Block the current thread until all the [Messages](https://docs.spotflow.io/send-data/#message) that
/// have been previously enqueued are sent to the Platform.
///
/// @param client The @ref spotflow_client_t object.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if the argument is invalid.
#[no_mangle]
pub extern "C" fn spotflow_client_wait_enqueued_messages_sent(
    client: *mut DeviceClient,
) -> CResult {
    {
        let client = AssertUnwindSafe(client);

        call_safe_with_unit_result(|| {
            ensure_logging();

            let client = unsafe { ptr_to_ref(*client) }?;

            client.wait_enqueued_messages_sent()
        })
    }
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
///
/// @param client The @ref spotflow_client_t object.
/// @param message_context The options that specify how to send the [Message](https://docs.spotflow.io/send-data/#message).
/// @param batch_id (Optional) The ID of the [Batch](https://docs.spotflow.io/send-data/#batch) the
///                 [Message](https://docs.spotflow.io/send-data/#message) is a part of.
///                 Use `NULL` if you don't want to specify it.
/// @param message_id (Optional) The ID of the [Message](https://docs.spotflow.io/send-data/#message).
///                   Use `NULL` if you don't want to specify it.
/// @param buffer The buffer that contains the [Message](https://docs.spotflow.io/send-data/#message).
/// @param length The length of the buffer in bytes.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              persisting the message.
#[no_mangle]
pub extern "C" fn spotflow_client_send_message(
    client: *mut DeviceClient,
    message_context: *const MessageContext,
    batch_id: *const c_char,
    message_id: *const c_char,
    buffer: *const u8,
    length: size_t,
) -> CResult {
    {
        let client = AssertUnwindSafe(client);

        call_safe_with_unit_result(|| {
            ensure_logging();

            let client = unsafe { ptr_to_ref(*client) }?;
            let message_context = unsafe { ptr_to_ref(message_context) }?;
            let batch_id = unsafe { ptr_to_str_option(batch_id) }?.map(str::to_owned);
            let message_id = unsafe { ptr_to_str_option(message_id) }?.map(str::to_owned);
            let payload = unsafe { buffer_to_slice(buffer, length)?.to_vec() };

            client.send_message(&message_context.inner, batch_id, message_id, payload)
        })
    }
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
///
/// @param client The @ref spotflow_client_t object.
/// @param message_context The options that specify how to send the [Message](https://docs.spotflow.io/send-data/#message).
/// @param batch_id (Optional) The ID of the [Batch](https://docs.spotflow.io/send-data/#batch) the
///                 [Message](https://docs.spotflow.io/send-data/#message) is a part of.
///                 Use `NULL` if you don't want to specify it.
/// @param batch_slice_id (Optional) The ID of the Batch Slice the [Message](https://docs.spotflow.io/send-data/#message) is
///                       a part of. Use `NULL` if you dont' want to specify it.
/// @param message_id (Optional) The ID of the [Message](https://docs.spotflow.io/send-data/#message).
///                   Use `NULL` if you don't want to specify it.
/// @param chunk_id (Optional) The ID of the Chunk of the [Message](https://docs.spotflow.io/send-data/#message).
///                 Use `NULL` if you don't want to specify it.
/// @param buffer The buffer that contains the [Message](https://docs.spotflow.io/send-data/#message).
/// @param length The length of the buffer in bytes.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              persisting the message.
#[no_mangle]
pub extern "C" fn spotflow_client_send_message_advanced(
    client: *mut DeviceClient,
    message_context: *const MessageContext,
    batch_id: *const c_char,
    batch_slice_id: *const c_char,
    message_id: *const c_char,
    chunk_id: *const c_char,
    buffer: *const u8,
    length: size_t,
) -> CResult {
    {
        let client = AssertUnwindSafe(client);

        call_safe_with_unit_result(|| {
            ensure_logging();

            let client = unsafe { ptr_to_ref(*client) }?;
            let message_context = unsafe { ptr_to_ref(message_context) }?;
            let batch_id = unsafe { ptr_to_str_option(batch_id) }?.map(str::to_owned);
            let batch_slice_id = unsafe { ptr_to_str_option(batch_slice_id) }?.map(str::to_owned);
            let message_id = unsafe { ptr_to_str_option(message_id) }?.map(str::to_owned);
            let chunk_id = unsafe { ptr_to_str_option(chunk_id) }?.map(str::to_owned);
            let payload = unsafe { buffer_to_slice(buffer, length)?.to_vec() };

            client.send_message_advanced(
                &message_context.inner,
                batch_id,
                batch_slice_id,
                message_id,
                chunk_id,
                payload,
            )
        })
    }
}

/// Get the number of [Messages](https://docs.spotflow.io/send-data/#message) that
/// have been persisted in the local database file but haven't been sent to the Platform yet.
///
/// @param client The @ref spotflow_client_t object.
/// @param count (Output) The number of pending [Messages](https://docs.spotflow.io/send-data/#message).
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid or there is an error in
///              accessing the local database file.
#[no_mangle]
pub extern "C" fn spotflow_client_get_pending_messages_count(
    client: *const DeviceClient,
    count: *mut size_t,
) -> CResult {
    let client = AssertUnwindSafe(client);

    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = unsafe { ptr_to_ref(*client) }?;
        client.pending_messages_count()
    });

    match result {
        Err(e) => e,
        Ok(pending) => unsafe { store_to_ptr(count, pending) },
    }
}

/// Write the ID of the [Workspace](https://docs.spotflow.io/manage-access/workspaces/) to which the
/// [Device](https://docs.spotflow.io/connect-devices/#device) belongs into the provided buffer.
///
/// @param client The @ref spotflow_client_t object.
/// @param buffer The buffer where the [Workspace ID](https://docs.spotflow.io/manage-access/workspaces/) string
///               including the trailing NUL character will be written to.
/// @param buffer_length The length of the buffer in bytes. Use @ref SPOTFLOW_WORKSPACE_ID_MAX_LENGTH to be sure that it is
///                      always large enough.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_INSUFFICIENT_BUFFER if the buffer is too small,
///         @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub extern "C" fn spotflow_client_get_workspace_id(
    client: *const DeviceClient,
    buffer: *mut c_char,
    buffer_length: size_t,
) -> CResult {
    let client = AssertUnwindSafe(client);

    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = unsafe { ptr_to_ref(*client) }?;
        client.workspace_id()
    });
    match result {
        Err(e) => e,
        Ok(workspace_id) => {
            let id_length = workspace_id.len();
            if buffer_length <= id_length {
                update_last_error(anyhow!(
                    "The buffer for Workspace ID needs to be at least {} bytes long.",
                    id_length + 1
                ));
                return CResult::SpotflowInsufficientBuffer;
            }

            unsafe {
                std::ptr::copy_nonoverlapping(workspace_id.as_ptr(), buffer as *mut u8, id_length);
                *buffer.add(id_length) = 0;
            }
            CResult::SpotflowOk
        }
    }
}

/// Write the [Device ID](https://docs.spotflow.io/connect-devices/#device-id) into
/// the provided buffer. Note that the value might differ from the one requested in @ref
/// spotflow_client_options_set_device_id if the technician overrides it during the approval of the
/// [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
///
/// @param client The @ref spotflow_client_t object.
/// @param buffer The buffer where the [Device ID](https://docs.spotflow.io/connect-devices/#device-id) string
///               including the trailing NUL character will be written to.
/// @param buffer_length The length of the buffer in bytes. Use @ref SPOTFLOW_DEVICE_ID_MAX_LENGTH to be sure that it is
///                      always large enough.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_INSUFFICIENT_BUFFER if the buffer is too small,
///         @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub extern "C" fn spotflow_client_get_device_id(
    client: *const DeviceClient,
    buffer: *mut c_char,
    buffer_length: size_t,
) -> CResult {
    let client = AssertUnwindSafe(client);

    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = unsafe { ptr_to_ref(*client) }?;
        client.device_id()
    });
    match result {
        Err(e) => e,
        Ok(device_id) => {
            let id_length = device_id.len();
            if buffer_length <= id_length {
                update_last_error(anyhow!(
                    "The buffer for Device ID needs to be at least {} bytes long.",
                    id_length + 1
                ));
                return CResult::SpotflowInsufficientBuffer;
            }

            unsafe {
                std::ptr::copy_nonoverlapping(device_id.as_ptr(), buffer as *mut u8, id_length);
                *buffer.add(id_length) = 0;
            }
            CResult::SpotflowOk
        }
    }
}

/// Disconnect from the Platform and destroy the @ref spotflow_client_t object.
///
/// @param client The @ref spotflow_client_t object.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_destroy(client: *mut DeviceClient) {
    drop_ptr(client);
}
