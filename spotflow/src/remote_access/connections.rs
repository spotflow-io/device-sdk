use futures_util::{future, SinkExt, StreamExt};
use http::Uri;
use std::{
    collections::HashMap,
    panic::RefUnwindSafe,
    sync::mpsc::{self, Receiver, Sender},
    thread,
    time::Duration,
};
use thiserror::Error;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
    runtime::Runtime,
    task::JoinHandle,
};
use tokio_tungstenite::{
    connect_async,
    tungstenite::{ClientRequestBuilder, Message},
    MaybeTlsStream, WebSocketStream,
};
use uuid::Uuid;

use crate::utils::thread::join;

// The timeout to respond to the direct method call is 30 seconds, these timeouts are chosen to fit into this time frame.
const LOCAL_PORT_CONNECTION_TIMEOUT: Duration = Duration::from_secs(5);
const WEBSOCKET_CONNECTION_TIMEOUT: Duration = Duration::from_secs(20);

#[derive(Clone)]
pub struct ConnectionDetails {
    pub tunnel_id: Uuid,
    pub target_port: u16,
    pub tunnel_secure_uri: Uri,
    pub traceparent_header: Option<String>,
}

// Command enum for communication with the runtime thread
enum Command {
    Connect {
        details: ConnectionDetails,
        response_tx: mpsc::Sender<Result<(), ConnectionError>>,
    },
    Shutdown,
}

pub struct ConnectionManager {
    command_tx: Sender<Command>,
    runtime_thread: Option<thread::JoinHandle<()>>,
}

// Explicitly implement RefUnwindSafe since our internal state is protected by channels
impl RefUnwindSafe for ConnectionManager {}

struct RuntimeManager {
    active_connections: HashMap<Uuid, JoinHandle<()>>,
    command_rx: Receiver<Command>,
}

#[derive(Debug, Error)]
pub enum ConnectionError {
    #[error("The previous attempt to connect to the tunnel '{0}' is still active.")]
    PreviousAttemptStillActive(Uuid),
    #[error("Failed to connect to the target port: {0}")]
    TargetPortConnectionFailed(#[from] std::io::Error),
    #[error("Failed to connect to the remote server: {0}")]
    ServerConnectionFailed(#[from] tokio_tungstenite::tungstenite::Error),
}

impl ConnectionManager {
    pub fn new() -> Self {
        let (command_tx, command_rx) = mpsc::channel();

        log::debug!("Starting connection manager Tokio runtime thread");

        // Spawn a new thread with Tokio runtime
        // (Needed to make sure the runtime is stable enough to allow remote device management even in the case of problems in other parts of Device SDK.)
        let runtime_thread = thread::Builder::new()
            .name("Connection manager Tokio runtime".into())
            .spawn(move || {
                let runtime = Runtime::new().expect("Failed to create Tokio runtime");
                let mut runtime_manager = RuntimeManager {
                    active_connections: HashMap::new(),
                    command_rx,
                };

                runtime.block_on(async {
                    while let Ok(cmd) = runtime_manager.command_rx.recv() {
                        match cmd {
                            Command::Connect {
                                details,
                                response_tx,
                            } => {
                                runtime_manager.handle_connect(details, response_tx).await;
                            }
                            Command::Shutdown => {
                                runtime_manager.shutdown().await;
                                break;
                            }
                        }
                    }
                });
            })
            .expect("Failed to spawn connection manager runtime thread");

        Self {
            command_tx,
            runtime_thread: Some(runtime_thread),
        }
    }

    pub fn connect(&self, details: ConnectionDetails) -> Result<(), ConnectionError> {
        let (response_tx, response_rx) = mpsc::channel();

        self.command_tx
            .send(Command::Connect {
                details,
                response_tx,
            })
            .expect("Runtime thread has terminated");

        // Block until we receive the result
        response_rx.recv().expect("Runtime thread has terminated")
    }
}

impl Drop for ConnectionManager {
    fn drop(&mut self) {
        log::debug!("Shutting down connection manager Tokio runtime thread");

        let _ = self.command_tx.send(Command::Shutdown);
        join(&mut self.runtime_thread);

        log::debug!("Connection manager Tokio runtime thread shut down");
    }
}

impl RuntimeManager {
    async fn handle_connect(
        &mut self,
        details: ConnectionDetails,
        response_tx: mpsc::Sender<Result<(), ConnectionError>>,
    ) {
        // Check if a connection already exists for this port
        if let Some(handle) = self.active_connections.get(&details.tunnel_id) {
            if !handle.is_finished() {
                let _ = response_tx.send(Err(ConnectionError::PreviousAttemptStillActive(
                    details.tunnel_id,
                )));
                return;
            }
        }

        let tunnel_id = details.tunnel_id;

        // Spawn a new tokio task to handle the connection
        let handle = tokio::spawn(async move { process_connection(details, response_tx).await });

        // Store the handle in our active connections
        self.active_connections.insert(tunnel_id, handle);
    }

    async fn shutdown(&mut self) {
        // Close all active connections
        for (_, handle) in self.active_connections.drain() {
            handle.abort();
        }
    }
}

async fn process_connection(
    details: ConnectionDetails,
    response_tx: mpsc::Sender<Result<(), ConnectionError>>,
) {
    let tunnel_id = details.tunnel_id;
    let target_port = details.target_port;

    let (ws_stream, socket_stream) = match init_connection(details).await {
        Ok(result) => {
            let _ = response_tx.send(Ok(()));
            result
        }
        Err(e) => {
            let _ = response_tx.send(Err(e));
            return;
        }
    };

    let (mut ws_sink, mut ws_stream) = ws_stream.split();
    let (mut socket_read, mut socket_write) = socket_stream.into_split();

    let ws_to_socket = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_stream.next().await {
            match msg {
                Message::Binary(_) | Message::Text(_) => {
                    let data = msg.into_data();
                    log::trace!(
                        "Forwarding {} bytes from WebSocket to the target port.",
                        data.len()
                    );
                    if let Err(e) = socket_write.write_all(&data).await {
                        log::debug!(
                            "Failed to write bytes to the target port, cancelling forwarding: {}",
                            e
                        );
                        break;
                    }
                }
                Message::Close(_) => {
                    log::debug!("Close message received from WebSocket, cancelling forwarding.");
                    break;
                }
                _ => {} // Ping and Pong are handled automatically and raw frames are never received
            }
        }
    });

    let socket_to_ws = tokio::spawn(async move {
        let mut buf = vec![0; 1024];
        while let Ok(n) = socket_read.read(&mut buf).await {
            if n == 0 {
                log::debug!("Received 0 bytes from the target port, sending close message.");
                if let Err(e) = ws_sink.send(Message::Close(None)).await {
                    log::debug!("Failed to send close message: {}", e);
                }

                log::debug!("Cancelling forwarding.");
                break;
            }

            log::trace!("Forwarding {} bytes to WebSocket.", n);
            if let Err(e) = ws_sink
                .send(Message::Binary(buf[..n].to_vec().into()))
                .await
            {
                log::debug!(
                    "Failed to write message to the server, cancelling forwarding: {}",
                    e
                );
                break;
            }
        }
    });

    let (ws_result, socket_result) = future::join(ws_to_socket, socket_to_ws).await;
    if let Err(e) = ws_result {
        log::warn!(
            "WebSocket to socket forwarding task error when joining: {}",
            e
        );
    }
    if let Err(e) = socket_result {
        log::warn!(
            "Socket to WebSocket forwarding task error when joining: {}",
            e
        );
    }

    log::info!(
        "Disconnected from the tunnel '{}' and port '{}'.",
        tunnel_id,
        target_port
    );
}

async fn init_connection(
    details: ConnectionDetails,
) -> Result<(WebSocketStream<MaybeTlsStream<TcpStream>>, TcpStream), ConnectionError> {
    log::debug!(
        "Starting connection to tunnel '{}' and port '{}'.",
        details.tunnel_id,
        details.target_port
    );

    let mut request_builder = ClientRequestBuilder::new(details.tunnel_secure_uri);

    if let Some(traceparent) = details.traceparent_header {
        request_builder = request_builder.with_header("traceparent", traceparent);
    }

    // We connect to the target port first, because once the connection to the WebSocket is established, another connections to the same
    // tunnel (i.e., the same tunnel secure URI) will be rejected. Therefore, connecting to the WebSocket first (or trying both in parallel)
    // would in the case of a failed connection to the target port cause any additional retries (i.e., another calls of the direct method) to fail.

    let socket_stream = tokio::time::timeout(
        LOCAL_PORT_CONNECTION_TIMEOUT,
        TcpStream::connect(format!("127.0.0.1:{}", details.target_port)),
    )
    .await
    .map_err(|_| {
        ConnectionError::TargetPortConnectionFailed(std::io::Error::new(
            std::io::ErrorKind::TimedOut,
            "Connection to target port timed out.",
        ))
    })?
    .map_err(ConnectionError::TargetPortConnectionFailed)?;

    log::debug!("Connected to the target port {}", details.target_port);

    let (ws_stream, _) =
        tokio::time::timeout(WEBSOCKET_CONNECTION_TIMEOUT, connect_async(request_builder))
            .await
            .map_err(|_| {
                ConnectionError::ServerConnectionFailed(tokio_tungstenite::tungstenite::Error::Io(
                    std::io::Error::new(
                        std::io::ErrorKind::TimedOut,
                        "Connection to WebSocket timed out.",
                    ),
                ))
            })?
            .map_err(ConnectionError::ServerConnectionFailed)?;

    log::debug!("Connected to WebSocket for port {}", details.target_port);

    Ok((ws_stream, socket_stream))
}
