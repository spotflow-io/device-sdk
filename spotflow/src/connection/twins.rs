use std::panic::RefUnwindSafe;

use anyhow::Result;
use async_trait::async_trait;

/// A wrapper of [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties).
#[derive(Clone, Debug)]
pub struct DesiredProperties {
    /// The version of the properties.
    pub version: u64,
    /// The values of the individual properties encoded in JSON.
    pub values: String,
}

/// Handles updates of the [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties).
pub trait DesiredPropertiesUpdatedCallback: Send + Sync + RefUnwindSafe {
    /// Handle the updated version of the [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties).
    fn properties_updated(&self, properties: DesiredProperties) -> Result<()>;
}

#[async_trait]
pub trait TwinsClient: Send + Sync {
    // Makes the connection update the twins from cloud
    async fn get_twins(&self);
    async fn get_reported_properties(&self) -> Option<String>;
    async fn set_reported_properties(&self, patch: &str) -> Result<()>;
    async fn patch_reported_properties(&self, patch: &str) -> Result<()>;
    async fn get_desired_properties(&self) -> Result<DesiredProperties>;
    async fn get_desired_properties_if_newer(&self, version: u64) -> Option<DesiredProperties>;
    async fn desired_properties_changed(&self) -> Result<DesiredProperties>;
    // Whether there are any Reported Properties that have not yet been sent upstream
    async fn pending_reported_properties_updates(&self) -> Result<bool>;
    async fn wait_properties_ready(&self) -> Result<()>;
}
