use pyo3::prelude::*;

/// A wrapper of [Desired Properties](https://docs.iot.spotflow.io/configure-devices/#desired-properties).
#[pyclass]
#[derive(Clone)]
pub struct DesiredProperties {
    /// The version of the properties.
    #[pyo3(get)]
    pub version: u64,
    /// The dictionary containing the values of the individual properties.
    #[pyo3(get)]
    pub values: Py<PyAny>,
}

impl DesiredProperties {
    pub(crate) fn new(py: Python<'_>, version: u64, values: &str) -> PyResult<DesiredProperties> {
        let json = PyModule::import(py, "json")?;
        let loads = json.getattr("loads")?;

        let desired_values = loads.call1((values,))?;

        Ok(DesiredProperties {
            version,
            values: desired_values.into(),
        })
    }
}
