use anyhow::{Context, Result};
use chrono::{DateTime, Utc};
use http::Uri;
use serde::Deserialize;
use serde_json::json;
use thiserror::Error;

use super::{
    api_core::{post, put, RequestError},
    log_workspace_disabled_error,
};

#[derive(Debug, Error)]
pub enum InitProvisioningError {
    #[error("Provisioning Token is invalid")]
    InvalidProvisioningToken,
    #[error("Workspace is disabled")]
    WorkspaceDisabled,
    #[error("Non-recoverable error: {}", .0.as_deref().unwrap_or("Unknown error"))]
    OtherNonRecoverable(Option<String>),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

#[derive(Debug, Error)]
pub enum CompletionError {
    #[error("Provisioning Operation was not approved yet")]
    NotReady,
    #[error("Provisioning Operation was closed")]
    Closed(ProvisioningOperationClosedReason),
    #[error("Workspace is disabled")]
    WorkspaceDisabled,
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

#[derive(Debug, Error)]
pub enum RefreshError {
    #[error("Workspace is disabled")]
    WorkspaceDisabled,
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

#[derive(Debug)]
pub enum ProvisioningOperationClosedReason {
    Cancelled,
    Other,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InitProvisioningResponse {
    pub provisioning_operation_id: String,
    pub verification_code: String,
    pub expiration_time: DateTime<Utc>,
}

#[derive(Debug, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct RegistrationToken {
    #[serde(rename = "registrationToken")]
    pub token: String,
    #[serde(rename = "expirationTime")]
    pub expiration: Option<DateTime<Utc>>,
}

impl RegistrationToken {
    pub fn is_expired(&self) -> bool {
        // TODO: Make more reliable in the presence of clockskew (see expect_clockskew)
        match self.expiration {
            None => false,
            Some(datetime) => datetime < Utc::now(),
        }
    }
}

#[derive(Debug, Clone)]
pub struct ProvisioningToken {
    pub token: String,
}

impl AsRef<str> for RegistrationToken {
    fn as_ref(&self) -> &str {
        &self.token
    }
}

impl AsRef<str> for ProvisioningToken {
    fn as_ref(&self) -> &str {
        &self.token
    }
}

#[derive(Clone)]
pub struct Provisioning {
    instance_url: Uri,
    pt: ProvisioningToken,
    device_id: Option<String>,
}

impl Provisioning {
    pub fn new(instance_url: Uri, token: ProvisioningToken) -> Provisioning {
        Provisioning {
            instance_url,
            pt: token,
            device_id: None,
        }
    }

    pub fn instance_url(&self) -> &Uri {
        &self.instance_url
    }

    pub fn with_device_id(&mut self, device_id: impl AsRef<str>) -> &mut Provisioning {
        self.device_id = Some(device_id.as_ref().to_string());
        self
    }

    pub fn init(&mut self) -> Result<InitProvisioningResponse, InitProvisioningError> {
        let relative_url = Uri::from_static("/provisioning-operations/init");
        let body = match &self.device_id {
            Some(device_id) => json!({
                "deviceId": device_id,
            }),
            None => json!({}),
        };
        post(&self.instance_url, &relative_url, &self.pt, body)
            .map_err(|e| match e {
                RequestError::Status(401, _) => InitProvisioningError::InvalidProvisioningToken,
                RequestError::Status(423, _) => {
                    log_workspace_disabled_error();
                    InitProvisioningError::WorkspaceDisabled
                }
                RequestError::Status(400, Some(problem_details)) => {
                    InitProvisioningError::OtherNonRecoverable(problem_details.title)
                }
                RequestError::Status(400, _) => InitProvisioningError::OtherNonRecoverable(None),
                _ => InitProvisioningError::Other(e.into()),
            })?
            .into_json()
            .context("Failed deserializing response from JSON")
            .map_err(|e| e.into())
    }

    pub fn complete(&mut self, operation_id: &str) -> Result<RegistrationToken, CompletionError> {
        let relative_url = Uri::from_static("/provisioning-operations/complete");
        let data = json!({
            "provisioningOperationId": operation_id,
        });

        match put(&self.instance_url, &relative_url, &self.pt, data) {
            Ok(response) => {
                if response.status() == 202 {
                    Err(CompletionError::NotReady)
                } else {
                    Ok(response
                        .into_json()
                        .context("Failed deserializing response from JSON")?)
                }
            }
            Err(RequestError::Status(423, _)) => {
                log_workspace_disabled_error();
                Err(CompletionError::WorkspaceDisabled)
            }
            Err(RequestError::Status(410, Some(problem_details))) => {
                let reason = match problem_details.r#type.as_deref() {
                    Some("/problems/deviceProvisioning/provisioningOperationCancelled") => {
                        ProvisioningOperationClosedReason::Cancelled
                    }
                    _ => ProvisioningOperationClosedReason::Other,
                };

                Err(CompletionError::Closed(reason))
            }
            Err(e) => Err(CompletionError::Other(e.into())),
        }
    }
}

pub fn refresh(
    instance_url: &Uri,
    pt: &ProvisioningToken,
    rt: &RegistrationToken,
) -> Result<RegistrationToken, RefreshError> {
    let relative_url = Uri::from_static("/devices/registration-tokens/refresh");
    let data = json!({
        "registrationToken": rt.token,
    });

    put(instance_url, &relative_url, &pt.token, data)
        .map_err(|e| match e {
            RequestError::Status(423, _) => {
                log_workspace_disabled_error();
                RefreshError::WorkspaceDisabled
            }
            _ => RefreshError::Other(e.into()),
        })?
        .into_json()
        .context("Failed deserializing response from JSON")
        .map_err(|e| e.into())
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use azure_identity::token_credentials::{AzureCliCredential, TokenCredential};
    use oauth2::AccessToken;
    use uuid::Uuid;

    use crate::cloud::drs::register;

    use super::*;

    #[test]
    #[ignore]
    fn provisioning() {
        env_logger::Builder::from_env(
            env_logger::Env::default().default_filter_or("ureq=warn,debug"),
        )
        .init();

        let device_id = &Uuid::new_v4().to_string();

        let workspace_id = std::env::var("SPOTFLOW_DEVICE_SDK_TEST_WORKSPACE_ID")
            .expect("The environment variable SPOTFLOW_DEVICE_SDK_TEST_WORKSPACE_ID is not set.");

        let mut instance_uri = std::env::var("SPOTFLOW_DEVICE_SDK_TEST_INSTANCE")
            .unwrap_or_else(|_| String::from("https://api.eu1.spotflow.io"));
        if !instance_uri.starts_with("https://") {
            instance_uri = format!("https://{instance_uri}");
        }

        let instance_url: Uri = instance_uri
            .parse()
            .expect("Invalid instance URL: '{instance_uri}'");

        let pt = std::env::var("SPOTFLOW_DEVICE_SDK_TEST_PROVISIONING_TOKEN").expect(
            "The environment variable SPOTFLOW_DEVICE_SDK_TEST_PROVISIONING_TOKEN is not set.",
        );
        let pt = ProvisioningToken { token: pt };

        let mut provisioning = Provisioning::new(instance_url.clone(), pt);
        provisioning.with_device_id(device_id);
        let init = provisioning
            .init()
            .expect("Unable to initiate provisioning operation");
        let operation = init.provisioning_operation_id;
        println!("Operation ID: {operation:?}");
        println!("Code: {}", &init.verification_code);

        let result = provisioning.complete(&operation);
        // The operation has not been approved yet
        assert!(result.is_err());
        // assert!(matches!(result.unwrap_err());
        assert!(matches!(result, Err(CompletionError::NotReady)));

        // Approve will normally happen out of band by manual operator calls
        approve_provisioning(&instance_url, &workspace_id, &operation)
            .expect("Unable to approve operation");

        let result = provisioning.complete(&operation);

        // It has been already approved now.
        assert!(result.is_ok());

        println!("{:#?}", result.unwrap());

        // The result can be polled again. The token will be different and this invalidates any tokens received earlier.
        let result = provisioning.complete(&operation);

        // It has been already approved now.
        assert!(result.is_ok());

        let result = result.unwrap();
        println!("{result:#?}");

        let rt = RegistrationToken {
            token: result.token,
            expiration: result.expiration,
        };

        let res = register(&instance_url, &rt).expect("Unable to register device.");
        println!("{res:#?}");
    }

    pub fn approve_provisioning(
        instance_url: &Uri,
        workspace_id: &str,
        operation_id: &str,
    ) -> Result<()> {
        let token = get_azure_token(instance_url, "device-provisioning");

        let url =
            format!("{instance_url}workspaces/{workspace_id}/provisioning-operations/approve");
        let auth_header = format!("Bearer {}", token.secret());

        let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
        let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

        let data = json!({
            "provisioningOperationId": operation_id,
        });

        agent
            .put(&url)
            .set("Content-Type", "application/json")
            .set("Authorization", &auth_header)
            .send_json(data)?;

        Ok(())
    }

    pub fn get_azure_token(url: &Uri, service: &str) -> AccessToken {
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();

        let creds = AzureCliCredential {};
        let resource = format!(
            "https://{service}.{}",
            url.host().expect("Invalid instance URL")
        );
        let h = rt.spawn(async move { creds.get_token(&resource).await });
        let token = rt
            .block_on(h)
            .expect("Unable to obtain azure token")
            .expect("Unable to obtain azure token")
            .token;
        rt.shutdown_background();

        token
    }
}
