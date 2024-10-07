use anyhow::{bail, Context, Result};
use async_trait::async_trait;
use json_patch::merge;
use serde::{Deserialize, Serialize};
use sqlx::SqliteConnection;

use super::sqlite_channel::Storable;

#[derive(Deserialize, Debug)]
pub struct Twins {
    pub reported: Twin,
    pub desired: Twin,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
pub struct Twin {
    #[serde(rename = "$version")]
    pub version: u64,
    #[serde(flatten)]
    pub properties: serde_json::Value,
}

impl Twin {
    pub fn update(&mut self, update: &TwinUpdate) -> Result<()> {
        match update.version {
            None => {
                log::debug!("Applying twin patch to automatically incremented version");
                merge(&mut self.properties, &update.patch);
                self.version += 1;
            }
            Some(new_version) => {
                if new_version <= self.version {
                    log::debug!(
                        "Ignoring twin patch to version {} because we are already at {}",
                        new_version,
                        self.version
                    );
                } else if new_version == self.version + 1 {
                    log::debug!("Applying twin patch to version {new_version}");
                    merge(&mut self.properties, &update.patch);
                    self.version = new_version;
                } else {
                    bail!("Ignoring unexpected patch of a twin. We have version {} and patch for {} which would skip updates", self.version, new_version);
                }
            }
        };

        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize)]
pub struct TwinUpdate {
    #[serde(rename = "$version")]
    pub version: Option<u64>,
    #[serde(flatten)]
    pub patch: serde_json::Value,
}

#[derive(Debug, Clone, sqlx::Type)]
pub enum ReportedPropertiesUpdateType {
    Full = 0,
    Patch = 1,
}

#[derive(Debug, Clone)]
pub struct ReportedPropertiesUpdate {
    pub id: Option<i32>,
    pub update_type: ReportedPropertiesUpdateType,
    pub patch: serde_json::Value,
}

#[derive(sqlx::FromRow)]
struct ReportedPropertiesUpdateDb {
    id: Option<i32>,
    update_type: ReportedPropertiesUpdateType,
    patch: String,
}

#[async_trait]
impl Storable for ReportedPropertiesUpdate {
    fn id(&self) -> i32 {
        // This is only ever called on a retrieved object which has to have an ID
        self.id
            .expect("id must not be called on objects without one")
    }

    async fn store(&self, conn: &mut SqliteConnection) -> Result<i32> {
        let patch = self.patch.to_string();
        let res = sqlx::query!(
            r#"INSERT INTO ReportedPropertiesUpdates (patch, update_type) VALUES (?, ?);
            SELECT last_insert_rowid() as id"#,
            patch,
            self.update_type,
        )
        .fetch_one(conn)
        .await
        .context("Unable to save reported properties update")?;

        Ok(res.id)
    }

    async fn load(conn: &mut SqliteConnection, id: i32) -> Result<Option<Self>> {
        let res = sqlx::query_as!(
            ReportedPropertiesUpdateDb,
            r#"SELECT id AS "id?: i32", patch, update_type AS "update_type: ReportedPropertiesUpdateType" FROM ReportedPropertiesUpdates WHERE id > ? ORDER BY id LIMIT 1"#,
            id,
        )
        .fetch_optional(conn)
        .await
        .context("Unable to load twin")?;

        let update = match res {
            None => None,
            Some(update) => {
                let patch = serde_json::from_str(&update.patch).context(format!(
                    "Malformed reported properties update with ID {:?}",
                    update.id
                ))?;
                Some(ReportedPropertiesUpdate {
                    id: update.id,
                    update_type: update.update_type,
                    patch,
                })
            }
        };

        Ok(update)
    }

    async fn remove(conn: &mut SqliteConnection, id: i32) -> Result<()> {
        sqlx::query!("DELETE FROM ReportedPropertiesUpdates WHERE id = ?", id,)
            .execute(conn)
            .await?;
        Ok(())
    }

    async fn count(conn: &mut SqliteConnection) -> Result<usize> {
        let res = sqlx::query!("SELECT count(id) AS count FROM ReportedPropertiesUpdates")
            .fetch_one(conn)
            .await?;

        // This is safe because the result cannot be negative
        Ok(res.count.try_into().unwrap_or_default())
    }
}
