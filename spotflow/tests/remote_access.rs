use std::{
    io::{Read, Write},
    net::TcpListener,
    path::Path,
    thread,
};

use tungstenite::Message;
use uuid::Uuid;

use spotflow::DeviceClientBuilder;

#[path = "../examples/common/mod.rs"]
mod common;

#[test]
fn remote_access() {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or("sqlx=warn,ureq=warn,rumqtt=warn,info"),
    )
    .init();

    let env_ctx = common::EnvironmentContext::try_load()
        .expect("Unable to load settings from environment variables.");

    let platform_caller = common::PlatformCaller::try_new(&env_ctx)
        .expect("This test needs to call the platform automatically and it's unable to do so.");

    let path = Path::new("./test.db");

    let device_id = format!("remote_access_test_{}", Uuid::new_v4());

    let port = 5097;

    log::info!("Using device ID {}", device_id);

    log::info!("Initiating Device Remote Access tests");

    common::clear_db(path);

    log::info!("Creating Device Client");

    let client =
        DeviceClientBuilder::new(Some(device_id.clone()), env_ctx.provisioning_token, path)
            .with_instance(env_ctx.instance_url.to_string())
            .with_display_provisioning_operation_callback(Box::new(
                common::ProvisioningOperationApprovalHandler::new(Some(platform_caller.clone())),
            ))
            .with_remote_access_allowed_for_all_ports()
            .build()
            .expect("Unable to build connection to platform");

    log::info!(
        "Creating background task to handle connections to the port {}",
        port
    );
    let handle = thread::spawn(move || {
        let listener = TcpListener::bind(("0.0.0.0", port)).expect("Unable to bind to the port");

        let (mut stream, _) = listener.accept().unwrap();

        log::info!(
            "Waiting for message from the port {} in the background thread",
            port
        );
        let mut buffer = [0; 1024];
        let n = stream.read(&mut buffer).unwrap();
        let name = String::from_utf8_lossy(&buffer[..n]);

        log::info!(
            "Responding to the message from the port {} in the background thread",
            port
        );
        stream
            .write_all(format!("Hello, {}!", name).as_bytes())
            .unwrap();
        stream.flush().unwrap();

        log::info!(
            "Closing the connection to the port {} in the background thread",
            port
        );
    });

    let tunnel_uri = platform_caller
        .get_tunnel_uri(&device_id, port)
        .expect("Unable to get tunnel URI");

    log::info!("Tunnel URI: {}", tunnel_uri);

    let (mut ws_stream, _) =
        tungstenite::connect(tunnel_uri).expect("Failed to connect to the tunnel");

    log::info!("Connected to the tunnel");

    log::info!("Sending message to the tunnel");
    ws_stream.send(Message::text("test")).unwrap();

    log::info!("Waiting for the response from the tunnel");
    let message = ws_stream.read().unwrap();
    log::info!("Received message from the tunnel: {}", message);

    assert_eq!(message.to_string(), "Hello, test!");

    log::info!("Closing the connection to the tunnel");
    drop(ws_stream);

    log::info!("Waiting for the background thread to finish");
    handle.join().unwrap();

    log::info!("Terminating connection");
    drop(client);

    log::info!("Deleting the device");
    platform_caller
        .delete_device(&device_id)
        .expect("Unable to delete the device");
}
