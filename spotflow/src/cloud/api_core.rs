use std::{sync::Arc, time::Duration};

use anyhow::{anyhow, Context, Result};
use http::{
    uri::{PathAndQuery, Scheme},
    Uri,
};
use serde::Deserialize;
use thiserror::Error;
use ureq::Response;

#[derive(Debug, Error)]
pub(crate) enum RequestError {
    #[error("request failed with status code {0}: {}", get_problem_title(.1))]
    Status(u16, Option<Box<ProblemDetails>>),
    #[error("request failed with transport error: {0}")]
    Transport(Box<ureq::Transport>),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

fn get_problem_title(details: &Option<Box<ProblemDetails>>) -> String {
    details
        .as_ref()
        .and_then(|d| d.title.clone())
        .unwrap_or_default()
}

#[allow(dead_code)]
#[derive(Debug, Deserialize)]
pub(crate) struct ProblemDetails {
    pub r#type: Option<String>,
    pub title: Option<String>,
    pub status: Option<u16>,
    pub detail: Option<String>,
    pub instance: Option<String>,
    #[serde(flatten)]
    pub extensions: serde_json::Value,
}

pub(crate) fn put(
    base_uri: &Uri,
    relative_uri: &Uri,
    token: impl AsRef<str>,
    data: impl serde::Serialize,
) -> Result<Response, RequestError> {
    send(&http::Method::PUT, base_uri, relative_uri, token, data)
}

pub(crate) fn post(
    base_uri: &Uri,
    relative_uri: &Uri,
    token: impl AsRef<str>,
    data: impl serde::Serialize,
) -> Result<Response, RequestError> {
    send(&http::Method::POST, base_uri, relative_uri, token, data)
}

pub(crate) fn send(
    method: &http::Method,
    base_uri: &Uri,
    relative_uri: &Uri,
    token: impl AsRef<str>,
    data: impl serde::Serialize,
) -> Result<Response, RequestError> {
    let Some(authority) = base_uri.authority() else {
        return Err(anyhow!("Provided base URI {base_uri:?} does not contain the authority (e.g., 'api.eu1.spotflow.io').").into());
    };
    let path = relative_uri.path_and_query();

    let uri = Uri::builder()
        .scheme(Scheme::HTTPS)
        .authority(authority.to_owned())
        .path_and_query(
            path.cloned()
                .unwrap_or_else(|| PathAndQuery::from_static("")),
        )
        .build()
        .with_context(|| format!("Unable to build URI from {base_uri:?} and {relative_uri:?}"))?;

    log::debug!("Sending request to {uri}");

    let auth_header = format!("DeviceToken {}", token.as_ref());

    let connector =
        Arc::new(native_tls::TlsConnector::new().expect("Unable to build TLS connector"));
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    let request = match *method {
        http::Method::POST => agent.post(&uri.to_string()),
        http::Method::PUT => agent.put(&uri.to_string()),
        _ => unimplemented!("Method {} is not implemented.", method),
    };

    let result = request
        .timeout(Duration::from_secs(10))
        .set("Content-Type", "application/json")
        .set("Authorization", &auth_header)
        .send_json(data);

    match result {
        Ok(response) => {
            log::debug!(
                "Request to {uri} succeeded with status code {}",
                response.status()
            );
            Ok(response)
        }
        Err(ureq::Error::Status(status, response)) => {
            let response_body = response.into_string().unwrap_or_default();

            log::debug!(
                "Request to {uri} failed with status code {status}. Response: {response_body}"
            );

            let problem_details = serde_json::from_str(&response_body).ok();

            Err(RequestError::Status(status, problem_details))
        }
        Err(ureq::Error::Transport(e)) => {
            log::debug!("Request to {uri} failed with transport error: {e}");
            Err(RequestError::Transport(Box::new(e)))
        }
    }
}
