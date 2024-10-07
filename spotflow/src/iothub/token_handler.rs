use std::time::{Duration, Instant};

use anyhow::{anyhow, Result};
use chrono::{DateTime, Utc};
use http::Uri;
use tokio::select;
use tokio::sync::{mpsc, watch};

use crate::cloud::dps::{self, ProvisioningToken, RegistrationToken};
use crate::cloud::drs::{self, RegistrationResponse};
use crate::persistence::ConfigurationStore;

pub(crate) type RegistrationWatch = watch::Receiver<Option<RegistrationResponse>>;
pub(crate) type RegistrationCommandSender = mpsc::UnboundedSender<RegistrationCommand>;

pub enum RegistrationCommand {
    RefreshRegistrationToken { time: Instant },
    RefreshRegistration { time: Instant },
}

#[derive(Debug)]
pub struct TokenHandler {
    instance_url: Uri,
    tokens: TokenCache,
    store: ConfigurationStore,
    registration_sender: watch::Sender<Option<RegistrationResponse>>,
    command_sender: RegistrationCommandSender,
    command_receiver: mpsc::UnboundedReceiver<RegistrationCommand>,
    last_token_refresh_attempt: Instant,
    last_registration_refresh_attempt: Instant,
}

#[derive(Debug)]
struct TokenCache {
    provisioning_token: ProvisioningToken,
    registration_token: RegistrationToken,
    iothub_sas_token: Option<ConnectionToken>,
}

#[derive(Debug)]
struct ConnectionToken {
    // This struct does not need the token itself so it just keeps the validity period
    valid_until: DateTime<Utc>,
}

impl TokenHandler {
    pub async fn init(
        instance_url: Uri,
        provisioning_token: ProvisioningToken,
        registration_token: RegistrationToken,
        store: ConfigurationStore,
        initial_registration_response: Option<RegistrationResponse>,
    ) -> Result<(RegistrationWatch, RegistrationCommandSender)> {
        store.save_provisioning_token(&provisioning_token).await?;
        store.save_registration_token(&registration_token).await?;

        let cache = TokenCache {
            provisioning_token,
            registration_token: RegistrationToken {
                token: registration_token.token,
                // This usually means that there is no expiration. Here it means unknown expiration.
                // This is sane because the token will have to be used for registration and we can (and must!) update the expiration then.
                expiration: None,
            },
            iothub_sas_token: None,
        };

        let (registration_sender, registration_receiver) = watch::channel(None);
        let (command_sender, command_receiver) = mpsc::unbounded_channel();

        let handler = TokenHandler {
            instance_url,
            tokens: cache,
            store,
            registration_sender,
            command_sender: command_sender.clone(),
            command_receiver,
            last_token_refresh_attempt: Instant::now(),
            last_registration_refresh_attempt: Instant::now(),
        };

        tokio::spawn(async {
            handler.refresh_tokens(initial_registration_response).await;
        });

        Ok((registration_receiver, command_sender))
    }

    async fn refresh_tokens(mut self, initial_registration_response: Option<RegistrationResponse>) {
        // If there is an existing registration response (e.g., from Device Provisioning), use it. Otherwise, register.
        let mut registration_response = match initial_registration_response {
            Some(registration_response) => Ok(registration_response),
            None => drs::register(&self.instance_url, &self.tokens.registration_token),
        };

        // Repeat registration attempts until it succeeds
        loop {
            let processing_result = match registration_response {
                Ok(registration_response) => {
                    self.process_registration_response(registration_response)
                        .await
                }
                Err(e) => Err(anyhow!(e)),
            };

            match processing_result {
                Ok(()) => break,
                Err(e) => {
                    log::warn!("First registration has failed, waiting for 30 seconds and trying again. Error: {e:?}");
                    tokio::time::sleep(Duration::from_secs(30)).await;

                    registration_response =
                        drs::register(&self.instance_url, &self.tokens.registration_token);
                }
            }
        }

        loop {
            // Add next commands to the queue according to the expiration of the tokens

            let instant_now = Instant::now();
            let utc_now = Utc::now();

            let sas_expiry = &self
                .tokens
                .iothub_sas_token
                .as_ref()
                .expect("Cannot refresh IoT Hub SAS token before there is one")
                .valid_until;
            if sas_expiry <= &utc_now {
                if let Err(e) = self
                    .command_sender
                    .send(RegistrationCommand::RefreshRegistration { time: instant_now })
                {
                    log::warn!("Unable to send refresh registration command: {:?}", e);
                }
            }

            let registration_token_expiry = &self
                .tokens
                .registration_token
                .expiration
                .unwrap_or(DateTime::<Utc>::MAX_UTC);
            if registration_token_expiry <= &utc_now {
                if let Err(e) = self
                    .command_sender
                    .send(RegistrationCommand::RefreshRegistrationToken { time: instant_now })
                {
                    log::warn!("Unable to send refresh registration token command: {:?}", e);
                }
            }

            // Wait until the next command is received or it is time to check token expiration time again
            // (we check it periodically to ensure that we do not miss the expiration even if the device is in sleep mode)
            select! {
                () = tokio::time::sleep(Duration::from_secs(60)) => {}
                Some(command) = self.command_receiver.recv() => self.process_command(command).await
            }

            // Process the commands in the queue
            while let Ok(command) = self.command_receiver.try_recv() {
                self.process_command(command).await;
            }
        }
    }

    async fn process_registration_response(
        &mut self,
        registration_response: RegistrationResponse,
    ) -> Result<()> {
        // Get the time of expiration of the registration token. If none was provided it does not expire. It gets lowered a bit to account for clockskew
        let registration_token_expiry = registration_response.token_remaining_lifetime.map(|t| {
            Self::expect_clockskew(
                Utc::now()
                    + chrono::Duration::from_std(t.into())
                        .unwrap_or_else(|_| chrono::Duration::max_value()),
            )
        });

        let sas_expiry = registration_response
            .connection_string_expiration
            .expect("The registration did not return SAS token");

        log::debug!("Registration token expires at {registration_token_expiry:?}");
        log::debug!("SAS token expires at {sas_expiry}");

        self.tokens.iothub_sas_token = Some(ConnectionToken {
            valid_until: sas_expiry,
        });
        self.tokens.registration_token.expiration = registration_token_expiry;

        self.store
            .save_registration_token(&self.tokens.registration_token)
            .await?;

        let device_id = registration_response.device_id()?;
        self.store.save_device_id(device_id).await?;

        let workspace_id = registration_response.workspace_id()?;
        self.store.save_workspace_id(workspace_id).await?;

        log::info!(
            "Startup registration in the Workspace with ID {} done successfully with Device ID {}.",
            workspace_id,
            device_id
        );

        self.registration_sender
            .send_replace(Some(registration_response));

        Ok(())
    }

    async fn process_command(&mut self, command: RegistrationCommand) {
        match command {
            RegistrationCommand::RefreshRegistrationToken { time } => {
                if time >= self.last_token_refresh_attempt {
                    let result = self.try_refresh_token().await;
                    self.last_token_refresh_attempt = Instant::now();

                    if let Err(e) = result {
                        log::warn!("Unable to refresh registration token: {:?}", e);

                        // Ensure that there is enough pause between the attempts
                        tokio::time::sleep(Duration::from_secs(30)).await;

                        // Enqueue the command so that it is tried again next time
                        if let Err(e) = self.command_sender.send(
                            RegistrationCommand::RefreshRegistrationToken {
                                time: Instant::now(),
                            },
                        ) {
                            log::warn!(
                                "Unable to send refresh registration token command: {:?}",
                                e
                            );
                        }
                    }
                }
            }
            RegistrationCommand::RefreshRegistration { time } => {
                if time >= self.last_registration_refresh_attempt {
                    let result = self.try_refresh_registration();
                    self.last_registration_refresh_attempt = Instant::now();

                    if let Err(e) = result {
                        log::warn!("Failed registration: {:?}", e);

                        // Ensure that there is enough pause between the attempts
                        tokio::time::sleep(Duration::from_secs(30)).await;

                        // Enqueue the command so that it is tried again next time
                        if let Err(e) =
                            self.command_sender
                                .send(RegistrationCommand::RefreshRegistration {
                                    time: Instant::now(),
                                })
                        {
                            log::warn!("Unable to send refresh registration command: {:?}", e);
                        }
                    }
                }
            }
        }
    }

    fn try_refresh_registration(&mut self) -> Result<()> {
        log::info!("Refreshing registration to the platform");
        let registration = drs::register(&self.instance_url, &self.tokens.registration_token)?;
        self.tokens.iothub_sas_token = Some(ConnectionToken {
            valid_until: registration
                .connection_string_expiration
                .expect("IoT Hub SAS token must have expiration"),
        });

        // Replace does not return Err when no one is listening
        self.registration_sender.send_replace(Some(registration));

        log::info!("Registration refreshed successfully");

        Ok(())
    }

    async fn try_refresh_token(&mut self) -> Result<()> {
        log::info!("Refreshing registration token");
        let refresh = dps::refresh(
            &self.instance_url,
            &self.tokens.provisioning_token,
            &self.tokens.registration_token,
        )?;

        self.tokens.registration_token = RegistrationToken {
            token: refresh.token,
            expiration: refresh.expiration.map(Self::expect_clockskew),
        };

        self.store
            .save_registration_token(&self.tokens.registration_token)
            .await?;

        log::info!("Registration token refreshed successfully");

        Ok(())
    }

    fn expect_clockskew(expiration: DateTime<Utc>) -> DateTime<Utc> {
        let expiration_duration = expiration.signed_duration_since(Utc::now());
        let expiration_datetime = expiration - expiration_duration / 2;

        let minutes_25 = chrono::Duration::try_minutes(25).expect("Unreachable");
        let minutes_10 = chrono::Duration::try_minutes(10).expect("Unreachable");

        // Should be always true in production
        if expiration_duration > minutes_25 {
            return Ord::min(expiration - minutes_10, expiration_datetime);
        }

        expiration_datetime
    }
}
