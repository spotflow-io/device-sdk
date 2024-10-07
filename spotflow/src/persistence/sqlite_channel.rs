//! This channel works like multiple producer single consumer channel but additionally persists data into a SQLite database and provides separate acknowledge functionality.
//! Separate acknowledgment turns the channel delivery guarantees into at-least-once instead of at-most-once. This does NOT imply exactly-once so some data may be delivered multiple times.
//! Acknowledgement can be done either through `Receiver` or through a separate `Acknowledger`.
//! To create a channel use `channel`, `Sender` is `Clone` if multiple producers are needed.

// To use this channel for d2c messages we would need to implement acknowledge last. Or the callsite would have to considerably change

use std::{marker::PhantomData, sync::Arc};

use anyhow::{Context, Result};
use async_trait::async_trait;
use sqlx::SqliteConnection;
use tokio::{
    select,
    sync::{watch, Mutex},
};
use tokio_util::sync::CancellationToken;

use super::sqlite::SqliteStore;

// The implementation could be made much easier and less error-prone if we used an ORM but I don't want to do that right now because of other priorities and my lack of knowledge on that topic
//
/// This trait must be implemented by anything that wants to be sent/stored through the sqlite channel.
/// Each object has to have a unique ID which is returned by the store operation. The objects are guaranteed to be returned in an ascending order during the run by a given instance.
/// If the channel is closed any unacknowledged messages will be delivered again starting from the lowest ID.
/// The store operations must create IDs higher than the last received value. This is trivially done by returning ascending series by using SQLite AUTOINCREMENT.
#[async_trait]
pub trait Storable: Sized {
    fn id(&self) -> i32;
    async fn store(&self, conn: &mut SqliteConnection) -> Result<i32>;
    async fn load(conn: &mut SqliteConnection, minimum_id: i32) -> Result<Option<Self>>;
    async fn remove(conn: &mut SqliteConnection, id: i32) -> Result<()>;
    async fn count(conn: &mut SqliteConnection) -> Result<usize>;
}

pub fn channel<T: Storable>(store: SqliteStore) -> (Sender<T>, Receiver<T>) {
    let (watch_tx, watch_rx) = watch::channel(None);
    (
        Sender {
            store: store.clone(),
            last_saved: Arc::new(Mutex::new(watch_tx)),
            phantom: PhantomData,
        },
        Receiver {
            store,
            last_saved: watch_rx,
            last_received: None,
            phantom: PhantomData,
        },
    )
}

#[derive(Debug, Clone)]
pub struct Sender<T> {
    store: SqliteStore,
    last_saved: Arc<Mutex<watch::Sender<Option<i32>>>>,
    phantom: PhantomData<T>,
}

#[derive(Debug)]
pub struct Receiver<T> {
    store: SqliteStore,
    last_saved: watch::Receiver<Option<i32>>,
    last_received: Option<i32>,
    phantom: PhantomData<T>,
}

impl<T: Storable + Sync> Sender<T> {
    pub async fn send(&self, obj: &T) -> Result<()> {
        let mut conn = self.store.connection().await;
        let id = obj.store(&mut conn).await?;

        {
            let last_saved = self.last_saved.lock().await;
            let last_id = last_saved.send_replace(Some(id));
            // If the previously stored ID was higher put it back
            if let Some(last_id) = last_id {
                if last_id > id {
                    last_saved.send_replace(Some(last_id));
                }
            }
        }

        Ok(())
    }

    pub async fn count(&self) -> Result<usize> {
        let mut conn = self.store.connection().await;
        T::count(&mut conn).await
    }
}

impl<T: Storable + Send + Sync> Receiver<T> {
    pub async fn recv(&mut self, cancellation: &Option<CancellationToken>) -> Result<T> {
        let last_inserted = self.wait_new(cancellation).await?;

        let mut conn = self.store.connection().await;

        let obj = T::load(&mut conn, self.last_received.unwrap_or(i32::MIN))
            .await?
            .ok_or_else(|| {
                anyhow::anyhow!(
                    "Unable to retrieve object with ID {:?} that should have already been stored.",
                    last_inserted
                )
            })?;
        self.last_received = Some(obj.id());

        Ok(obj)
    }

    async fn wait_new(&mut self, cancellation: &Option<CancellationToken>) -> Result<i32> {
        let mut last_inserted = *self.last_saved.borrow_and_update();

        while last_inserted <= self.last_received {
            let change_task = self.last_saved.changed();

            let result = if let Some(cancellation) = cancellation {
                select! {
                    result = change_task => result,
                    () = cancellation.cancelled() => anyhow::bail!("Task cancelled."),
                }
            } else {
                change_task.await
            };

            result.context("No more messages will be received in this run")?;

            last_inserted = *self.last_saved.borrow_and_update();
        }

        // Since last_inserted must be strictly larger than last_received it cannot be None
        Ok(last_inserted.expect("Last inserted cannot be None."))
    }

    pub async fn ack(&self, obj: &T) -> Result<()> {
        let mut conn = self.store.connection().await;
        T::remove(&mut conn, obj.id()).await
    }

    pub async fn count(&self) -> Result<usize> {
        let mut conn = self.store.connection().await;
        T::count(&mut conn).await
    }
}
