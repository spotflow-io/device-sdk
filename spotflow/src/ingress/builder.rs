use crate::connection::twins::DesiredPropertiesUpdatedCallback;
use crate::remote_access::create_remote_access_method_handler;
use crate::{
    cloud,
    persistence::sqlite::{SdkConfiguration, SdkConfigurationFragment, SqliteStore},
};
use anyhow::{anyhow, bail, Result};
use chrono::{DateTime, Utc};
use std::path::{Path, PathBuf};

use http::Uri;

use crate::cloud::{
    dps::{
        self, CompletionError, InitProvisioningError, InitProvisioningResponse, Provisioning,
        ProvisioningOperationClosedReason, ProvisioningToken, RegistrationToken,
    },
    drs::{RegistrationError, RegistrationResponse},
};

use crate::{EmptyProcessSignalsSource, ProcessSignalsSource};

use super::{DeviceClient, MethodHandler, NoneHandler};

/// The summary of an ongoing [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
///
/// If you specify a custom implementation of [`ProvisioningOperationDisplayHandler`] to
/// [`DeviceClientBuilder::with_desired_properties_updated_callback`], you'll receive a
/// [`ProvisioningOperation`] as the argument to [`ProvisioningOperationDisplayHandler::display`].
#[derive(Clone, Debug)]
pub struct ProvisioningOperation {
    /// The Provisioning Operation ID.
    pub id: String,
    /// The verification code of this Provisioning Operation.
    pub verification_code: String,
    /// The expiration time of this Provisioning Operation.
    /// The operation is no longer valid after that.
    ///
    /// The date/time format is [RFC 3339](https://www.rfc-editor.org/rfc/rfc3339#section-5.8).
    pub expiration_time: DateTime<Utc>,
}

impl From<InitProvisioningResponse> for ProvisioningOperation {
    fn from(response: InitProvisioningResponse) -> Self {
        ProvisioningOperation {
            id: response.provisioning_operation_id,
            verification_code: response.verification_code,
            expiration_time: response.expiration_time,
        }
    }
}

/// Displays the details of the current [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
pub trait ProvisioningOperationDisplayHandler {
    /// Display the details of the current [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation) to the user.
    fn display(&self, provisioning_operation: &ProvisioningOperation) -> Result<()>;
}

enum ErrorAction<E> {
    Retry(E),
    Fail(E),
}

/// A builder for [`DeviceClient`] allowing to configure the connection to the Platform.
pub struct DeviceClientBuilder {
    database_file: PathBuf,
    provisioning_token: ProvisioningToken,
    device_id: Option<String>,
    site_id: Option<String>,
    instance: Option<String>,
    display_provisioning_operation_callback: Option<Box<dyn ProvisioningOperationDisplayHandler>>,
    desired_properties_updated_callback: Option<Box<dyn DesiredPropertiesUpdatedCallback>>,
    signals_src: Option<Box<dyn ProcessSignalsSource>>,
    remote_access_allowed_for_all_ports: bool,
}

impl DeviceClientBuilder {
    /// Creates a new [`DeviceClientBuilder`] with the provided basic configuration options:
    ///
    /// * `device_id`: The unique [Device ID](https://docs.spotflow.io/connect-devices/#device-id) you
    ///   are running the code from. If you don't specify it here, you'll need to either store it in the
    ///   [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token),
    ///   or choose it during the approval of the
    ///   [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation).
    /// * `provisioning_token`: The [Provisioning Token](https://docs.spotflow.io/connect-devices/#provisioning-token)
    ///   used to start [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning).
    /// * `db`: The path to the local database file where the Device SDK stores the connection credentials and temporarily persists
    ///   incoming and outgoing messages. This method creates the file if it doesn't exist.
    ///   The file must end with the suffix `".db"`, for example, `"spotflow.db"`.
    ///   If you don't use an absolute path, the file is created relative to the current working directory.
    pub fn new(
        device_id: Option<String>,
        provisioning_token: String,
        database_file: impl AsRef<Path>,
    ) -> Self {
        DeviceClientBuilder {
            database_file: database_file.as_ref().to_path_buf(),
            provisioning_token: ProvisioningToken {
                token: provisioning_token,
            },
            device_id,
            site_id: None,
            instance: None,
            display_provisioning_operation_callback: None,
            desired_properties_updated_callback: None,
            signals_src: None,
            remote_access_allowed_for_all_ports: false,
        }
    }

    /// Hidden from the documentation because the concept of Sites and their IDs is not yet explained in the Platform documentation.
    #[doc(hidden)]
    pub fn with_site_id(mut self, site_id: String) -> DeviceClientBuilder {
        self.site_id = Some(site_id);
        self
    }

    /// Set the URI/hostname of the Platform instance where the
    /// [Device](https://docs.spotflow.io/connect-devices/#device) will connect to.
    ///
    /// If your company uses a dedicated instance of the Platform, such as `acme.spotflow.io`, specify it here.
    /// The default value is `api.eu1.spotflow.io`.
    pub fn with_instance(mut self, instance: String) -> DeviceClientBuilder {
        self.instance = Some(instance);
        self
    }

    /// Set the callback to display the details of the
    /// [Provisioning Operation](https://docs.spotflow.io/connect-devices/#provisioning-operation)
    /// when [`DeviceClientBuilder::build`] is performing [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning).
    pub fn with_display_provisioning_operation_callback(
        mut self,
        callback: Box<dyn ProvisioningOperationDisplayHandler>,
    ) -> DeviceClientBuilder {
        self.display_provisioning_operation_callback = Some(callback);
        self
    }

    /// Set the callback that is called right after [`DeviceClientBuilder::build`] with the current version of the
    /// [Desired Properties](https://docs.spotflow.io/configure-devices/#desired-properties) and then whenever the
    /// [Device](https://docs.spotflow.io/connect-devices/#device) receives their update from the Platform.
    /// The [Device configuration tutorial](https://docs.spotflow.io/configure-devices/tutorial-configure-device#1-start-device)
    /// shows how to use this option. The callback must be `Send` and `Sync`, because it's called in a separate thread.
    pub fn with_desired_properties_updated_callback(
        mut self,
        callback: Box<dyn DesiredPropertiesUpdatedCallback>,
    ) -> DeviceClientBuilder {
        self.desired_properties_updated_callback = Some(callback);
        self
    }

    /// Set the source of the system signals that can request the process to stop.
    pub fn with_signals_source(mut self, signals_src: Box<dyn ProcessSignalsSource>) -> Self {
        self.signals_src = Some(signals_src);
        self
    }

    /// Allow the Device to accept remote access requests for all ports.
    pub fn with_remote_access_allowed_for_all_ports(mut self) -> Self {
        self.remote_access_allowed_for_all_ports = true;
        self
    }

    /// **Warning**: Don't use, the interface for direct method calls hasn't been finalized yet.
    #[deprecated]
    #[doc(hidden)]
    pub fn with_method_handler<F>(self, method_handler: F) -> DeviceClientBuilderWithHandler<F>
    where
        F: MethodHandler,
    {
        DeviceClientBuilderWithHandler {
            builder: self,
            method_handler,
        }
    }

    /// Build the [`DeviceClient`] that starts communicating with the Platform.
    ///
    /// If the [Device](https://docs.spotflow.io/connect-devices/#device) is
    /// not yet registered in the Platform, or its
    /// [Registration Token](https://docs.spotflow.io/connect-devices/#registration-token) is
    /// expired, this method performs [Device Provisioning](https://docs.spotflow.io/connect-devices/#device-provisioning)
    /// and waits for the approval.
    /// [Get Started](https://docs.spotflow.io/get-started) shows this process in practice.
    ///
    /// If the [Registration Token](https://docs.spotflow.io/connect-devices/#registration-token) from
    /// the last run is still valid, this method succeeds even without the connection to the Internet. The Device SDK will
    /// store all outgoing communication in the local database file and send it once it connects to the Platform.
    pub fn build(self) -> Result<DeviceClient> {
        self.build_impl(None::<NoneHandler>)
    }

    fn build_impl<F>(self, method_handler: Option<F>) -> Result<DeviceClient>
    where
        F: MethodHandler,
    {
        // Validate the options
        if self.database_file.as_os_str().is_empty() {
            bail!("The path to the local database file cannot be empty; provide a value.");
        }

        // Look up the last stored configuration from the local database file
        let db_config = if self.database_file.exists() {
            // Process the communication with SQLite on the current thread
            let runtime = tokio::runtime::Builder::new_current_thread()
                    .enable_all()
                    .build()
                    .map_err(|e| anyhow!("Unable to create a tokio single-threaded runtime for loading data from the local database file: {e}"))?;

            runtime.block_on(SqliteStore::load_available_configuration(
                &self.database_file,
            ))
        } else {
            SdkConfigurationFragment::default()
        };

        // Compute the URL of the Platform instance

        let instance = if let Some(instance) = &self.instance {
            // Strip optional protocol prefix
            instance.strip_prefix("https://").unwrap_or(instance)
        } else {
            "api.eu1.spotflow.io"
        };

        log::debug!("Connecting to the Platform instance '{}'", &instance);

        let instance_url = format!("https://{instance}")
            .parse::<Uri>()
            .map_err(|e| anyhow!("Unable to parse the Platform instance URL: {e}"))?;

        let signals_src: &dyn ProcessSignalsSource = self
            .signals_src
            .as_ref()
            .map_or(EmptyProcessSignalsSource::instance(), Box::as_ref);

        let (registration_token, workspace_id, device_id, registration_response) = self
            .obtain_valid_credentials(
                db_config,
                &self.provisioning_token,
                &instance_url,
                signals_src,
            )?;

        signals_src.check_signals()?;

        let config = SdkConfiguration {
            instance_url,
            provisioning_token: self.provisioning_token,
            registration_token,
            requested_device_id: self.device_id,
            workspace_id,
            device_id,
            site_id: self.site_id,
        };

        // This code duplication is caused by having the method handler type generic
        // (might be simplified in the future if we decide to use dynamic dispatch instead)
        if self.remote_access_allowed_for_all_ports {
            DeviceClient::new(
                config,
                &self.database_file,
                Some(create_remote_access_method_handler(method_handler)),
                self.desired_properties_updated_callback,
                self.signals_src,
                registration_response,
            )
        } else {
            DeviceClient::new(
                config,
                &self.database_file,
                method_handler,
                self.desired_properties_updated_callback,
                self.signals_src,
                registration_response,
            )
        }
    }

    fn obtain_valid_credentials(
        &self,
        db_config: SdkConfigurationFragment,
        options_provisioning_token: &ProvisioningToken,
        instance_url: &Uri,
        signals_src: &dyn ProcessSignalsSource,
    ) -> Result<
        (
            RegistrationToken,
            String,
            String,
            Option<RegistrationResponse>,
        ),
        anyhow::Error,
    > {
        // If possible, reuse the existing registration token
        if let (
            Some(db_provisioning_token),
            Some(db_registration_token),
            Some(db_workspace_id),
            Some(db_device_id),
        ) = (
            db_config.provisioning_token,
            db_config.registration_token,
            db_config.workspace_id,
            db_config.device_id,
        ) {
            if db_provisioning_token
                .token
                .eq(&options_provisioning_token.token)
                && db_config.requested_device_id.eq(&self.device_id)
                && !db_registration_token.is_expired()
            {
                // Check if the registration token is still valid and optionally update the current Device ID
                let (is_considered_valid, registration_response) =
                    register_if_connected(&db_registration_token, instance_url);

                signals_src.check_signals()?;

                let (workspace_id, device_id) = match &registration_response {
                    Some(response) => (
                        response.workspace_id()?.to_owned(),
                        response.device_id()?.to_owned(),
                    ),
                    None => (db_workspace_id, db_device_id),
                };

                if is_considered_valid {
                    if registration_response.is_some() {
                        log::info!("The Registration Token stored in the local database file is still valid, skipping Device Provisioning.");
                    } else {
                        log::info!(
                            "It wasn't possible to check the validity of the Registration Token stored in the local database file. \
                            It's considered valid, because it hasn't expired yet. Skipping Device Provisioning.");
                    }
                    return Ok((
                        db_registration_token,
                        workspace_id,
                        device_id,
                        registration_response,
                    ));
                }
            }
        }

        // Otherwise, perform the device provisioning
        let (registration_token, registration_response) =
            self.provision_device(options_provisioning_token, instance_url, signals_src)?;
        Ok((
            registration_token,
            registration_response.workspace_id()?.to_owned(),
            registration_response.device_id()?.to_owned(),
            Some(registration_response),
        ))
    }

    fn provision_device(
        &self,
        provisioning_token: &cloud::dps::ProvisioningToken,
        instance_url: &Uri,
        signals_src: &dyn ProcessSignalsSource,
    ) -> Result<(RegistrationToken, RegistrationResponse)> {
        log::info!("Starting device provisioning");

        let mut provisioning =
            dps::Provisioning::new(instance_url.clone(), provisioning_token.clone());

        if let Some(device_id) = &self.device_id {
            provisioning.with_device_id(device_id);
        }

        loop {
            let init_response = init_operation(&mut provisioning, signals_src)?;

            log::debug!(
                "Provisioning operation '{}' initialized, displaying details to the user",
                &init_response.provisioning_operation_id
            );

            display_operation_details(
                &init_response.clone().into(),
                &self.display_provisioning_operation_callback,
            )?;

            log::debug!("Waiting for the approval of the provisioning operation");

            let registration_token =
                match complete_operation(&mut provisioning, &init_response, signals_src) {
                    Ok(registration_token) => registration_token,
                    Err(ErrorAction::Retry(e)) => {
                        log::warn!("{e}");
                        continue;
                    }
                    Err(ErrorAction::Fail(e)) => {
                        log::error!("{e}");
                        return Err(e);
                    }
                };

            log::debug!("Provisioning operation approved, performing registration");

            let registration_response =
                match register_device(instance_url, &registration_token, signals_src) {
                    Ok(response) => response,
                    Err(ErrorAction::Retry(e)) => {
                        log::warn!("{e}");
                        continue;
                    }
                    Err(ErrorAction::Fail(e)) => {
                        log::error!("{e}");
                        return Err(e);
                    }
                };

            log::info!("Device Provisioning was successfully completed");

            return Ok((registration_token, registration_response));
        }
    }
}

fn register_if_connected(
    db_registration_token: &RegistrationToken,
    instance_url: &Uri,
) -> (bool, Option<RegistrationResponse>) {
    match cloud::drs::register(instance_url, db_registration_token) {
        Ok(response) => (true, Some(response)),
        Err(RegistrationError::InvalidRegistrationToken) => (false, None),
        Err(RegistrationError::WorkspaceDisabled) => {
            log::warn!(
                "Unable to check the Registration Token validity because the Workspace is disabled. \
                Expecting the Registration Token to be valid based on its expiration time.");
            (true, None)
        }
        Err(RegistrationError::Other(e)) => {
            // We don't want to force another device provisioning just because the Device is temporarily disconnected from the Internet
            // or there's another transient error.
            log::warn!(
                "An attempt to check the Registration Token validity failed because of a different reason than the validity itself. \
                Expecting the Registration Token to be valid based on its expiration time. \
                Error: {e}");
            (true, None)
        }
    }
}

fn init_operation(
    provisioning: &mut Provisioning,
    signals_src: &dyn ProcessSignalsSource,
) -> Result<InitProvisioningResponse> {
    let init_response = loop {
        let init_response = provisioning.init();
        match init_response {
            Ok(init_response) => break init_response,
            Err(InitProvisioningError::InvalidProvisioningToken) => {
                bail!(
                    "Unable to initiate a Provisioning Operation: Invalid Provisioning Token. \
                    Check that your Provisioning Token is valid and that you're connecting to the right Platform instance \
                    (the current instance URL: '{}').",
                    provisioning.instance_url())
            }
            Err(InitProvisioningError::OtherNonRecoverable(message)) => {
                bail!(
                    "Unable to initiate a Provisioning Operation due to a non-recoverable error: {}",
                    message.as_deref().unwrap_or("Unknown error"))
            }
            Err(e) => {
                log::warn!("An attempt to initiate provisioning operation failed: {e}");

                signals_src.check_signals()?;
                std::thread::sleep(std::time::Duration::from_millis(5000));
                signals_src.check_signals()?;
            }
        }
    };

    Ok(init_response)
}

fn display_operation_details(
    provisioning_operation: &ProvisioningOperation,
    callback: &Option<Box<dyn ProvisioningOperationDisplayHandler>>,
) -> Result<(), anyhow::Error> {
    if let Some(handler) = callback {
        handler.display(provisioning_operation).map_err(|e| {
            anyhow!("Error when calling custom callback to display provisioning operation: {e}")
        })?;
    } else {
        println!("Provisioning operation initialized, waiting for approval.");
        println!("Operation ID: {}", provisioning_operation.id);
        println!(
            "Verification Code: {}",
            provisioning_operation.verification_code
        );
    }

    Ok(())
}

fn complete_operation(
    provisioning: &mut Provisioning,
    init_response: &InitProvisioningResponse,
    signals_src: &dyn ProcessSignalsSource,
) -> Result<RegistrationToken, ErrorAction<anyhow::Error>> {
    loop {
        match provisioning.complete(&init_response.provisioning_operation_id) {
            Ok(registration_token) => {
                return Ok(registration_token);
            }
            Err(CompletionError::Closed(ProvisioningOperationClosedReason::Cancelled)) => {
                return Err(ErrorAction::Fail(anyhow!(
                    "The Provisioning Operation {:?} was cancelled. Try connecting again and make sure to approve the operation.",
                    &init_response.provisioning_operation_id
                )));
            }
            Err(CompletionError::Closed(ProvisioningOperationClosedReason::Other)) => {
                return Err(ErrorAction::Retry(anyhow!(
                    "The Provisioning Operation {:?} was closed, but not cancelled. Retrying Device Provisioning.",
                    &init_response.provisioning_operation_id)));
            }
            _ => {}
        }

        signals_src.check_signals().map_err(ErrorAction::Fail)?;

        std::thread::sleep(std::time::Duration::from_millis(5000));

        signals_src.check_signals().map_err(ErrorAction::Fail)?;
    }
}

fn register_device(
    instance_url: &Uri,
    registration_token: &RegistrationToken,
    signals_src: &dyn ProcessSignalsSource,
) -> Result<RegistrationResponse, ErrorAction<anyhow::Error>> {
    loop {
        match cloud::drs::register(instance_url, registration_token) {
            Ok(response) => {
                return Ok(response);
            }
            Err(RegistrationError::InvalidRegistrationToken) => {
                return Err(ErrorAction::Retry(anyhow!(
                    "The Registration Token is invalid. Retrying Device Provisioning."
                )));
            }
            Err(RegistrationError::WorkspaceDisabled) => {
                log::warn!("An attempt to register the Device failed because the Workspace is disabled, retrying.");
            }
            Err(RegistrationError::Other(e)) => {
                log::warn!("An attempt to register the Device failed, retrying. Error: {e}");
            }
        }

        signals_src.check_signals().map_err(ErrorAction::Fail)?;

        std::thread::sleep(std::time::Duration::from_millis(5000));

        signals_src.check_signals().map_err(ErrorAction::Fail)?;
    }
}

pub struct DeviceClientBuilderWithHandler<F> {
    builder: DeviceClientBuilder,
    method_handler: F,
}

impl<F> DeviceClientBuilderWithHandler<F>
where
    F: MethodHandler,
{
    /// **Warning**: Don't use, the interface for Cloud-to-Device Messages hasn't been finalized yet.
    #[deprecated]
    #[doc(hidden)]
    pub fn build(self) -> Result<DeviceClient> {
        self.builder.build_impl(Some(self.method_handler))
    }
}
