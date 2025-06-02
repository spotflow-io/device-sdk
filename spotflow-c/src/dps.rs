use libc::{c_char, c_void};

use crate::{drop_str_ptr, marshall::Marshall, string_to_ptr};

/// The callback to display the details of an ongoing [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation)
/// when the Device SDK is performing [Device Provisioning](https://docs.iot.spotflow.io/connect-devices/#device-provisioning).
/// The callback is called only if you have configured it by @ref spotflow_client_options_set_display_provisioning_operation_callback.
/// The callback is called on the same thread that calls @ref spotflow_client_start.
///
/// @param operation The summary of the [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
///                  See @ref spotflow_provisioning_operation_t for details.
/// @param context The optional context that was configured by @ref spotflow_client_options_set_display_provisioning_operation_callback.
pub type DisplayProvisioningOperationCallback =
    Option<extern "C" fn(operation: *const ProvisioningOperation, context: *mut c_void)>;

/// The summary of an ongoing [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
/// This object is managed by the Device SDK and its contents must not be modified.
///
/// If you specify a custom callback to @ref spotflow_client_options_set_display_provisioning_operation_callback,
/// you'll receive a pointer to @ref spotflow_provisioning_operation_t as its argument. The pointer is valid
/// only for the duration of the callback.
#[repr(C)]
pub struct ProvisioningOperation {
    /// (Don't modify) The ID of this [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
    pub id: *const c_char,
    /// (Don't modify) The verification code of this [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
    pub verification_code: *const c_char,
    /// (Don't modify) The expiration time of this [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
    /// The operation is no longer valid after that.
    ///
    /// The date/time format is [RFC 3339](https://www.rfc-editor.org/rfc/rfc3339#section-5.8).
    pub expiration_time: *const c_char,
}

impl Drop for ProvisioningOperation {
    fn drop(&mut self) {
        unsafe {
            drop_str_ptr(self.id as *mut c_char);
            drop_str_ptr(self.verification_code as *mut c_char);
            drop_str_ptr(self.expiration_time as *mut c_char);
        }
    }
}

impl Marshall for ProvisioningOperation {
    type Target = spotflow::ProvisioningOperation;

    fn marshall(original: Self::Target) -> Self {
        let provisioning_operation_id = string_to_ptr(original.id);
        let verification_code = string_to_ptr(original.verification_code);
        let expiration_time = string_to_ptr(original.expiration_time.to_rfc3339());
        ProvisioningOperation {
            id: provisioning_operation_id,
            verification_code,
            expiration_time,
        }
    }
}
