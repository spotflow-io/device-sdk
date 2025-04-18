//! This crate contains the Device SDK for the Spotflow IoT Platform.
//! More information:
//!
//! - [Rust interface documentation](https://docs.spotflow.io/device-sdk/rust/)
//! - [Spotflow IoT Platform documentation](https://docs.spotflow.io)
//! - [Spotflow IoT Platform homepage](https://iot.spotflow.io/platform)

use anyhow::Result;

mod cloud;
mod connection;
mod ingress;
mod iothub;
mod persistence;
mod remote_access;

#[doc(hidden)]
pub use ingress::CloudToDeviceMessage;

pub use ingress::{
    Compression, DesiredProperties, DesiredPropertiesUpdatedCallback, DeviceClient,
    DeviceClientBuilder, MessageContext, ProvisioningOperation,
    ProvisioningOperationDisplayHandler,
};

pub(crate) mod utils;

/// Checks if a system signal requested the process to stop.
///
/// This trait doesn't have to be used in environments where the process runtime already handles the
/// signals in the background. This doesn't happen, for example, in Python runtime, because there the
/// signals must be explicitly checked during the execution of long-running operations.
pub trait ProcessSignalsSource: Send + Sync {
    /// Return an error if the process should stop due to a signal.
    fn check_signals(&self) -> Result<()>;
}

pub(crate) struct EmptyProcessSignalsSource {
    _private: (),
}

const EMPTY_PROCESS_SIGNALS_SOURCE: EmptyProcessSignalsSource =
    EmptyProcessSignalsSource { _private: () };

impl EmptyProcessSignalsSource {
    pub fn instance() -> &'static Self {
        &EMPTY_PROCESS_SIGNALS_SOURCE
    }
}

impl ProcessSignalsSource for EmptyProcessSignalsSource {
    fn check_signals(&self) -> Result<()> {
        Ok(())
    }
}
