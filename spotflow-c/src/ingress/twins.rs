use std::{cmp::min, panic::AssertUnwindSafe};

use anyhow::anyhow;
use libc::{c_char, c_void, size_t};
use spotflow::DeviceClient;

use crate::{
    call_safe_with_result, call_safe_with_unit_result, ensure_logging,
    error::{update_last_error, CResult},
    ptr_to_ref, ptr_to_str, SPOTFLOW_PROPERTIES_VERSION_ANY,
};

/// The callback to be called when the [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties)
/// are updated.
/// The callback is called only if you have configured it by @ref spotflow_client_options_set_desired_properties_updated_callback.
/// The callback is called on a background thread.
///
/// @param desired_properties The new Desired Properties represented as a JSON string encoded in UTF-8.
/// @param version The version of the new Desired Properties.
/// @param context The optional context that was configured by @ref spotflow_client_options_set_desired_properties_updated_callback.
pub type DesiredPropertiesUpdatedCallback =
    Option<extern "C" fn(desired_properties: *const c_char, version: u64, context: *mut c_void)>;

/// Write the current [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties)
/// into the provided buffer and obtain their version. The content is a JSON string encoded in UTF-8.
///
/// Only the latest version is returned, any versions between the last obtained one and the current one will be skipped.
///
/// @param client The @ref spotflow_client_t object.
/// @param buffer The buffer to write the JSON string encoded in UTF-8 into.
/// @param buffer_length The length of the buffer in bytes.
/// @param properties_length (Output) The length of the JSON string in bytes including the trailing null character.
/// @param properties_version (Output) The version of the current properties.
/// @return @ref SPOTFLOW_OK if the properties were written successfully, @ref SPOTFLOW_INSUFFICIENT_BUFFER if the buffer
///         is too small (you can then resize it using `properties_length` and call the function again), @ref SPOTFLOW_ERROR
///         if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_get_desired_properties(
    client: *mut DeviceClient,
    buffer: *mut c_char,
    buffer_length: size_t,
    properties_length: *mut size_t,
    properties_version: *mut u64,
) -> CResult {
    let client = AssertUnwindSafe(client);
    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;
        client.desired_properties()
    });
    match result {
        Err(e) => e,
        Ok(desired_properties) => spotflow_client_get_desired_properties_common(
            desired_properties,
            buffer_length,
            buffer,
            properties_length,
            properties_version,
        ),
    }
}

/// Write the current [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties)
/// into the provided buffer and obtain their version if their version is higher than `version` or if `version` is
/// @ref SPOTFLOW_PROPERTIES_VERSION_ANY. The content is a JSON string encoded in UTF-8.
///
/// Only the latest version is returned, any versions between the last obtained one and the current one will be skipped.
///
/// @param client The @ref spotflow_client_t object.
/// @param version The version of the last received properties. Only the properties with a higher version are retrieved. Use
///                @ref SPOTFLOW_PROPERTIES_VERSION_ANY to always retrieve the current properties.
/// @param buffer The buffer to write the JSON string encoded in UTF-8 into.
/// @param buffer_length The length of the buffer in bytes.
/// @param properties_length (Output) The length of the JSON string in bytes including the trailing null character.
///                          If the current version of the current version of the properties is not larger than `version`, `0` will be stored here.
/// @param properties_version (Output) The version of the current properties. If the current version of the properties is not larger than
///                           `version`, nothing is stored here.
/// @return @ref SPOTFLOW_OK if the properties were written successfully or they were not written at all because their version wasn't higher
///         than `version`, @ref SPOTFLOW_INSUFFICIENT_BUFFER if the buffer is too small (you can then resize it using `properties_length`
///         and call the function again), @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_get_desired_properties_if_newer(
    client: *mut DeviceClient,
    version: u64,
    buffer: *mut c_char,
    buffer_length: size_t,
    properties_length: *mut size_t,
    properties_version: *mut u64,
) -> CResult {
    let client = AssertUnwindSafe(client);
    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;

        if version == SPOTFLOW_PROPERTIES_VERSION_ANY {
            Ok(Some(client.desired_properties()?))
        } else {
            Ok(client.desired_properties_if_newer(version))
        }
    });
    match result {
        Err(e) => e,
        Ok(None) => {
            if !properties_length.is_null() {
                *properties_length = 0;
            }

            CResult::SpotflowOk
        }
        Ok(Some(desired_properties)) => spotflow_client_get_desired_properties_common(
            desired_properties,
            buffer_length,
            buffer,
            properties_length,
            properties_version,
        ),
    }
}

unsafe fn spotflow_client_get_desired_properties_common(
    desired_properties: spotflow::DesiredProperties,
    buffer_length: size_t,
    buffer: *mut c_char,
    properties_length: *mut size_t,
    properties_version: *mut u64,
) -> CResult {
    let properties = desired_properties.values;

    let success = if buffer_length == 0 {
        false
    } else {
        let copy_length = min(buffer_length - 1, properties.len());

        std::ptr::copy_nonoverlapping(properties.as_ptr(), buffer as *mut u8, copy_length);
        *buffer.add(copy_length) = 0;

        if !properties_length.is_null() {
            *properties_length = properties.len() + 1;
        }

        if !properties_version.is_null() {
            *properties_version = desired_properties.version;
        }

        copy_length == properties.len()
    };

    if success {
        CResult::SpotflowOk
    } else {
        update_last_error(anyhow!(
            "Buffer for current desired properties needs to be at least {} bytes long.",
            properties.len() + 1
        ));
        CResult::SpotflowInsufficientBuffer
    }
}

/// Wait until a change of Desired Properties is received from the Platform.
///
/// @param client The @ref spotflow_client_t object.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if the argument is invalid.
#[no_mangle]
#[allow(deprecated)] // We're keeping this function here until the original one is stabilized or removed
                     /*pub*/
unsafe extern "C" fn spotflow_client_wait_desired_properties_changed(
    client: *const DeviceClient,
) -> CResult {
    let client = AssertUnwindSafe(client);
    call_safe_with_unit_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;
        client.wait_desired_properties_changed()?;
        Ok(())
    })
}

/// Write the Reported Properties of the Device Twin into the provided buffer. The content is a JSON
/// string encoded in UTF-8. Only the latest version is returned, any number of versions between the
/// last obtained one and the current one will be skipped.
///
/// @param client The @ref spotflow_client_t object.
/// @param buffer The buffer to write the JSON string encoded in UTF-8 into.
/// @param buffer_length The length of the buffer in bytes.
/// @param properties_length (Output) The length of the JSON string in bytes including the trailing null character.
/// @return @ref SPOTFLOW_OK if the properties were written successfully, @ref SPOTFLOW_INSUFFICIENT_BUFFER if the buffer
///         is too small (you can then resize it using `properties_length` and call the function again), @ref SPOTFLOW_NOT_READY
///         if the Reported Properties are not ready yet.
#[no_mangle]
#[allow(deprecated)] // We're keeping this function here until the original one is stabilized or removed
                     /*pub*/
unsafe extern "C" fn spotflow_client_read_reported_properties(
    client: *const DeviceClient,
    buffer: *mut c_char,
    buffer_length: size_t,
    properties_length: *mut size_t,
) -> CResult {
    let client = AssertUnwindSafe(client);
    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;
        Ok(client.reported_properties())
    });
    match result {
        Err(e) => e,
        Ok(None) => {
            update_last_error(anyhow!("Reported properties are not ready yet."));
            CResult::SpotflowNotReady
        }
        Ok(Some(properties)) => {
            let success = if buffer_length == 0 {
                false
            } else {
                let copy_length = min(buffer_length - 1, properties.len());

                std::ptr::copy_nonoverlapping(properties.as_ptr(), buffer as *mut u8, copy_length);
                *buffer.add(copy_length) = 0;

                if !properties_length.is_null() {
                    *properties_length = properties.len() + 1;
                }

                copy_length == properties.len()
            };

            if success {
                CResult::SpotflowOk
            } else {
                update_last_error(anyhow!(
                    "Buffer for current reported properties needs to be at least {} bytes long.",
                    properties.len() + 1
                ));
                CResult::SpotflowInsufficientBuffer
            }
        }
    }
}

/// Enqueue an update of the [Reported Properties](https://docs.iot.spotflow.io/configure-devices/#reported-properties) to be sent to the Platform.
///
/// This method saves these reported properties persistently in the local database file.
/// The update will be sent asynchronously when possible.
/// The update may be sent later depending on Internet connectivity and other factors.
/// To be sure that it has been sent to the Platform, call
/// @ref spotflow_client_get_any_pending_reported_properties_updates.
///
/// @param client The @ref spotflow_client_t object.
/// @param properties The JSON string encoded in UTF-8 containing the desired Reported Properties.
/// @return @ref SPOTFLOW_OK if the properties were updated successfully, @ref SPOTFLOW_ERROR
///         if any argument is invalid or there was an error in accessing the local database file.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_update_reported_properties(
    client: *const DeviceClient,
    properties: *const c_char,
) -> CResult {
    let client = AssertUnwindSafe(client);
    call_safe_with_unit_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;
        let properties = ptr_to_str(properties)?;
        client.update_reported_properties(properties)
    })
}

/// Enqueue a patch of the Reported Properties to be sent to the Platform.
///
/// This method saves these reported properties persistently in the state file.
/// The patch will be sent asynchronously when possible.
/// The patch may be sent later depending on Internet connectivity and other factors.
/// To be sure that it has been sent to the Platform, call
/// @ref spotflow_client_get_any_pending_reported_properties_updates.
///
/// @param client The @ref spotflow_client_t object.
/// @param patch The JSON string containing the fragment of the Reported Properties that should be updated.
/// @return @ref SPOTFLOW_OK if the properties were updated successfully, @ref SPOTFLOW_ERROR
///         if any argument is invalid or there was an error in accessing the local database file.
#[no_mangle]
#[allow(deprecated)] // We're keeping this function here until the original one is stabilized or removed
                     /*pub*/
unsafe extern "C" fn spotflow_client_patch_reported_properties(
    client: *const DeviceClient,
    patch: *const c_char,
) -> CResult {
    let client = AssertUnwindSafe(client);
    call_safe_with_unit_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;
        let patch = ptr_to_str(patch)?;
        client.patch_reported_properties(patch)
    })
}

/// Check if there are any updates to [Reported Properties](https://docs.iot.spotflow.io/configure-devices/#reported-properties)
/// that are yet to be sent to the Platform.
///
/// @param client The @ref spotflow_client_t object.
/// @param any (Output) Whether there are any updates to Reported Properties that are yet to be sent to the Platform.
/// @return @ref SPOTFLOW_OK if the function succeeds, @ref SPOTFLOW_ERROR if any argument is invalid.
#[no_mangle]
pub unsafe extern "C" fn spotflow_client_get_any_pending_reported_properties_updates(
    client: *const DeviceClient,
    any: *mut bool,
) -> CResult {
    let client = AssertUnwindSafe(client);
    let result = call_safe_with_result(|| {
        ensure_logging();

        let client = ptr_to_ref(*client)?;
        client.any_pending_reported_properties_updates()
    });
    match result {
        Err(e) => e,
        Ok(value) => {
            *any = value;
            CResult::SpotflowOk
        }
    }
}
