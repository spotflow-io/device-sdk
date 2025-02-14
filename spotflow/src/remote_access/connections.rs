use futures_util::{future, SinkExt, StreamExt};
use std::{
    collections::HashMap,
    mem,
    panic::RefUnwindSafe,
    sync::{
        mpsc::{self, Receiver, Sender},
        Arc,
    },
    thread,
};
use thiserror::Error;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
    runtime::Runtime,
    task::JoinHandle,
};
use tokio_tungstenite::{connect_async, tungstenite::Message};

// Command enum for communication with the runtime thread
enum Command {
    Connect {
        target_port: u16,
        tunnel_uri: String,
        response_tx: mpsc::Sender<Result<(), ConnectionError>>,
    },
    Shutdown,
}

#[derive(Clone)]
pub struct ConnectionManager {
    command_tx: Arc<Sender<Command>>,
    runtime_thread: Arc<thread::JoinHandle<()>>,
}

// Explicitly implement RefUnwindSafe since our internal state is protected by channels
impl RefUnwindSafe for ConnectionManager {}

struct RuntimeManager {
    active_connections: HashMap<u16, JoinHandle<Result<(), ConnectionError>>>,
    command_rx: Receiver<Command>,
}

#[derive(Debug, Error)]
pub enum ConnectionError {
    #[error("Connection already exists")]
    ConnectionAlreadyExists,
    #[error("Failed to connect to the target port: {0}")]
    TargetPortConnectionFailed(#[from] std::io::Error),
    #[error("Failed to connect to the remote server: {0}")]
    ServerConnectionFailed(#[from] tokio_tungstenite::tungstenite::Error),
}

impl ConnectionManager {
    pub fn new() -> Self {
        let (command_tx, command_rx) = mpsc::channel();

        // Spawn a new thread with tokio runtime
        let runtime_thread = thread::spawn(move || {
            let runtime = Runtime::new().expect("Failed to create Tokio runtime");
            let mut runtime_manager = RuntimeManager {
                active_connections: HashMap::new(),
                command_rx,
            };

            runtime.block_on(async {
                while let Ok(cmd) = runtime_manager.command_rx.recv() {
                    match cmd {
                        Command::Connect {
                            target_port,
                            tunnel_uri,
                            response_tx,
                        } => {
                            let result = runtime_manager
                                .handle_connect(target_port, tunnel_uri)
                                .await;
                            let _ = response_tx.send(result);
                        }
                        Command::Shutdown => {
                            runtime_manager.shutdown().await;
                            break;
                        }
                    }
                }
            });
        });

        Self {
            command_tx: Arc::new(command_tx),
            runtime_thread: Arc::new(runtime_thread),
        }
    }

    pub fn connect(&self, target_port: u16, tunnel_uri: String) -> Result<(), ConnectionError> {
        let (response_tx, response_rx) = mpsc::channel();

        self.command_tx
            .send(Command::Connect {
                target_port,
                tunnel_uri,
                response_tx,
            })
            .expect("Runtime thread has terminated");

        // Block until we receive the result
        response_rx.recv().expect("Runtime thread has terminated")
    }
}

impl Drop for ConnectionManager {
    fn drop(&mut self) {
        // Send shutdown command when the last instance is dropped
        if Arc::strong_count(&self.command_tx) == 1 {
            let _ = self.command_tx.send(Command::Shutdown);
            // We can safely take the JoinHandle since we're the last reference
            if let Some(thread) = Arc::get_mut(&mut self.runtime_thread) {
                let _ = mem::replace(thread, thread::spawn(|| {})).join();
            }
        }
    }
}

impl RuntimeManager {
    async fn handle_connect(
        &mut self,
        target_port: u16,
        tunnel_uri: String,
    ) -> Result<(), ConnectionError> {
        // Check if a connection already exists for this port
        if let Some(handle) = self.active_connections.get(&target_port) {
            if !handle.is_finished() {
                log::info!("Attempting to connect to port {} which is already in use by another connection.", target_port);
                return Err(ConnectionError::ConnectionAlreadyExists);
            }
        }

        // Spawn a new tokio task to handle the connection
        let handle =
            tokio::spawn(async move { process_connection(target_port, &tunnel_uri).await });

        // Store the handle in our active connections
        self.active_connections.insert(target_port, handle);

        Ok(())
    }

    async fn shutdown(&mut self) {
        // Close all active connections
        for (_, handle) in self.active_connections.drain() {
            handle.abort();
        }
    }
}

async fn process_connection(target_port: u16, tunnel_uri: &str) -> Result<(), ConnectionError> {
    let (ws_stream, _) = connect_async(tunnel_uri)
        .await
        .map_err(ConnectionError::ServerConnectionFailed)?;
    log::info!("Connected to the server for port {}", target_port);

    let socket_stream = TcpStream::connect(format!("127.0.0.1:{}", target_port))
        .await
        .map_err(ConnectionError::TargetPortConnectionFailed)?;

    log::info!("Connected to the target port {}", target_port);

    let (mut ws_sink, mut ws_stream) = ws_stream.split();
    let (mut socket_read, mut socket_write) = socket_stream.into_split();

    let ws_to_socket = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_stream.next().await {
            match msg {
                Message::Binary(_) | Message::Text(_) => {
                    let data = msg.into_data();
                    println!("DeviceWS->Device: Forwarding {} bytes.", data.len());
                    if let Err(e) = socket_write.write_all(&data).await {
                        log::info!(
                            "Failed to write message to the target port, cancelling forwarding: {}",
                            e
                        );
                        break;
                    }
                }
                Message::Close(_) => {
                    log::info!("DeviceWS->Device: Close message received, cancelling forwarding.");
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
                log::info!(
                    "Device->DeviceWS: Received 0 bytes from the port, sending close message."
                );
                if let Err(e) = ws_sink.send(Message::Close(None)).await {
                    log::warn!("Failed to send close message: {}", e);
                }

                log::info!("Device->DeviceWS: Cancelling forwarding.");
                break;
            }

            println!("Device->DeviceWS: Forwarding {} bytes.", n);
            if let Err(e) = ws_sink
                .send(Message::Binary(buf[..n].to_vec().into()))
                .await
            {
                log::info!(
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
        "Forwarding between the server and the target port {} cancelled in both directions.",
        target_port
    );

    Ok(())
}
