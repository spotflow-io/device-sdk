use std::{
    num::NonZeroU32,
    path::Path,
    time::{Duration, Instant},
};

use azure_storage::core::prelude::*;
use azure_storage_blobs::prelude::*;

use spotflow::{Compression, DeviceClientBuilder, MessageContext};

use log::*;
use uuid::Uuid;

#[path = "../examples/common/mod.rs"]
mod common;

#[test]
fn ingress() {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or("sqlx=warn,ureq=warn,rumqtt=warn,info"),
    )
    .init();

    let env_ctx = common::EnvironmentContext::try_load()
        .expect("Unable to load settings from environment variables.");

    let platform_caller = common::PlatformCaller::try_new(&env_ctx)
        .expect("This test needs to call the Platform automatically and it's unable to do so.");

    let device_id = format!("messages_test_{}", Uuid::new_v4());

    let stream_group = "device-sdk";
    let stream = "rust";

    let batch_count = 10;
    let message_count = 10;

    let path = Path::new("./test.db");

    info!("Using device ID {}", device_id);

    info!("Initiating tests of Message sending");

    common::clear_db(path);

    info!("Creating Device Client");

    let startup = Instant::now();
    let sending: Instant;
    let buffered: Instant;

    {
        let client = DeviceClientBuilder::new(
            Some(device_id.clone()),
            env_ctx.provisioning_token.clone(),
            path,
        )
        .with_instance(env_ctx.instance_url.to_string())
        .with_display_provisioning_operation_callback(Box::new(
            common::ProvisioningOperationApprovalHandler::new(Some(platform_caller.clone())),
        ))
        .build()
        .expect("Unable to build ingress connection");

        let mut message_context =
            MessageContext::new(Some(stream_group.to_owned()), Some(stream.to_owned()));
        message_context.set_compression(Some(Compression::Fastest));

        sending = Instant::now();

        for batch_id in 0..batch_count {
            let batch_id = format!("{batch_id:0>2}");

            for message_id in 0..message_count {
                let message_id = format!("{message_id:0>2}");

                debug!("Publishing message {message_id}");
                client
                    .enqueue_message(
                        &message_context,
                        Some(batch_id.clone()),
                        Some(message_id),
                        vec![b'a'; 1000],
                    )
                    .expect("Unable to send message");
            }

            info!("Completing batch {batch_id}");
            client
                .enqueue_batch_completion(&message_context, batch_id)
                .expect("Unable to complete batch");
        }

        info!("Dropping original ingress");
    }
    {
        info!("Building new ingress");

        let client =
            DeviceClientBuilder::new(Some(device_id.clone()), env_ctx.provisioning_token, path)
                .with_instance(env_ctx.instance_url.to_string())
                .with_display_provisioning_operation_callback(Box::new(
                    common::ProvisioningOperationApprovalHandler::new(Some(
                        platform_caller.clone(),
                    )),
                ))
                .build()
                .expect("Unable to build ingress connection");

        buffered = Instant::now();

        loop {
            let pending = client
                .pending_messages_count()
                .expect("Unable to obtain number of pending messages");
            if pending == 0 {
                break;
            }
            if Instant::now() - buffered > Duration::from_secs(30) {
                panic!("Sending data took too long");
            }
            info!("Waiting for {} more messages to be sent.", pending);
            std::thread::sleep(std::time::Duration::from_millis(500));
        }
    }

    let sent = Instant::now();

    // Get credentials to the landing store
    let storage_sas_uri = platform_caller
        .get_workspace_storage_sas_uri()
        .expect("Unable to get storage SAS URI");
    let account_name = storage_sas_uri.host().unwrap().split('.').next().unwrap();
    let container_name = storage_sas_uri.path().trim_start_matches('/');
    let sas_token = storage_sas_uri.query().unwrap();

    // Check landing store
    let http_client = azure_core::new_http_client();
    let storage_client = StorageAccountClient::new_sas_token(http_client, account_name, sas_token)
        .unwrap()
        .as_storage_client();

    let container_client = storage_client.as_container_client(container_name);

    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();

    let blobs_prefix = format!("{stream_group}/{stream}/{device_id}/");
    runtime.block_on(async move {
        let mut blobs;
        loop {
            let iv = container_client
                .list_blobs()
                .prefix(blobs_prefix.as_str())
                .max_results(NonZeroU32::new(1000).unwrap())
                .execute()
                .await
                .unwrap();

            let blob_count = iv.blobs.blobs.len();
            blobs = Some(iv);

            let expected_count = (batch_count * message_count) as usize;
            if blob_count < expected_count {
                error!(
                    "Unexpected number of blobs: Expected {}, got {}",
                    expected_count, blob_count
                );
                tokio::time::sleep(Duration::from_secs(5)).await;
                continue;
            } else {
                break;
            }
        }

        let blobs_ready = Instant::now();

        if let Some(blobs) = blobs {
            for cont in blobs.blobs.blobs.iter() {
                log::trace!("\t{}\t{} bytes", cont.name, cont.properties.content_length);
            }
        }

        info!("Startup: {:?}", sending - startup);
        info!("Sending: {:?}", buffered - sending);
        info!("Actual Sending: {:?}", sent - buffered);
        info!("Time until data is ready: {:?}", blobs_ready - sent);
    });

    log::info!("Deleting the Device");
    platform_caller
        .delete_device(&device_id)
        .expect("Unable to delete the device");
}
