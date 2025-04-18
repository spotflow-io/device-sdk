use std::{
    collections::HashMap,
    panic::catch_unwind,
    sync::mpsc::{self, TrySendError},
    thread::{self, JoinHandle},
};

use rumqttc::{AsyncClient, Publish, QoS};
use serde_json::json;

use crate::ingress::MethodInvocationHandler;
use crate::ingress::{MethodError, MethodReturnValue};

use super::super::topics;
use super::{super::query, Handler};

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
    pub(crate) fn new(
        client: AsyncClient,
        handler_map: HashMap<String, Box<dyn MethodInvocationHandler>>,
    ) -> Self {
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

                        if let Some(handler) = handler_map.get(&msg.method_name) {
                            handler.handle(msg.publish.payload.as_ref())
                        } else {
                            None
                        }
                    });

                    let result = match result {
                        Err(cause) => {
                            let error_message = if let Some(s) =
                                cause.downcast_ref::<&'static str>()
                            {
                                log::error!("Direct method processing failed with panic: {}", s);
                                format!("Panic: {}", s)
                            } else if let Some(s) = cause.downcast_ref::<String>() {
                                log::error!(
                                    "Direct method message processing failed with panic: {}",
                                    s
                                );
                                format!("Panic: {}", s)
                            } else {
                                log::error!(
                                    "Direct method message processing failed with unknown panic."
                                );
                                "Unknown panic".to_string()
                            };

                            Err(MethodError::new(500, error_message))
                        }
                        Ok(Some(result)) => result,
                        Ok(None) => {
                            log::warn!(
                                "Unhandled direct method call on topic {}.",
                                msg.publish.topic
                            );
                            Err(MethodError::new(404, "No handler found".to_string()))
                        }
                    };

                    match result {
                        Ok(MethodReturnValue { status_code, body }) => {
                            log::debug!(
                                "Sending successful response with status code {}.",
                                status_code
                            );

                            let topic = topics::response_topic(status_code, msg.request_id);

                            _ = client.try_publish(
                                topic,
                                QoS::AtLeastOnce,
                                false,
                                body.unwrap_or_default(),
                            );
                        }
                        Err(MethodError {
                            status_code,
                            message,
                        }) => {
                            log::debug!(
                                "Sending error response '{}' with status code {}",
                                message,
                                status_code
                            );

                            let topic = topics::response_topic(status_code, msg.request_id);

                            // We might extend to full Problem Details format in the future, using only the details field for now
                            let payload = json!({
                                "detail": message
                            })
                            .to_string();

                            _ = client.try_publish(
                                topic,
                                QoS::AtLeastOnce,
                                false,
                                payload.as_bytes(),
                            );
                        }
                    };
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
        let topic_without_prefix = &topic[topics::METHODS_PREFIX.len()..];
        let last_slash = topic_without_prefix
            .rfind('/')
            .expect("Invalid topic starts like direct method call but misses fourth slash.");
        let method_name = topic_without_prefix[..last_slash].to_string();

        // Skip the last slash and the leading question mark
        let properties = match query::parse(&topic_without_prefix[last_slash + 2..]) {
            Ok(properties) => properties,
            Err(e) => {
                log::error!("Failed parsing method call topic `{}`: {:?}", topic, e);
                return;
            }
        };

        let request_id = match properties.get("$rid") {
            Some(Some(id)) => id.to_string(),
            _ => {
                log::error!("Request ID is missing in method call on topic `{}`", topic);
                return;
            }
        };

        log::debug!("Invoking method named {method_name}");

        match self
            .sender
            .as_mut()
            .expect("The sender is wrapped in Option only to be able to drop it explicitly")
            .try_send(Invocation {
                publish: publish.clone(),
                method_name,
                request_id,
            }) {
                Err(TrySendError::Full(invocation)) =>
                    log::warn!("Received unexpectedly many direct method calls before they could be processed. Ignoring call to {} with request ID {}.", invocation.method_name, invocation.request_id),
                Err(TrySendError::Disconnected(invocation)) =>
                    log::error!("Received direct method call after processor shut down. Ignoring call to {} with request ID {}.", invocation.method_name, invocation.request_id),
                Ok(_) => {},
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
        let name = thread.name().map(|n| n.to_string()).unwrap_or_default();
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
