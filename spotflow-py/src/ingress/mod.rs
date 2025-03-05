use anyhow::{anyhow, Result};
use std::panic::RefUnwindSafe;
use std::sync::Mutex;
use std::time::Duration;

use pyo3::exceptions::PyException;
use pyo3::types::{PyBytes, PyDict, PyTraceback, PyTuple};
use pyo3::{prelude::*, types::PyType};
use spotflow::{
    DesiredPropertiesUpdatedCallback, DeviceClientBuilder, MessageContext,
    ProvisioningOperationDisplayHandler,
};

use crate::dps::ProvisioningOperation;
use crate::{PythonProcessSignalsSource, SpotflowError};

use self::c2d::CloudToDeviceMessage;
use self::twins::DesiredProperties;

pub mod c2d;
pub mod twins;

/// An enum that specifies the compression to use for sending [Messages](https://docs.spotflow.io/send-data/#message).
/// There are three options:
///
/// - `UNCOMPRESSED` - Don't compress the message.
/// - `FASTEST` - Compress the message using the fastest compression algorithm settings.
/// - `SMALLEST_SIZE` - Compress the message using the algorithm settings that produce the smallest size.
///   Beware that this may be significantly slower than the fastest compression. We recommend to test the
///   performance of your application with this setting before using it in production.
#[pyclass]
#[derive(Clone)]
pub enum Compression {
    #[pyo3(name = "UNCOMPRESSED")]
    Uncompressed,
    #[pyo3(name = "FASTEST")]
    Fastest,
    #[pyo3(name = "SMALLEST_SIZE")]
    SmallestSize,
}

impl Compression {
    fn to_ingress_compression_option(&self) -> Option<spotflow::Compression> {
        match self {
            Compression::Uncompressed => None,
            Compression::Fastest => Some(spotflow::Compression::Fastest),
            Compression::SmallestSize => Some(spotflow::Compression::SmallestSize),
        }
    }
}

struct ProvisioningOperationDisplayCallable {
    callable: PyObject,
}

impl ProvisioningOperationDisplayHandler for ProvisioningOperationDisplayCallable {
    fn display(&self, provisioning_operation: &spotflow::ProvisioningOperation) -> Result<()> {
        let provisioning_operation = ProvisioningOperation {
            inner: provisioning_operation.clone(),
        };

        Python::with_gil(|py| -> PyResult<()> {
            let args = PyTuple::new(py, &[provisioning_operation.into_py(py)]);
            self.callable.call1(py, args)?;
            Ok(())
        })
        .map_err(|e| anyhow!(e))
    }
}

struct DesiredPropertiesUpdatedCallable {
    callable: PyObject,
}

impl RefUnwindSafe for DesiredPropertiesUpdatedCallable {}

impl DesiredPropertiesUpdatedCallback for DesiredPropertiesUpdatedCallable {
    fn properties_updated(&self, properties: spotflow::DesiredProperties) -> Result<()> {
        Python::with_gil(|py| -> PyResult<()> {
            let properties =
                DesiredProperties::new(py, properties.version, &properties.values).unwrap();
            let args = PyTuple::new(py, &[properties.into_py(py)]);
            self.callable.call1(py, args)?;
            Ok(())
        })
        .map_err(|e| anyhow!(e))
    }
}

/// A client communicating with the Platform. Create its instance using `DeviceClient.start`.
///
/// The client stores all outgoing communication to the local database file and then sends it in a background thread asynchronously.
/// Thanks to that, it works even when the connection is unreliable.
/// Similarly, the client also stores all ingoing communication to the local database file and deletes it
/// only after the application processes it.
#[pyclass]
pub struct DeviceClient {
    inner: Mutex<Option<spotflow::DeviceClient>>,
    site_id: Option<String>,
}

impl DeviceClient {
    /// Disposes of the inner structures of the class that could prevent shutdown of the layers communicating over the Internet.
    /// The actual shutdown may not happen right after this call as some other classes held in Python may still prevent it.
    /// After this invocation no functions should be called again on this class and any invocation may throw an error.
    fn disconnect(&self, py: Python<'_>) {
        py.allow_threads(|| {
            self.inner.lock().unwrap().take();
        });
    }
}

#[pymethods]
impl DeviceClient {
    /// Start communicating with the Platform. Options:
    ///
    /// - **device_id**: The unique [Device ID](https://docs.spotflow.io/connect-devices/#device-id) you
    ///   are running the code from. If you don't specify it here, you'll need to either store it in the
    ///   [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token), or choose it during the approval of the
    ///   [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
    /// - **provisioning_token**: The [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token)
    ///   used to start [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning).
    /// - **db**: The path to the local database file where the Device SDK stores the connection credentials and temporarily persists
    ///   incoming and outgoing messages. This method creates the file if it doesn't exist.
    ///   The file must end with the suffix `".db"`, for example, `"spotflow.db"`.
    ///   If you don't use an absolute path, the file is created relative to the current working directory.
    /// - **instance**: The URI/hostname of the Platform instance where the
    ///   [Device](https://docs.spotflow.io/connect-devices/#device) will connect to.
    ///   If your company uses a dedicated instance of the Platform, such as `acme.spotflow.io`, specify it here.
    ///   The default value is `api.eu1.spotflow.io`.
    /// - **display_provisioning_operation_callback**: The function that displays the details of the
    ///   [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
    ///   Set it to override the default message that is written to the standard output.
    /// - **desired_properties_updated_callback**: The function that is called right after `DeviceClient.start` with the current
    ///   version of the [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties)
    ///   and then whenever the [Device](https://docs.spotflow.io/connect-devices/#device) receives their update
    ///   from the Platform. The [Device configuration tutorial](https://docs.spotflow.io/configure-devices/tutorial-configure-device#1-start-device)
    ///   shows how to use this option. The function is called in a separate thread, so make sure that you properly synchronize
    ///   access to your shared resources. The whole interface of the Device SDK is thread-safe, so it's safe to use it in the function.
    /// - **allow_remote_access**: Whether the Device should accept remote access requests for all ports.
    ///   If the Device is not configured to accept remote access requests, it will reject any incoming requests.
    ///   The default value is `False`.
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
    #[classmethod]
    #[pyo3(signature = (device_id, provisioning_token, db, instance=None, display_provisioning_operation_callback=None, desired_properties_updated_callback=None, allow_remote_access=false))]
    #[allow(clippy::too_many_arguments)]
    fn start(
        _cls: &PyType,
        py: Python<'_>,
        device_id: Option<String>,
        provisioning_token: String,
        db: String,
        instance: Option<String>,
        display_provisioning_operation_callback: Option<PyObject>,
        desired_properties_updated_callback: Option<PyObject>,
        allow_remote_access: bool,
    ) -> PyResult<DeviceClient> {
        py.allow_threads(|| {
            let mut builder = DeviceClientBuilder::new(device_id, provisioning_token, db);

            if let Some(instance) = instance {
                builder = builder.with_instance(instance);
            }

            if let Some(callback) = display_provisioning_operation_callback {
                builder = builder.with_display_provisioning_operation_callback(Box::new(
                    ProvisioningOperationDisplayCallable { callable: callback },
                ));
            }

            if let Some(callback) = desired_properties_updated_callback {
                builder = builder.with_desired_properties_updated_callback(Box::new(
                    DesiredPropertiesUpdatedCallable { callable: callback },
                ));
            }

            // If we decide to enable specifying which specific ports are allowed, we'll need to change this to
            // a more complex object. However, the nature of Python would allow us to accept both this object and
            // bool to maintain backward compatibility.
            if allow_remote_access {
                builder = builder.with_remote_access_allowed_for_all_ports();
            }

            builder
                .with_signals_source(Box::<PythonProcessSignalsSource>::default())
                .build()
                .map(|inner| DeviceClient {
                    inner: Mutex::new(Some(inner)),
                    site_id: None,
                })
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    #[pyo3(name = "__enter__")]
    fn enter(slf: PyRef<'_, Self>) -> PyRef<'_, Self> {
        slf
    }

    #[pyo3(name = "__exit__")]
    fn exit(
        &self,
        py: Python<'_>,
        _exception_type: Option<&PyType>,
        _exception_value: Option<&PyException>,
        _traceback: Option<&PyTraceback>,
    ) {
        self.disconnect(py);
    }

    /// (Read-only) The ID of the [Workspace](https://docs.spotflow.io/manage-access/workspaces/) to which the
    /// [Device](https://docs.spotflow.io/connect-devices/#device) belongs.
    #[getter]
    fn workspace_id(&self, py: Python<'_>) -> PyResult<String> {
        py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .workspace_id()
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    /// (Read-only) The [Device ID](https://docs.spotflow.io/connect-devices/#device-id). Note that the value
    /// might differ from the one requested in `DeviceClient.start` if the technician overrides it during the approval
    /// of the [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
    #[getter]
    fn device_id(&self, py: Python<'_>) -> PyResult<String> {
        py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .device_id()
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    /// Create a `StreamSender` for sending [Messages](https://docs.spotflow.io/send-data/#message) to
    /// a [Stream](https://docs.spotflow.io/send-data/#stream) that is contained in a
    /// [Stream Group](https://docs.spotflow.io/send-data/#stream-group).
    ///
    /// If `stream_group` is omitted, the Platforms directs the Messages to the default Stream Group of the current
    /// [Workspace](https://docs.spotflow.io/manage-access/workspaces/). If `stream` is ommited, the Platform
    /// directs the Messages into the default Stream of the given Stream Group.
    fn create_stream_sender(
        &self,
        py: Python<'_>,
        stream_group: Option<String>,
        stream: Option<String>,
        compression: Option<Compression>,
    ) -> PyResult<StreamSender> {
        let compression = compression.unwrap_or(Compression::Uncompressed);

        py.allow_threads(|| {
            let connection =
                self.inner.lock().unwrap().clone().ok_or_else(|| {
                    SpotflowError::new_err("Connection has already been shut down")
                })?;

            let mut message_context = MessageContext::new(stream_group, stream);
            message_context.set_compression(compression.to_ingress_compression_option());

            Ok(StreamSender {
                connection,
                message_context,
            })
        })
    }

    /// The number of [Messages](https://docs.spotflow.io/send-data/#message) that
    /// have been persisted in the local database file but haven't been sent to the Platform yet.
    #[getter]
    fn pending_messages_count(&self, py: Python<'_>) -> PyResult<usize> {
        py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .pending_messages_count()
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    /// Block the current thread until all the [Messages](https://docs.spotflow.io/send-data/#message) that
    /// have been previously enqueued are sent to the Platform.
    fn wait_enqueued_messages_sent(&self, py: Python<'_>) -> PyResult<()> {
        py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .wait_enqueued_messages_sent()
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    /// Get the current [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties)
    /// if their version is higher than `version` or if `version` is `None`. Otherwise, return `None`.
    ///
    /// Only the latest version is returned, any versions between the last obtained one and the current one are skipped.
    fn get_desired_properties_if_newer(
        &self,
        py: Python<'_>,
        version: Option<u64>,
    ) -> PyResult<Option<DesiredProperties>> {
        let version = match version {
            Some(version) => version,
            None => {
                return self.get_desired_properties(py).map(Some);
            }
        };

        let desired = py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .desired_properties_if_newer(version)
        });

        desired
            .map(|desired| DesiredProperties::new(py, desired.version, &desired.values))
            .transpose()
    }

    /// Get the current [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties).
    ///
    /// Only the latest version is returned, any versions between the last obtained one and the current one are skipped.
    fn get_desired_properties(&self, py: Python<'_>) -> PyResult<DesiredProperties> {
        let desired = py
            .allow_threads(|| {
                self.inner
                    .lock()
                    .unwrap()
                    .as_ref()
                    .unwrap()
                    .desired_properties()
            })
            .map_err(|e| SpotflowError::new_err(e.to_string()))?;

        DesiredProperties::new(py, desired.version, &desired.values)
    }

    /// Enqueue an update of the [Reported Properties](https://docs.spotflow.io/configure-devices/#reported-properties)
    /// to be sent to the Platform.
    ///
    /// This method saves these Reported Properties persistently in the local database file.
    /// The update will be sent asynchronously when possible.
    /// The update may be sent later depending on Internet connectivity and other factors.
    /// To be sure that it has been sent to the Platform, call `any_pending_reported_properties_updates`.
    fn update_reported_properties(&self, py: Python<'_>, properties: &PyDict) -> PyResult<()> {
        let json = PyModule::import(py, "json")?;
        let dumps = json.getattr("dumps")?;

        let reported = dumps.call1((properties,))?.extract()?;

        py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .update_reported_properties(reported)
        })
        .map_err(|e| SpotflowError::new_err(e.to_string()))
    }

    /// (Read-only) Whether are there any updates to [Reported Properties](https://docs.spotflow.io/configure-devices/#reported-properties)
    /// that are yet to be sent to the Platform.
    #[getter]
    fn any_pending_reported_properties_updates(&self, py: Python<'_>) -> PyResult<bool> {
        py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .any_pending_reported_properties_updates()
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }
}

// Temporarily hidden until the interface is stabilized.
#[allow(dead_code, deprecated)]
impl DeviceClient {
    /// Get the [Reported Properties](https://docs.spotflow.io/configure-devices/#reported-properties),
    /// that were last enqueued to be sent to the Platform.
    fn get_reported_properties(&self, py: Python<'_>) -> PyResult<Py<PyAny>> {
        let reported = py.allow_threads(|| {
            self.inner
                .lock()
                .unwrap()
                .as_ref()
                .unwrap()
                .reported_properties()
                .ok_or_else(|| {
                    SpotflowError::new_err(
                        "Reported properties were expected to be ready, but are not.",
                    )
                })
        })?;

        let json = PyModule::import(py, "json")?;
        let loads = json.getattr("loads")?;

        let reported = loads.call1((reported,))?;

        Ok(reported.into())
    }

    /// (Read-only) The number of Cloud-to-Device Messages that have been received by the client but were not consumed by the user-code yet.
    // #[getter]
    fn unread_c2d_messages_count(&self, py: Python<'_>) -> PyResult<usize> {
        let connection = self.inner.lock().unwrap();
        py.allow_threads(|| {
            connection
                .as_ref()
                .unwrap()
                .pending_c2d()
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    /// Get a Cloud-to-Device Message that was sent to this device.
    ///
    /// There may be some messages persisted in the state file from previous or current runs or the method
    /// may wait for a predefined amount of seconds.
    /// If you don't specify `timeout`, the method will wait until it receives a message.
    /// The message is acknowledged when it is received in this method and will not be delivered again.
    fn read_c2d_message(
        &self,
        py: Python<'_>,
        timeout: Option<u64>,
    ) -> PyResult<CloudToDeviceMessage> {
        let connection = self.inner.lock().unwrap();
        let message = py.allow_threads(|| {
            connection
                .as_ref()
                .unwrap()
                .get_c2d(timeout.map(Duration::from_secs).unwrap_or(Duration::MAX))
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })?;

        let properties = PyDict::new(py);
        for (key, value) in &message.properties {
            properties.set_item(key, value)?;
        }

        Ok(CloudToDeviceMessage {
            content: PyBytes::new(py, message.content.as_slice()).into(),
            properties: properties.into(),
        })
    }

    /// The ID of the [Site](https://docs.spotflow.io/connect-devices/#site) the
    /// [Device](https://docs.spotflow.io/connect-devices/#device) is located at.
    // #[getter]
    fn get_site_id(&self) -> Option<String> {
        self.site_id.clone()
    }

    // #[setter]
    fn set_site_id(&mut self, site_id: Option<String>) -> PyResult<()> {
        self.site_id = site_id;
        Ok(())
    }
}

/// A sender of [Messages](https://docs.spotflow.io/send-data/#message) to
/// a [Stream](https://docs.spotflow.io/send-data/#stream).
///
/// Create it with `DeviceClient.create_stream_sender`. `StreamSender` will send all the
/// [Messages](https://docs.spotflow.io/send-data/#message) to the given
/// [Stream](https://docs.spotflow.io/send-data/#stream).
#[pyclass]
pub struct StreamSender {
    connection: spotflow::DeviceClient,
    message_context: MessageContext,
}

#[pymethods]
impl StreamSender {
    /// Send a [Message](https://docs.spotflow.io/send-data/#message) to
    /// the Platform.
    ///
    /// **Warning:** This method blocks the current thread until the Message (and all the Messages enqueued before it)
    /// is sent to the Platform. If your Device doesn't have a stable Internet connection, consider using `enqueue_message` instead.
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
    fn send_message(
        &mut self,
        py: Python<'_>,
        payload: Vec<u8>,
        batch_id: Option<String>,
        message_id: Option<String>,
        batch_slice_id: Option<String>,
        chunk_id: Option<String>,
    ) -> PyResult<()> {
        py.allow_threads(|| {
            self.connection
                .send_message_advanced(
                    &self.message_context,
                    batch_id,
                    batch_slice_id,
                    message_id,
                    chunk_id,
                    payload,
                )
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
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
    /// You can check the number of pending messages in the queue using `DeviceClient.pending_messages_count`.
    fn enqueue_message(
        &mut self,
        py: Python<'_>,
        payload: Vec<u8>,
        batch_id: Option<String>,
        message_id: Option<String>,
        batch_slice_id: Option<String>,
        chunk_id: Option<String>,
    ) -> PyResult<()> {
        py.allow_threads(|| {
            self.connection
                .enqueue_message_advanced(
                    &self.message_context,
                    batch_id,
                    batch_slice_id,
                    message_id,
                    chunk_id,
                    payload,
                )
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
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
    /// You can check the number of pending messages in the queue using `DeviceClient.pending_messages_count`.
    fn enqueue_batch_completion(&mut self, py: Python<'_>, batch_id: String) -> PyResult<()> {
        py.allow_threads(|| {
            self.connection
                .enqueue_batch_completion(&self.message_context, batch_id)
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }

    /// Enqueue the manual completion of the current [Message](https://docs.spotflow.io/send-data/#message) to
    /// be sent to the Platform. Use this methods when Message Chunking is used.
    ///
    /// The method returns right after it saves the message-completing Message to the queue in the local database file.
    /// A background thread asynchronously sends the Messages from the queue to the Platform.
    /// You can check the number of pending messages in the queue using `DeviceClient.pending_messages_count`.
    fn enqueue_message_completion(
        &mut self,
        py: Python<'_>,
        batch_id: String,
        message_id: String,
    ) -> PyResult<()> {
        py.allow_threads(|| {
            self.connection
                .enqueue_message_completion(&self.message_context, batch_id, message_id)
                .map_err(|e| SpotflowError::new_err(e.to_string()))
        })
    }
}
