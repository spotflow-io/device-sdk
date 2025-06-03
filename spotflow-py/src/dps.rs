use pyo3::prelude::*;

/// The summary of an ongoing [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
///
/// If you specify a custom callback to `DeviceClient.start`,
/// you'll receive a `ProvisioningOperation` as its argument.
#[pyclass]
pub struct ProvisioningOperation {
    pub(crate) inner: spotflow::ProvisioningOperation,
}

#[pymethods]
impl ProvisioningOperation {
    /// (Read-only) The ID of this [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
    #[getter]
    pub fn id(&self) -> String {
        self.inner.id.clone()
    }

    /// (Read-only) The verification code of this [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
    #[getter]
    pub fn verification_code(&self) -> String {
        self.inner.verification_code.clone()
    }

    /// (Read-only) The expiration time of this [Provisioning Operation](https://docs.iot.spotflow.io/connect-devices/#provisioning-operation).
    /// The operation is no longer valid after that.
    ///
    /// The date/time format is [RFC 3339](https://www.rfc-editor.org/rfc/rfc3339#section-5.8).
    #[getter]
    pub fn expiration_time(&self) -> String {
        self.inner.expiration_time.to_rfc3339()
    }
}
