use anyhow::{Context, Result};
use std::{panic::catch_unwind, sync::mpsc, thread::JoinHandle};

use crate::connection::twins::{DesiredProperties, DesiredPropertiesUpdatedCallback};

#[derive(Debug)]
pub struct DesiredPropertiesUpdatedCallbackDispatcher {
    sender: Option<mpsc::Sender<DesiredProperties>>,
    thread: Option<JoinHandle<()>>,
}

impl DesiredPropertiesUpdatedCallbackDispatcher {
    pub fn new(callback: Box<dyn DesiredPropertiesUpdatedCallback>) -> Self {
        let (sender, receiver) = mpsc::channel();

        log::debug!("Starting properties updated processing thread.");
        // This is thread and not a simple Tokio task because the user-provided callback can potentially block.
        // That's also the reason why we use an asynchronous (unbounded) channel here - to avoid blocking our internal threads by the user code.
        let thread = std::thread::spawn(move || {
            while let Ok(properties) = receiver.recv() {
                let result = catch_unwind(|| {
                    if let Err(e) = callback.properties_updated(properties) {
                        log::error!("Properties updated processing callback failed: {}", e);
                    }
                });

                if let Err(cause) = result {
                    let message = if let Some(s) = cause.downcast_ref::<&'static str>() {
                        (*s).to_string()
                    } else if let Some(s) = cause.downcast_ref::<String>() {
                        s.clone()
                    } else {
                        "Unknown panic with no string representation.".to_string()
                    };

                    log::error!(
                        "Properties updated processing callback failed with panic: {}",
                        message
                    );
                }
            }

            log::debug!("Properties updated processing thread is stopping.");
        });

        Self {
            sender: Some(sender),
            thread: Some(thread),
        }
    }

    pub fn dispatch(&self, properties: DesiredProperties) -> Result<()> {
        self.sender
            .as_ref()
            .expect("Sender unexpectedly empty")
            .send(properties)
            .context("Failed to send updated properties to the callback")
    }
}

impl Drop for DesiredPropertiesUpdatedCallbackDispatcher {
    fn drop(&mut self) {
        // We need to drop the sender so that the thread stops waiting for more method calls
        drop(self.sender.take());

        self.thread
            .take()
            .expect("Property update thread join handle unexpectedly empty.")
            .join()
            .unwrap_or_else(|_| {
                log::error!("Failed joining the thread for processing of property updates.");
            });
    }
}
