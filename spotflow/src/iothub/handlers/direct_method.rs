use std::{
    panic::{catch_unwind, RefUnwindSafe},
    sync::mpsc::{self, TrySendError},
    thread::{self, JoinHandle},
};

use rumqttc::{AsyncClient, Publish, QoS};

use super::super::query;
use super::super::topics;

use super::Handler;

struct Invocation {
    publish: Publish,
    method_name: String,
    request_id: String,
}

pub(crate) struct DirectMethodHandler {
    sender: Option<mpsc::SyncSender<Invocation>>,
    thread: Option<JoinHandle<()>>,
}

impl DirectMethodHandler {
    pub(crate) fn new<F>(client: AsyncClient, method_handler: F) -> Self
    where
        F: Fn(String, &[u8]) -> (i32, Vec<u8>) + Send + RefUnwindSafe + 'static,
    {
        let (sender, receiver) = mpsc::sync_channel::<Invocation>(50);

        log::debug!("Starting direct method processing thread");
        // This is thread and not a simple Tokio task because the handler could potentially block
        // The thread ends when the channel sender is dropped (and all methods are done).
        let thread = thread::spawn({
            move || {
                while let Ok(msg) = receiver.recv() {
                    let result = catch_unwind(|| {
                        // We ignore the error becasue in the worst case we just do not send the ack.
                        // In that case the message will be redelivered again anyway because AtLeastOnce QoS
                        // We want to acknowledge first and then run and return result. These are not retryable.
                        _ = client.try_ack(&msg.publish);
                        method_handler(msg.method_name, msg.publish.payload.as_ref())
                    });
                    match result {
                        Err(cause) => {
                            if let Some(s) = cause.downcast_ref::<&'static str>() {
                                log::error!("Direct method processing failed with panic: {}", s);
                            }
                            if let Some(s) = cause.downcast_ref::<String>() {
                                log::error!(
                                    "Direct method message processing failed with panic: {}",
                                    s
                                );
                            }
                        }
                        Ok((status, payload)) => {
                            let topic = topics::response_topic(status, &msg.request_id);
                            _ = client.try_publish(topic, QoS::AtLeastOnce, false, payload);
                        }
                    }
                }

                log::debug!("Direct method handler is stopping.");
            }
        });

        DirectMethodHandler {
            sender: Some(sender),
            thread: Some(thread),
        }
    }
}

impl Handler for DirectMethodHandler {
    fn prefix(&self) -> Vec<&str> {
        vec![topics::METHODS_PREFIX]
    }

    fn handle(&mut self, publish: &Publish) {
        // WARNING! The topic name may actually contain pretty much anything. IoT Hub does not check whether the method name contains slashes or anything else

        // The topic should be formatted like this:
        // $iothub/methods/POST/{method name}/?$rid={request id}
        let topic = &publish.topic;

        log::debug!("Received direct method call on topic {topic}");

        // Because the method name may contain slashes we need to look for `/` from the right
        let Some(topic_without_prefix) = topic.strip_prefix(topics::METHODS_PREFIX) else {
            // Ignore malformed requests
            return;
        };
        let Some((method_name, rest)) = topic_without_prefix.rsplit_once('/') else {
            // "Invalid topic starts like direct method call but misses fourth slash."
            return;
        };

        let Some(properties) = rest.strip_prefix("?$") else {
            return;
        };

        // Skip the last slash and the leading question mark
        let properties = match query::parse(properties) {
            Ok(properties) => properties,
            Err(e) => {
                log::error!("Failed parsing method call topic `{topic}`: {e:?}");
                return;
            }
        };

        let Some(Some(request_id)) = properties.get("$rid") else {
            log::error!("Request ID is missing in method call on topic `{topic}`");
            return;
        };

        log::debug!("Invoking method named {method_name}");

        match self
            .sender
            .as_mut()
            .expect("The sender is wrapped in Option only to be able to drop it explicitly")
            .try_send(Invocation {
                publish: publish.clone(),
                method_name: method_name.to_owned(),
                request_id: request_id.to_owned(),
            }) {
                Err(TrySendError::Full(invocation)) =>
                    log::warn!("Received unexpectedly many direct method calls before they could be processed. Ignoring call to {} with request ID {}.", invocation.method_name, invocation.request_id),
                Err(TrySendError::Disconnected(invocation)) =>
                    log::error!("Received direct method call after processor shut down. Ignoring call to {} with request ID {}.", invocation.method_name, invocation.request_id),
                Ok(()) => {},
        }
    }
}

impl Drop for DirectMethodHandler {
    fn drop(&mut self) {
        // We need to drop the sender so that the thread stops waiting for more method calls
        drop(self.sender.take());
        join(&mut self.thread);
    }
}

fn join<T>(handle: &mut Option<JoinHandle<T>>) {
    let handle = handle.take();
    if let Some(handle) = handle {
        let thread = handle.thread();
        let id = thread.id();
        let name = thread.name().map(ToString::to_string).unwrap_or_default();
        log::trace!("Joining thread {:?} named `{}`", id, name);
        if let Err(cause) = handle.join() {
            if let Some(s) = cause.downcast_ref::<&'static str>() {
                log::error!("Thread `{}` failed with panic: {}", name, s,);
            } else if let Some(s) = cause.downcast_ref::<String>() {
                log::error!("Thread `{}` failed with panic: {}", name, s,);
            } else {
                log::error!("Thread `{}` failed with panic that is not a string.", name,);
            }
        }
    }
}
