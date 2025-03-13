use std::{
    path::Path,
    thread,
    time::{Duration, Instant},
};

use serde_json::json;
use spotflow::DeviceClientBuilder;
use uuid::Uuid;

#[path = "../examples/common/mod.rs"]
mod common;

#[allow(deprecated)] // We're using the current direct method call interface here until it's stabilized
#[test]
#[ignore]
fn c2d() {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or("sqlx=warn,ureq=warn,rumqtt=warn,info"),
    )
    .init();

    let env_ctx = common::EnvironmentContext::try_load()
        .expect("Unable to load settings from environment variables.");

    let platform_caller = common::PlatformCaller::try_new(&env_ctx)
        .expect("This test needs to call the Platform automatically and it's unable to do so.");

    let msg_cnt = 10;
    let path = Path::new("./test.db");
    let device_id = Uuid::new_v4().to_string();

    log::info!("Using device ID {}", &device_id);

    log::info!("Initiating Cloud-to-Device Message tests");

    log::info!("Creating ingress client");

    let client =
        DeviceClientBuilder::new(Some(device_id.clone()), env_ctx.provisioning_token, path)
            .with_instance(env_ctx.instance_url.to_string())
            .with_display_provisioning_operation_callback(Box::new(
                common::ProvisioningOperationApprovalHandler::new(Some(platform_caller.clone())),
            ))
            .build()
            .expect("Unable to build ingress connection");

    // FIXME: Ensure that the device is registered before sending C2D messages (use new interface that performs provisioning)
    log::info!("Awaiting C2D messages");

    let sender = thread::spawn(move || {
        log::debug!("Obtaining Azure token for sending C2D.");
        log::debug!("Azure token for sending C2D ready.");
        for i in 0..(2 * msg_cnt) {
            thread::sleep(Duration::from_millis(250));
            log::info!("Sending message {}", i);
            loop {
                let data = json!({
                    "uuid": Uuid::new_v4().to_string(),
                })
                .to_string();

                log::debug!("Sending C2D message.");

                if let Err(e) = platform_caller.send_c2d_message(&device_id, data.as_bytes()) {
                    log::warn!("Failed sending C2D message, retrying. Error: {:?}", e);
                    continue;
                }

                log::info!("C2D message sent.");
                break;
            }
        }
    });

    for _ in 0..msg_cnt {
        let start = Instant::now();
        let msg = client.get_c2d(Duration::MAX).expect(
            "This should not panic unless called concurrently or when process_c2d has been called",
        );
        let end = Instant::now();
        log::info!("C2D message received after {:?}", end - start);
        let payload = std::str::from_utf8(msg.content.as_ref()).unwrap();
        log::info!("Directly received C2D message with payload `{}`", payload);
        for (k, v) in &msg.properties {
            log::debug!("{k}: {v}");
        }
        // C2D message is acknowledged when dropped
    }

    client
        .process_c2d(|msg| {
            let payload = std::str::from_utf8(msg.content.as_ref()).unwrap();
            log::info!("Handler received C2D message with payload `{}`", payload);
            for (k, v) in &msg.properties {
                log::debug!("{k}: {v}");
            }
        })
        .expect("Unable to register c2d handler");

    sender.join().expect("Failed joining thread");
}
