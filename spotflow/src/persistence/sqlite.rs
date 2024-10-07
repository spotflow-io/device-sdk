use anyhow::{bail, Context, Result};
use chrono::{DateTime, Utc};
use http::Uri;
use log::{debug, warn};
use sqlx::{Connection, Row, SqliteConnection};
use std::{fs::File, path::Path, str::FromStr, sync::Arc};
use tokio::sync::{Mutex, MutexGuard};

use super::{
    CloseOption, Compression,
    {twins::Twin, DeviceMessage},
    {ProvisioningToken, RegistrationToken},
};

const DB_VERSION: &str = "1.2.0";

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error(transparent)]
    Sqlite(#[from] sqlx::Error),
    #[error(transparent)]
    Io(#[from] std::io::Error),
}

#[derive(Debug, Clone)]
pub struct SqliteStore {
    conn: Arc<Mutex<SqliteConnection>>,
}

pub struct SdkConfiguration {
    pub instance_url: Uri,
    pub provisioning_token: ProvisioningToken,
    pub registration_token: RegistrationToken,
    pub requested_device_id: Option<String>,
    pub workspace_id: String,
    pub device_id: String,
    pub site_id: Option<String>,
}

#[derive(Default)]
pub struct SdkConfigurationFragment {
    #[allow(dead_code)]
    pub instance_url: Option<Uri>,
    pub provisioning_token: Option<ProvisioningToken>,
    pub registration_token: Option<RegistrationToken>,
    pub requested_device_id: Option<String>,
    pub workspace_id: Option<String>,
    pub device_id: Option<String>,
}

struct MigrationRequiredValues<'a> {
    device_id: &'a str,
    workspace_id: &'a str,
}

impl SqliteStore {
    pub(crate) async fn connection(&self) -> MutexGuard<'_, SqliteConnection> {
        self.conn.lock().await
    }

    pub async fn load_available_configuration(path: &Path) -> SdkConfigurationFragment {
        if !path.exists() {
            debug!(
                "The local database file on the path '{}' doesn't exist yet.",
                path.to_string_lossy(),
            );

            return SdkConfigurationFragment::default();
        }

        debug!(
            "Loading configuration from the local database file on the path '{}'",
            path.to_string_lossy()
        );

        match try_load_available_configuration(path).await {
            Ok(fragment) => fragment,
            Err(e) => {
                warn!(
                    "Loading configuration from the local database file on the path '{}' was skipped \
                    because of the following error: {e}",
                    path.to_string_lossy(),
                );
                SdkConfigurationFragment::default()
            }
        }
    }

    // Setup
    // ================================================================================
    pub async fn init(path: &Path, config: &SdkConfiguration) -> Result<SqliteStore> {
        if !Path::new(path).exists() {
            log::debug!("Creating a local database file");
            File::create(path)?;
        }
        // let mut conn = SqliteConnection::connect(&path.as_os_str().to_string_lossy()).await?;
        let conn = SqliteConnection::connect(&path.as_os_str().to_string_lossy()).await;
        let mut conn = match conn {
            Ok(conn) => {
                log::debug!("Connection to SQLite established");
                conn
            }
            Err(e) => {
                log::error!("
                    Unable to connect to SQLite in file `{path:?}`. \
                    Make sure that the current process can read from the file and write to it, and that no other process accesses the file. \
                    Error details: {e:?}");
                return Err(e.into());
            }
        };

        log::debug!("Getting database version");
        let record = sqlx::query!(r#"SELECT db_version FROM SdkConfiguration WHERE id = "0""#)
            .fetch_one(&mut conn)
            .await;

        if let Ok(record) = record {
            log::debug!(
                "The database contains schema in version {}",
                record.db_version
            );
            // The database already exists and is set up
            // Update the schema if necessary, bail if the version is unknown
            if record.db_version != DB_VERSION {
                let migration_values = MigrationRequiredValues {
                    device_id: config.device_id.as_str(),
                    workspace_id: config.workspace_id.as_str(),
                };

                try_update_version(&mut conn, &record.db_version, &migration_values).await?;
            }
        } else {
            log::debug!("Importing schema");
            sqlx::query_file!("./db_init.sql")
                .execute(&mut conn)
                .await?;
        }

        // In any case update the configuration with the provided values

        let instance_url = config.instance_url.to_string();

        log::debug!("Saving configuration");
        sqlx::query!(
            "INSERT OR REPLACE INTO SdkConfiguration (id, db_version, instance_url, provisioning_token, registration_token, rt_expiration, requested_device_id, workspace_id, device_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            0i64,
            DB_VERSION,
            instance_url,
            config.provisioning_token.token,
            config.registration_token.token,
            config.registration_token.expiration,
            config.requested_device_id,
            config.workspace_id,
            config.device_id,
        ).execute(&mut conn)
        .await?;
        log::debug!("Configuration saved");

        Ok(SqliteStore {
            conn: Arc::new(Mutex::new(conn)),
        })
    }

    // Device to Cloud Messages
    // ================================================================================
    pub async fn store_message(&self, msg: &DeviceMessage) -> Result<i32> {
        let mut conn = self.conn.lock().await;
        let record = sqlx::query!(
            r#"INSERT INTO Messages (site_id, stream_group, stream, batch_id, message_id, content, close_option, compression, batch_slice_id, chunk_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
            SELECT last_insert_rowid() as id"#,
            msg.site_id,
            msg.stream_group,
            msg.stream,
            msg.batch_id,
            msg.message_id,
            msg.content,
            msg.close_option as _,
            msg.compression as _,
            msg.batch_slice_id,
            msg.chunk_id,
        ).fetch_one(&mut *conn).await?;

        Ok(record.id)
    }

    pub(crate) async fn list_messages_after(&self, after: i32) -> Result<Vec<DeviceMessage>> {
        let mut conn = self.conn.lock().await;

        sqlx::query_as!(
            DeviceMessage,
            r#"SELECT id AS "id?: i32", site_id, stream_group, stream, batch_id, message_id, content, close_option AS "close_option!: CloseOption", compression AS "compression!: Compression", batch_slice_id, chunk_id FROM Messages WHERE id > ? ORDER BY id LIMIT 100"#, after,
        ).fetch_all(&mut *conn).await.map_err(anyhow::Error::from)
    }

    pub async fn message_count(&self) -> Result<usize> {
        let mut conn = self.conn.lock().await;
        let res = sqlx::query!("SELECT COUNT(id) as cnt FROM Messages")
            .fetch_one(&mut *conn)
            .await?;

        // This is safe because the result cannot be negative.
        Ok(res.cnt.try_into().unwrap_or_default())
    }

    pub async fn remove_oldest_message(&self) -> Result<()> {
        let mut conn = self.conn.lock().await;
        sqlx::query!(
            "DELETE FROM Messages WHERE id = (SELECT id FROM Messages ORDER BY id LIMIT 1)"
        )
        .execute(&mut *conn)
        .await?;

        Ok(())
    }

    // Twins
    // ================================================================================
    pub async fn load_desired_properties(&self) -> Result<Option<Twin>> {
        self.load_twin_properties("desired").await
    }

    pub async fn load_reported_properties(&self) -> Result<Option<Twin>> {
        self.load_twin_properties("reported").await
    }

    pub async fn save_desired_properties(&self, twin: &Twin) -> Result<()> {
        self.save_twin_properties("desired", twin).await
    }

    pub async fn save_reported_properties(&self, twin: &Twin) -> Result<()> {
        self.save_twin_properties("reported", twin).await
    }

    async fn load_twin_properties(&self, twin_type: &str) -> Result<Option<Twin>> {
        let mut conn = self.conn.lock().await;
        let res = sqlx::query!(
            r#"SELECT properties FROM Twins WHERE type = ? ORDER BY id DESC LIMIT 1"#,
            twin_type,
        )
        .fetch_optional(&mut *conn)
        .await
        .context("Unable to load twin")?;

        let Some(res) = res else {
            return Ok(None);
        };

        Ok(Some(
            serde_json::from_str(&res.properties).context("Unable to deserialize stored twin")?,
        ))
    }

    async fn save_twin_properties(&self, twin_type: &str, twin: &Twin) -> Result<()> {
        let mut conn = self.conn.lock().await;
        let json = serde_json::to_string(twin).context("Unable to deserialize twin")?;
        sqlx::query!(
            r#"INSERT INTO Twins (type, properties) VALUES (?, ?);"#,
            twin_type,
            json,
        )
        .execute(&mut *conn)
        .await
        .context(format!("Unable to save twin {twin_type} properties"))
        .map(|_| ())
    }

    // Configuration & Tokens
    // ================================================================================
    pub async fn load_requested_device_id(&self) -> Result<Option<String>> {
        let mut conn = self.conn.lock().await;
        Ok(
            sqlx::query!(r#"SELECT requested_device_id FROM SdkConfiguration WHERE id = "0""#,)
                .fetch_one(&mut *conn)
                .await
                .context("Unable to load device ID from configuration")?
                .requested_device_id,
        )
    }

    pub async fn save_workspace_id(&self, workspace_id: &str) -> Result<()> {
        let mut conn = self.conn.lock().await;
        sqlx::query!(
            r#"UPDATE SdkConfiguration SET workspace_id = ? WHERE id = "0""#,
            workspace_id,
        )
        .execute(&mut *conn)
        .await
        .context("Unable to save Workspace ID to configuration")?;

        Ok(())
    }

    pub async fn load_workspace_id(&self) -> Result<String> {
        let mut conn = self.conn.lock().await;
        Ok(
            sqlx::query!(r#"SELECT workspace_id FROM SdkConfiguration WHERE id = "0""#,)
                .fetch_one(&mut *conn)
                .await
                .context("Unable to load workspace ID from configuration")?
                .workspace_id,
        )
    }

    pub async fn save_device_id(&self, device_id: &str) -> Result<()> {
        let mut conn = self.conn.lock().await;
        sqlx::query!(
            r#"UPDATE SdkConfiguration SET device_id = ? WHERE id = "0""#,
            device_id,
        )
        .execute(&mut *conn)
        .await
        .context("Unable to save Device ID to configuration")?;

        Ok(())
    }

    pub async fn load_device_id(&self) -> Result<String> {
        let mut conn = self.conn.lock().await;
        Ok(
            sqlx::query!(r#"SELECT device_id FROM SdkConfiguration WHERE id = "0""#,)
                .fetch_one(&mut *conn)
                .await
                .context("Unable to load device ID from configuration")?
                .device_id,
        )
    }

    pub async fn load_provisioning_token(&self) -> Result<ProvisioningToken> {
        let mut conn = self.conn.lock().await;
        sqlx::query_as!(
            ProvisioningToken,
            r#"SELECT provisioning_token AS token FROM SdkConfiguration WHERE id = "0""#,
        )
        .fetch_one(&mut *conn)
        .await
        .context("Unable to load provisioning token from configuration")
    }

    pub async fn save_provisioning_token(&self, token: &ProvisioningToken) -> Result<()> {
        log::debug!("Saving provisioning token");
        let mut conn = self.conn.lock().await;
        sqlx::query!(
            r#"UPDATE SdkConfiguration SET provisioning_token = ? WHERE id = "0""#,
            token.token,
        )
        .execute(&mut *conn)
        .await?;

        Ok(())
    }

    pub async fn load_registration_token(&self) -> Result<RegistrationToken> {
        let mut conn = self.conn.lock().await;
        sqlx::query_as!(
            RegistrationToken,
            r#"SELECT registration_token AS token, rt_expiration AS "expiration: DateTime<Utc>" FROM SdkConfiguration WHERE id = "0""#,
        )
        .fetch_one(&mut *conn)
        .await
        .context("Unable to load registration token from configuration")
    }

    pub async fn save_registration_token(&self, token: &RegistrationToken) -> Result<()> {
        log::debug!(
            "Saving registration token with expiration {:?}",
            token.expiration
        );
        let mut conn = self.conn.lock().await;
        sqlx::query!(
            r#"UPDATE SdkConfiguration SET registration_token = ?, rt_expiration = ? WHERE id = "0""#,
            token.token,
            token.expiration,
        )
        .execute(&mut *conn)
        .await?;

        Ok(())
    }

    pub(crate) async fn load_instance_url(&self) -> Result<String> {
        let mut conn = self.conn.lock().await;
        let record = sqlx::query!(r#"SELECT instance_url FROM SdkConfiguration WHERE id = "0""#)
            .fetch_one(&mut *conn)
            .await?;

        Ok(record.instance_url)
    }
}

async fn try_load_available_configuration(path: &Path) -> Result<SdkConfigurationFragment> {
    let mut conn = SqliteConnection::connect(&path.as_os_str().to_string_lossy()).await?;

    let row = load_configuration_row(&mut conn).await?;

    let db_version: String = row.try_get("db_version")?;

    let instance_url = match row.try_get::<String, _>("instance_url") {
        Ok(instance_url) => Some(Uri::from_str(&instance_url)?),
        Err(_) => match row.try_get::<String, _>("dps_url") {
            Ok(dps_url) => {
                let instance_url = convert_dps_url_to_instance_url(&dps_url)?;
                Some(Uri::from_str(&instance_url)?)
            }
            Err(_) => None,
        },
    };

    if let Some(instance_url) = &instance_url {
        log::debug!(
            "Loaded existing instance URL '{}' from the local database file.",
            instance_url
        );
    }

    let provisioning_token = match row.try_get::<String, _>("provisioning_token") {
        Ok(token) => {
            log::debug!("Loaded existing provisioning token from the local database file.");

            Some(ProvisioningToken { token })
        }
        Err(_) => None,
    };

    let registration_token = match row.try_get::<String, _>("registration_token") {
        Ok(token) => {
            log::debug!("Loaded existing registration token from the local database file.");

            let expiration: Option<DateTime<Utc>> = row.try_get("rt_expiration").ok();
            Some(RegistrationToken { token, expiration })
        }
        Err(_) => None,
    };

    let requested_device_id = row.try_get::<String, _>("requested_device_id").ok();

    if let Some(requested_device_id) = &requested_device_id {
        log::debug!(
            "Loaded existing last requested device ID '{}' from the local database file.",
            requested_device_id
        );
    }

    let (workspace_id, device_id) = match db_version.as_str() {
        "0.1.3" | "1.0.1" | "1.1.0" => {
            let iot_hub_device_id: Option<String> = row.try_get("device_id")?;
            match iot_hub_device_id {
                Some(iot_hub_device_id) => {
                    match convert_iothub_device_id_to_workspace_id_and_device_id(
                        iot_hub_device_id.as_str(),
                    ) {
                        Ok((workspace_id, device_id)) => (Some(workspace_id), Some(device_id)),
                        Err(_) => (None, None),
                    }
                }
                None => (None, None),
            }
        }
        _ => {
            let workspace_id = row.try_get::<String, _>("workspace_id").ok();
            let device_id = row.try_get::<String, _>("device_id").ok();
            (workspace_id, device_id)
        }
    };

    if let Some(workspace_id) = &workspace_id {
        log::debug!(
            "Loaded existing Workspace ID '{}' from the local database file.",
            workspace_id
        );
    }
    if let Some(device_id) = &device_id {
        log::debug!(
            "Loaded existing Device ID '{}' from the local database file.",
            device_id
        );
    }

    Ok(SdkConfigurationFragment {
        instance_url,
        provisioning_token,
        registration_token,
        requested_device_id,
        workspace_id,
        device_id,
    })
}

async fn try_update_version(
    conn: &mut SqliteConnection,
    db_version: &str,
    values: &MigrationRequiredValues<'_>,
) -> Result<()> {
    if db_version != DB_VERSION {
        let mut current_db_version = db_version;
        let mut known_version = false;

        if current_db_version == "0.1.3" {
            known_version = true;
            update_version_to_1_0_1(conn).await?;
            current_db_version = "1.0.1";
        }

        if current_db_version == "1.0.1" {
            known_version = true;
            update_version_to_1_1_0(conn).await?;
            current_db_version = "1.1.0";
        }

        if current_db_version == "1.1.0" {
            known_version = true;
            update_version_to_1_2_0(conn, values).await?;
        }

        if !known_version {
            bail!(
                "Unknown version {} of the local database file. Make sure that you're using the latest version of the Device SDK.",
                db_version
            );
        }
    }

    Ok(())
}

async fn update_version_to_1_0_1(conn: &mut SqliteConnection) -> Result<(), anyhow::Error> {
    log::debug!("Updating database schema from version 0.1.3 to 1.0.1");

    sqlx::query(
        r#"BEGIN TRANSACTION;
        ALTER TABLE SdkConfiguration ADD requested_device_id TEXT;
        UPDATE SdkConfiguration SET db_version = '1.0.1' WHERE id = "0";
        COMMIT"#,
    )
    .execute(conn)
    .await?;

    log::debug!("Database schema updated to version 1.0.1");
    Ok(())
}

async fn update_version_to_1_1_0(conn: &mut SqliteConnection) -> Result<(), anyhow::Error> {
    async fn do_columns_exist(conn: &mut SqliteConnection) -> Result<bool> {
        let res = sqlx::query_scalar!(
            r#"SELECT COUNT(*)
            FROM pragma_table_info('Messages')
            WHERE name = 'batch_slice_id' OR name = 'chunk_id'"#
        )
        .fetch_one(conn)
        .await?;

        Ok(res == 2)
    }

    log::debug!("Updating database schema from version 1.0.1 to 1.1.0");

    // There was an error in the code causing schema of version 1.1.0 to be marked 1.0.1, so we need to check if the
    // columns don't already exist
    let query = if do_columns_exist(conn).await? {
        sqlx::query(r#"UPDATE SdkConfiguration SET db_version = '1.1.0' WHERE id = "0";"#)
    } else {
        sqlx::query(
            r#"BEGIN TRANSACTION;
            ALTER TABLE Messages ADD batch_slice_id TEXT;
            ALTER TABLE Messages ADD chunk_id TEXT;
            UPDATE SdkConfiguration SET db_version = '1.1.0' WHERE id = "0";
            COMMIT"#,
        )
    };

    query.execute(conn).await?;

    log::debug!("Database schema updated to version 1.1.0");
    Ok(())
}

async fn update_version_to_1_2_0(
    conn: &mut SqliteConnection,
    values: &MigrationRequiredValues<'_>,
) -> Result<(), anyhow::Error> {
    log::debug!("Updating database schema from version 1.1.0 to 1.2.0");

    let configuration_row = load_configuration_row(conn).await?;

    let dps_url: String = configuration_row.try_get("dps_url")?;
    let instance_url = convert_dps_url_to_instance_url(&dps_url)?;

    sqlx::query(
        r#"BEGIN TRANSACTION;

                UPDATE SdkConfiguration SET device_id = ? WHERE id = "0";
                UPDATE SdkConfiguration SET db_version = '1.2.0' WHERE id = "0";

                CREATE TABLE SdkConfiguration_new (
                    id                  INTEGER PRIMARY KEY,
                    db_version          TEXT NOT NULL,
                    instance_url        TEXT NOT NULL,
                    provisioning_token  TEXT NOT NULL,
                    registration_token  TEXT NOT NULL,
                    rt_expiration       TEXT, -- DATETIME
                    requested_device_id TEXT,
                    workspace_id        TEXT NOT NULL,
                    device_id           TEXT NOT NULL
                ) STRICT;

                INSERT INTO SdkConfiguration_new
                    SELECT id, db_version, ?, provisioning_token, registration_token, rt_expiration, requested_device_id, ?, device_id
                    FROM SdkConfiguration;
                DROP TABLE SdkConfiguration;
                ALTER TABLE SdkConfiguration_new RENAME TO SdkConfiguration;

                COMMIT"#,
    )
    .bind(values.device_id)
    .bind(&instance_url)
    .bind(values.workspace_id)
    .execute(conn)
    .await?;

    log::debug!("Database schema updated to version 1.2.0");
    Ok(())
}

async fn load_configuration_row(
    conn: &mut SqliteConnection,
) -> Result<sqlx::sqlite::SqliteRow, anyhow::Error> {
    let row = sqlx::query(r#"SELECT * FROM SdkConfiguration WHERE id = "0""#)
        .fetch_one(conn)
        .await?;
    Ok(row)
}

fn convert_iothub_device_id_to_workspace_id_and_device_id(
    iot_hub_device_id: &str,
) -> Result<(String, String), anyhow::Error> {
    // The column device_id in previous schema versions contains in fact "{workspace_id}:{device_id}"
    let mut parts = iot_hub_device_id.splitn(2, ':');
    let workspace_id = parts.next().ok_or_else(|| {
        anyhow::anyhow!("Invalid Device ID stored in local database: '{iot_hub_device_id}'.")
    })?;
    let device_id = parts.next().ok_or_else(|| {
        anyhow::anyhow!("Invalid Device ID stored in local database: '{iot_hub_device_id}'.")
    })?;
    Ok((workspace_id.to_owned(), device_id.to_owned()))
}

fn convert_dps_url_to_instance_url(dps_url: &str) -> Result<String, anyhow::Error> {
    let dps_url = dps_url.parse::<Uri>().map_err(|e| {
        anyhow::anyhow!("The URL '{dps_url}' stored in the column 'dps_url' is invalid: {e:?}")
    })?;

    let dps_host = dps_url.host().ok_or_else(|| {
        anyhow::anyhow!(
            "The URL '{dps_url}' stored in the column 'dps_url' doesn't contain a host."
        )
    })?;

    // Individual services used to be hosted on separate domains
    let instance_host = dps_host.trim_start_matches("device-provisioning.");

    let instance_uri = format!("https://{instance_host}/");

    Ok(instance_uri)
}
