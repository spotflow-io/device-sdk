mod connections;

use connections::ConnectionError;
use log::warn;
use serde::Deserialize;

use crate::ingress::Handler;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct RequestPayload {
    pub port: u16,
    pub tunnel_secure_uri: String,
}

pub fn create_remote_access_method_handler<F: Handler>(chained_handler: Option<F>) -> impl Handler {
    let connections = connections::ConnectionManager::new();

    move |method_name: String, payload: &[u8]| {
        if method_name == "!remote-access" {
            let Ok(payload) = serde_json::from_slice::<RequestPayload>(payload) else {
                return Some((400, b"{\"error\": \"Invalid payload\"}".to_vec()));
            };

            let result = connections.connect(payload.port, payload.tunnel_secure_uri);

            match result {
                Ok(_) => Some((200, vec![])),
                Err(ConnectionError::ConnectionAlreadyExists) => {
                    warn!(
                        "Unable to connect to port {} because a connection already exists.",
                        payload.port
                    );
                    Some((409, b"{\"error\": \"Connection already exists\"}".to_vec()))
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
                .map(|handler| handler(method_name, payload))
                .flatten()
        }
    }
}
