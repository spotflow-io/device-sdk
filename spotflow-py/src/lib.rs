use anyhow::Result;
use dps::ProvisioningOperation;
use ingress::twins::DesiredProperties;
use ingress::{Compression, DeviceClient, StreamSender};
use log::LevelFilter;
use pyo3::create_exception;
use pyo3::prelude::*;

use pyo3_log::{Caching, Logger};
use spotflow::ProcessSignalsSource;

mod dps;
mod ingress;

create_exception!(
    spotflow_device,
    SpotflowError,
    pyo3::exceptions::PyException,
    "Any function, method, or property can throw this exception when it cannot continue because of an error. \
    Errors that happen during the communication in the background are logged using the package `logging`."
);

/// You can use the Python package `spotflow_device` to integrate your
/// [Devices](https://docs.iot.spotflow.io/connect-devices/#device) with the
/// Spotflow IoT Platform, see [Get Started](https://docs.iot.spotflow.io/get-started).
///
/// ## Requirements
///
/// - Python 3.7 or newer
///
/// ## Installation
///
/// Install the package using the following command:
///
/// ```
/// pip install --upgrade spotflow-device
/// ```
///
/// We currently support the following operating systems and CPU architectures:
///
/// - Linux (x64, ARMv7, AArch64)
/// - Windows (x64)
/// - macOS (x64, AArch64)
///
/// ## Basic Usage
///
/// The following code connects the Device to the Platform and starts sending simulated sensor measurements.
///
/// You need to [register to the Platform](https://portal.spotflow.io) and set it up before you can use the code.
/// See [Get Started](https://docs.iot.spotflow.io/get-started) for more information on configuring the Platform,
/// registering your Device, and viewing the received data. Don't forget to replace `<Your Provisioning Token>`
/// with the [actual Provisioning Token](https://docs.iot.spotflow.io/connect-devices/tutorial-connect-device#1-create-provisioning-token) from
/// the Platform.
///
/// ```
/// import datetime
/// import json
/// import time
/// from spotflow_device import DeviceClient
///
/// # Connect to the Platform (starts Device Provisioning if the Device is not already registered)
/// client = DeviceClient.start(device_id="my-device", provisioning_token="<Your Provisioning Token>", db="spotflow.db")
///
/// # Create a sender to the default stream
/// sender = client.create_stream_sender(stream_group = "default-stream-group", stream = "default-stream")
///
/// for i in range(0, 60):
///     # Send and display the measured data
///     payload = json.dumps({
///         "timestamp": datetime.datetime.now().astimezone().isoformat(),
///         "temperatureCelsius": 21 + (i * 0.05),
///         "humidityPercent": 50 + (i * 0.1)
///     })
///     sender.send_message(payload.encode())
///     print(payload)
///
///     # Pause till next iteration
///     time.sleep(5)
/// ```
///
/// ## Logging
///
/// The package uses the [standard Python logging](https://docs.python.org/3/howto/logging.html).
/// Use the following code to log all the events to the standard output:
///
/// ```
/// import logging
///
/// logging.basicConfig()
/// logging.getLogger().setLevel(logging.DEBUG)
/// ```
///
/// ## Reference
#[pymodule]
fn spotflow_device(py: Python, m: &PyModule) -> PyResult<()> {
    Logger::new(py, Caching::LoggersAndLevels)?
        .filter(LevelFilter::Trace)
        .filter_target(String::from("sqlx"), LevelFilter::Warn)
        .filter_target(String::from("ureq"), LevelFilter::Warn)
        .filter_target(String::from("rumqttc"), LevelFilter::Warn)
        .filter_target(String::from("mio"), LevelFilter::Warn)
        .install()
        .expect("Logger seems to have been already installed");

    m.add("SpotflowError", py.get_type::<SpotflowError>())?;
    m.add_class::<ProvisioningOperation>()?;
    m.add_class::<Compression>()?;
    m.add_class::<DeviceClient>()?;
    m.add_class::<StreamSender>()?;
    m.add_class::<DesiredProperties>()?;
    // m.add_class::<CloudToDeviceMessage>()?;
    Ok(())
}

#[derive(Default)]
pub struct PythonProcessSignalsSource {}

impl ProcessSignalsSource for PythonProcessSignalsSource {
    fn check_signals(&self) -> Result<()> {
        let result = Python::with_gil(|py| py.check_signals());

        match result {
            Ok(_) => Ok(()),
            Err(e) => {
                log::warn!("Cancelling execution: {e}");
                Err(e.into())
            }
        }
    }
}
