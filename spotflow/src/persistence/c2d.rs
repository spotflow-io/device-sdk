use anyhow::Result;
use async_trait::async_trait;
use sqlx::{Connection, SqliteConnection};

use super::{sqlite_channel::Storable, CloudToDeviceMessage};

#[derive(Debug)]
struct CloudToDeviceMessageDb {
    id: Option<i32>,
    content: Vec<u8>,
}

#[async_trait]
impl Storable for CloudToDeviceMessage {
    fn id(&self) -> i32 {
        // This is only ever called on a retrieved object which has to have an ID
        self.id
            .expect("id must not be called on objects without one")
    }

    async fn store(&self, conn: &mut SqliteConnection) -> Result<i32> {
        let mut transaction = conn.begin().await?;

        let record = sqlx::query!(
            r#"INSERT INTO CloudToDeviceMessages (content) VALUES (?);
            SELECT last_insert_rowid() as id"#,
            self.content,
        )
        .fetch_one(&mut *transaction)
        .await?;

        log::debug!("Saved C2D message with ID {}", record.id);

        for (k, v) in &self.properties {
            sqlx::query!(
                r#"INSERT INTO CloudToDeviceProperties (message_id, key, value) VALUES (?, ?, ?);"#,
                record.id,
                k,
                v,
            )
            .execute(&mut *transaction)
            .await?;
        }

        transaction.commit().await?;

        Ok(record.id)
    }

    async fn load(conn: &mut SqliteConnection, minimum_id: i32) -> Result<Option<Self>> {
        let res = match sqlx::query_as!(
            CloudToDeviceMessageDb,
            r#"SELECT id AS "id?: i32", content FROM CloudToDeviceMessages WHERE id > ? ORDER BY id LIMIT 1"#,
            minimum_id,
        ).fetch_one(&mut *conn).await {
            Err(sqlx::Error::RowNotFound) => return Ok(None),
            Err(e) => return Err(anyhow::Error::from(e)),
            Ok(row) => row,
        };
        let properties = sqlx::query!(
            "SELECT message_id, key, value FROM CloudToDeviceProperties WHERE message_id = ?",
            res.id,
        )
        .fetch_all(conn)
        .await?;

        let properties = properties.into_iter().map(|r| (r.key, r.value)).collect();

        Ok(Some(CloudToDeviceMessage {
            id: res.id,
            content: res.content,
            properties,
        }))
    }

    async fn remove(conn: &mut SqliteConnection, id: i32) -> Result<()> {
        sqlx::query!(
            "DELETE FROM CloudToDeviceProperties WHERE message_id = ?;
            DELETE FROM CloudToDeviceMessages WHERE id = ?",
            id,
            id,
        )
        .execute(conn)
        .await?;
        Ok(())
    }

    async fn count(conn: &mut SqliteConnection) -> Result<usize> {
        let res = sqlx::query!("SELECT COUNT(id) as cnt FROM CloudToDeviceMessages")
            .fetch_one(conn)
            .await?;

        // `cnt` cannot be negative so this is safe.
        Ok(res.cnt.try_into().unwrap_or_default())
    }
}
