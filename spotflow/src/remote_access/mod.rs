mod connections;

use std::str::FromStr;

use crate::ingress::{MethodError, MethodInvocationHandler, MethodResult, MethodReturnValue};
use connections::{ConnectionDetails, ConnectionError};
use http::Uri;
use log::warn;
use serde::Deserialize;
use uuid::Uuid;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct RequestPayload {
    pub tunnel_id: Uuid,
    pub port: u16,
    pub tunnel_secure_uri: String,
    pub traceparent_header: Option<String>,
}

pub const REMOTE_ACCESS_METHOD_NAME: &str = "!remote-access";

pub struct RemoteAccessMethodHandler {
    connections: connections::ConnectionManager,
}

impl RemoteAccessMethodHandler {
    pub fn new() -> Self {
        Self {
            connections: connections::ConnectionManager::new(),
        }
    }
}

impl MethodInvocationHandler for RemoteAccessMethodHandler {
    fn handle(&self, payload: &[u8]) -> Option<MethodResult> {
        let Ok(payload) = serde_json::from_slice::<RequestPayload>(payload) else {
            return Some(Err(MethodError::new(400, "Invalid payload".to_string())));
        };

        let Ok(tunnel_secure_uri) = Uri::from_str(&payload.tunnel_secure_uri) else {
            return Some(Err(MethodError::new(
                400,
                "Invalid tunnel secure URI".to_string(),
            )));
        };

        let details = ConnectionDetails {
            tunnel_id: payload.tunnel_id,
            target_port: payload.port,
            tunnel_secure_uri,
            traceparent_header: payload.traceparent_header,
        };

        let result = self.connections.connect(details);

        let method_return_value = match result {
            Ok(_) => {
                log::info!(
                    "Connected to the tunnel '{}' and port '{}'.",
                    payload.tunnel_id,
                    payload.port
                );
                Ok(MethodReturnValue::new(200, None))
            }
            Err(ConnectionError::PreviousAttemptStillActive(tunnel_id)) => {
                warn!(
                    "The previous attempt to connect to the tunnel '{}' is still active.",
                    tunnel_id
                );
                Err(MethodError::new(
                    409,
                    "Previous attempt still active.".to_string(),
                ))
            }
            Err(ConnectionError::TargetPortConnectionFailed(e)) => {
                warn!(
                    "Unable to connect to port {} because the target port connection failed: {}",
                    payload.port, e
                );
                Err(MethodError::new(
                    500,
                    format!("Failed to connect to target port: {}", e),
                ))
            }
            Err(ConnectionError::ServerConnectionFailed(e)) => {
                warn!(
                    "Unable to connect to port {} because the remote server connection failed: {}",
                    payload.port, e
                );
                Err(MethodError::new(
                    500,
                    format!("Failed to connect to remote server: {}", e),
                ))
            }
        };

        Some(method_return_value)
    }
}
