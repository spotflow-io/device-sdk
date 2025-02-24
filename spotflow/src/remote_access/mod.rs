mod connections;

use std::str::FromStr;

use connections::{ConnectionDetails, ConnectionError};
use http::Uri;
use log::warn;
use serde::Deserialize;
use uuid::Uuid;

use crate::ingress::MethodHandler;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct RequestPayload {
    pub tunnel_id: Uuid,
    pub port: u16,
    pub tunnel_secure_uri: String,
    pub traceparent_header: Option<String>,
}

pub fn create_remote_access_method_handler<F: MethodHandler>(
    chained_handler: Option<F>,
) -> impl MethodHandler {
    let connections = connections::ConnectionManager::new();

    move |method_name: String, payload: &[u8]| {
        if method_name == "!remote-access" {
            let Ok(payload) = serde_json::from_slice::<RequestPayload>(payload) else {
                return Some((400, b"{\"error\": \"Invalid payload\"}".to_vec()));
            };

            let Ok(tunnel_uri) = Uri::from_str(&payload.tunnel_secure_uri) else {
                return Some((400, b"{\"error\": \"Invalid tunnel secure URI\"}".to_vec()));
            };

            let details = ConnectionDetails {
                tunnel_id: payload.tunnel_id,
                target_port: payload.port,
                tunnel_uri,
                traceparent_header: payload.traceparent_header,
            };

            let result = connections.connect(details);

            match result {
                Ok(_) => Some((200, vec![])),
                Err(ConnectionError::PreviousAttemptStillActive(tunnel_id)) => {
                    warn!(
                        "The previous attempt to connect to the tunnel '{}' is still active.",
                        tunnel_id
                    );
                    Some((
                        409,
                        b"{\"error\": \"Previous attempt still active.\"}".to_vec(),
                    ))
                }
                Err(ConnectionError::TargetPortConnectionFailed(e)) => {
                    warn!("Unable to connect to port {} because the target port connection failed: {}", payload.port, e);
                    Some((
                        500,
                        format!("{{\"error\": \"Failed to connect to target port: {}\"}}", e)
                            .as_bytes()
                            .to_vec(),
                    ))
                }
                Err(ConnectionError::ServerConnectionFailed(e)) => {
                    warn!("Unable to connect to port {} because the remote server connection failed: {}", payload.port, e);
                    Some((
                        500,
                        format!(
                            "{{\"error\": \"Failed to connect to remote server: {}\"}}",
                            e
                        )
                        .as_bytes()
                        .to_vec(),
                    ))
                }
            }
        } else {
            chained_handler
                .as_ref()
                .and_then(|handler| handler(method_name, payload))
        }
    }
}
