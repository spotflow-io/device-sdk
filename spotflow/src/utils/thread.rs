use std::thread::JoinHandle;

pub(crate) fn join<T>(handle: &mut Option<JoinHandle<T>>) {
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
