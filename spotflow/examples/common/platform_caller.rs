use std::sync::Arc;

use anyhow::{anyhow, Result};
use azure_identity::token_credentials::{AzureCliCredential, TokenCredential};
use http::Uri;
use log::info;
use oauth2::AccessToken;
use serde_json::json;

use super::{EnvironmentContext, API_TOKEN_ENV_VAR, WORKSPACE_ID_ENV_VAR};

#[derive(Clone)]
pub struct PlatformCaller {
    instance_url: Uri,
    workspace_id: String,
    api_token: Option<String>,
}

impl PlatformCaller {
    pub fn try_new(env_ctx: &EnvironmentContext) -> Result<Self> {
        let instance_url = env_ctx.instance_url.clone();

        let workspace_id = env_ctx.workspace_id.clone().ok_or_else(|| {
            anyhow!("Environment variable '{WORKSPACE_ID_ENV_VAR}' is not set. Please set it to a valid Workspace ID.")
        })?;

        let api_token = if let Some(auth0_token) = &env_ctx.api_token {
            match check_workspace(&workspace_id, &instance_url, auth0_token) {
                Ok(_) => {
                    info!("Using the API access token provided in the environment variable {API_TOKEN_ENV_VAR}.");
                    Some(auth0_token.clone())
                }
                Err(e) => {
                    return Err(anyhow!("Error while checking the Workspace '{workspace_id}' using the API access token provided in the environment variable {API_TOKEN_ENV_VAR}: {e}"));
                }
            }
        } else {
            let azure_token = get_azure_token(&instance_url, "workspace-management")
                .map_err(|e| {
                    anyhow!("Tried to obtain the access token from Azure CLI because the environment variable '{API_TOKEN_ENV_VAR}' isn't defined. Error: {e}")
                })?;

            match check_workspace(&workspace_id, &instance_url, azure_token.secret()) {
                // This token differs for each service, so we obtain it before each call
                Ok(_) => {
                    info!("Using the Azure CLI to obtain the API access token for the particular service before each request.");
                    None
                }
                Err(e) => {
                    return Err(anyhow!("Error while checking the Workspace '{workspace_id}' using the token obtained from the Azure CLI: {e}"));
                }
            }
        };

        Ok(Self {
            instance_url,
            workspace_id,
            api_token,
        })
    }

    pub fn workspace_id(&self) -> &str {
        &self.workspace_id
    }

    fn get_token(&self, service: &str) -> Result<String> {
        if let Some(token) = &self.api_token {
            Ok(token.clone())
        } else {
            let azure_token = get_azure_token(&self.instance_url, service)?;
            Ok(azure_token.secret().clone())
        }
    }

    pub fn approve_provisioning_operation(&self, operation_id: &str) -> Result<()> {
        let token = self.get_token("device-provisioning")?;
        approve_provisioning_operation(&self.instance_url, &self.workspace_id, operation_id, &token)
    }

    pub fn delete_device(&self, device_id: &str) -> Result<()> {
        let token = self.get_token("device-management")?;
        delete_device(&self.instance_url, &self.workspace_id, device_id, &token)
    }

    pub fn update_desired_properties(
        &self,
        device_id: &str,
        data: &serde_json::Value,
    ) -> Result<()> {
        let token = self.get_token("device-management")?;
        update_desired_properties(
            &self.instance_url,
            &self.workspace_id,
            device_id,
            data,
            &token,
        )
    }

    pub fn get_desired_properties(&self, device_id: &str) -> Result<serde_json::Value> {
        let token = self.get_token("device-management")?;
        get_desired_properties(&self.instance_url, &self.workspace_id, device_id, &token)
    }

    pub fn get_reported_properties(&self, device_id: &str) -> Result<serde_json::Value> {
        let token = self.get_token("device-management")?;
        get_reported_properties(&self.instance_url, &self.workspace_id, device_id, &token)
    }

    pub fn send_c2d_message(&self, device_id: &str, data: &[u8]) -> Result<()> {
        let token = self.get_token("device-management")?;
        send_c2d_message(
            &self.instance_url.to_string(),
            &self.workspace_id,
            device_id,
            data,
            &token,
        )
    }

    pub fn get_workspace_storage_sas_uri(&self) -> Result<Uri> {
        let token = self.get_token("workspace-management")?;
        get_workspace_storage_sas_uri(&self.instance_url, &self.workspace_id, &token)
    }

    pub fn get_tunnel_secure_uri(&self, device_id: &str, port: u16) -> Result<Uri> {
        let token = self.get_token("device-management")?;
        get_tunnel_secure_uri(
            &self.instance_url,
            &self.workspace_id,
            device_id,
            port,
            &token,
        )
    }
}

fn get_azure_token(url: &Uri, service: &str) -> Result<AccessToken> {
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();

    let creds = AzureCliCredential {};
    let resource = format!(
        "https://{service}.{}",
        url.host()
            .expect("Invalid instance URL")
            .trim_start_matches("api."),
    );
    let h = rt.spawn(async move { creds.get_token(&resource).await });
    let token = rt
        .block_on(h)
        .expect("Error while running the process for obtaining Azure token")?
        .token;
    rt.shutdown_background();

    Ok(token)
}

fn check_workspace(workspace_id: &str, instance_url: &Uri, token: &str) -> Result<()> {
    let url = format!("{instance_url}workspaces/{workspace_id}");
    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    agent.get(&url).set("Authorization", &auth_header).call()?;

    Ok(())
}

fn approve_provisioning_operation(
    instance_url: &Uri,
    workspace_id: &str,
    operation_id: &str,
    token: &str,
) -> Result<()> {
    let url = format!("{instance_url}workspaces/{workspace_id}/provisioning-operations/approve");
    let auth_header = format!("Bearer {token}");

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

fn delete_device(
    instance_url: &Uri,
    workspace_id: &str,
    device_id: &str,
    token: &str,
) -> Result<()> {
    let url = format!("{instance_url}workspaces/{workspace_id}/devices/{device_id}/delete");
    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    agent
        .delete(&url)
        .set("Content-Type", "application/json")
        .set("Authorization", &auth_header)
        .call()?;

    Ok(())
}

fn update_desired_properties(
    instance_url: &Uri,
    workspace_id: &str,
    device_id: &str,
    data: &serde_json::Value,
    token: &str,
) -> Result<()> {
    let url =
        format!("{instance_url}workspaces/{workspace_id}/devices/{device_id}/desired-properties");

    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    agent
        .put(&url)
        .set("Content-Type", "application/json")
        .set("Authorization", &auth_header)
        .send_json(data)?;

    Ok(())
}

fn get_desired_properties(
    instance_url: &Uri,
    workspace_id: &str,
    device_id: &str,
    token: &str,
) -> Result<serde_json::Value> {
    let url =
        format!("{instance_url}workspaces/{workspace_id}/devices/{device_id}/desired-properties");

    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    let json = agent
        .get(&url)
        .set("Authorization", &auth_header)
        .call()?
        .into_json::<serde_json::Value>()?;

    Ok(json)
}

fn get_reported_properties(
    instance_url: &Uri,
    workspace_id: &str,
    device_id: &str,
    token: &str,
) -> Result<serde_json::Value> {
    let url =
        format!("{instance_url}workspaces/{workspace_id}/devices/{device_id}/reported-properties");

    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    let json = agent
        .get(&url)
        .set("Authorization", &auth_header)
        .call()?
        .into_json::<serde_json::Value>()?;

    Ok(json)
}

pub fn send_c2d_message(
    instance_url: &str,
    workspace_id: &str,
    device_id: &str,
    data: &[u8],
    token: &str,
) -> Result<()> {
    let url = format!("{instance_url}workspaces/{workspace_id}/devices/{device_id}/c2d-messages");
    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    agent
        .post(&url)
        .set("Authorization", &auth_header)
        .send_bytes(data)?;

    Ok(())
}

pub fn get_workspace_storage_sas_uri(
    instance_url: &Uri,
    workspace_id: &str,
    token: &str,
) -> Result<Uri> {
    let url = format!("{instance_url}workspaces/{workspace_id}/storage/secure-uri");
    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    let data = json!({
        "container": "Messages",
        "duration": "00:10:00",
    });

    let json = agent
        .post(&url)
        .set("Content-Type", "application/json")
        .set("Authorization", &auth_header)
        .send_json(data)?
        .into_json::<serde_json::Value>()?;

    let uri = json
        .get("secureUri")
        .ok_or_else(|| anyhow!("The response doesn't contain the 'secureUri' field"))?
        .as_str()
        .ok_or_else(|| anyhow!("The 'secureUri' field is not a string"))?;

    Ok(uri.parse()?)
}

pub fn get_tunnel_secure_uri(
    instance_url: &Uri,
    workspace_id: &str,
    device_id: &str,
    port: u16,
    token: &str,
) -> Result<Uri> {
    let url = format!("{instance_url}workspaces/{workspace_id}/devices/{device_id}/remote-access/ports/{port}/secure-uri");
    let auth_header = format!("Bearer {token}");

    let connector = Arc::new(native_tls::TlsConnector::new().unwrap());
    let agent = ureq::AgentBuilder::new().tls_connector(connector).build();

    let json = agent
        .post(&url)
        .set("Authorization", &auth_header)
        .call()?
        .into_json::<serde_json::Value>()?;

    let uri = json
        .get("tunnelSecureUri")
        .ok_or_else(|| anyhow!("The response doesn't contain the 'tunnelSecureUri' field"))?
        .as_str()
        .ok_or_else(|| anyhow!("The 'tunnelSecureUri' field is not a string"))?;

    Ok(uri.parse()?)
}
