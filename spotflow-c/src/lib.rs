#![allow(clippy::missing_safety_doc)]
#![allow(clippy::not_unsafe_ptr_arg_deref)]
#![allow(unused_unsafe)]

use std::ffi::{CStr, CString};
use std::panic::{self, UnwindSafe};
use std::slice;
use std::sync::atomic::{AtomicBool, Ordering};

use anyhow::{bail, Error, Result};
use error::{update_last_error, update_last_error_with_panic, CResult};
use libc::{c_char, size_t};
use simple_logger::SimpleLogger;

pub mod dps;
pub mod error;
pub mod ingress;
pub(crate) mod marshall;

/// The maximum number of bytes of any [Device ID](https://docs.iot.spotflow.io/connect-devices/#device-id) string
/// including the trailing NUL character.
pub const SPOTFLOW_DEVICE_ID_MAX_LENGTH: usize = 128;

/// The maximum number of bytes of any [Workspace ID](https://docs.iot.spotflow.io/manage-access/workspaces/) string
/// including the trailing NUL character.
pub const SPOTFLOW_WORKSPACE_ID_MAX_LENGTH: usize = 37;

/// The maximum number of bytes of any error message including the trailing null character.
pub const SPOTFLOW_ERROR_MAX_LENGTH: usize = 1024;

/// A special value that instructs @ref spotflow_client_get_desired_properties_if_newer to always return the current version.
pub const SPOTFLOW_PROPERTIES_VERSION_ANY: u64 = 0;

/// The verbosity levels of logging.
#[repr(C)]
pub enum LogLevel {
    /// No logging.
    SpotflowLogOff = 0,
    /// Log errors only.
    SpotflowLogError = 1,
    /// Log errors and warnings.
    SpotflowLogWarn = 2,
    /// Log errors, warnings, and information.
    SpotflowLogInfo = 3,
    /// Log errors, warnings, information, and debug messages.
    SpotflowLogDebug = 4,
    /// Log errors, warnings, information, debug messages, and trace messages.
    SpotflowLogTrace = 5,
}

/// Set the verbosity level of logging (@ref SPOTFLOW_LOG_WARN by default).
///
/// @param level The verbosity level of logging.
/// @return @ref SPOTFLOW_OK if successful, @ref SPOTFLOW_ERROR otherwise.
#[no_mangle]
pub extern "C" fn spotflow_set_log_level(level: LogLevel) -> CResult {
    call_safe_with_unit_result(|| {
        ensure_logging();

        let level = match level {
            LogLevel::SpotflowLogOff => log::LevelFilter::Off,
            LogLevel::SpotflowLogError => log::LevelFilter::Error,
            LogLevel::SpotflowLogWarn => log::LevelFilter::Warn,
            LogLevel::SpotflowLogInfo => log::LevelFilter::Info,
            LogLevel::SpotflowLogDebug => log::LevelFilter::Debug,
            LogLevel::SpotflowLogTrace => log::LevelFilter::Trace,
        };

        log::set_max_level(level);

        Ok(())
    })
}

/// Registers the default logger and sets the default log level to `LogLevel::SpotflowLogWarn`.
/// Can be run multiple times and even from different threads, it will only initialize the logger once.
///
/// Must be run as the first step of all the public functions in this library apart from `*_destroy` functions.
/// (These functions have no way to signalize that the logging was not initialized, so they would panic if it wasn't.)
fn ensure_logging() {
    // Make sure that the logger is initialized only once
    static INIT: AtomicBool = AtomicBool::new(false);
    if INIT
        .compare_exchange(false, true, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
    {
        // The logger will write all messages it receives to stderr, the verbosity of external modules
        // is reduced to warnings and errors
        SimpleLogger::new()
            .with_level(log::LevelFilter::Trace)
            .with_module_level("sqlx", log::LevelFilter::Warn)
            .with_module_level("ureq", log::LevelFilter::Warn)
            .with_module_level("rumqttc", log::LevelFilter::Warn)
            .with_module_level("mio", log::LevelFilter::Warn)
            .init()
            .unwrap();

        // We'll limit the verbosity of the messages that the logger will receive to warnings and errors by default
        log::set_max_level(log::LevelFilter::Warn);
    }
}

fn string_to_ptr(s: String) -> *const c_char {
    let string = CString::new(s).expect("Creating CString failed");
    string.into_raw()
}

/// # Safety
/// This calls `CStr::from_ptr` internally, so the same considerations apply with the exception of an automated check against a `nullptr` value.
/// This means among other things that `ptr` may point to non-owned location, it could have been previously freed, it may not contain a NUL terminator, it may be disposed of before the returned `&str`'s lifetime ends, etc.
unsafe fn ptr_to_str<'a>(ptr: *const c_char) -> Result<&'a str> {
    match ptr_to_str_option(ptr)? {
        None => bail!("String is NULL."),
        Some(s) => Ok(s),
    }
}

/// # Safety
/// This calls `CStr::from_ptr` internally, so the same considerations apply with the exception of an automated check against a `nullptr` value.
/// This means among other things that `ptr` may point to non-owned location, it could have been previously freed, it may not contain a NUL terminator, it may be disposed of before the returned `&str`'s lifetime ends, etc.
unsafe fn ptr_to_str_option<'a>(ptr: *const c_char) -> Result<Option<&'a str>> {
    if ptr.is_null() {
        return Ok(None);
    }
    // We can safely assume that c_char is one byte in width so alignment can be ignored

    let result = CStr::from_ptr(ptr).to_str().map_err(Error::from)?;
    Ok(Some(result))
}

fn obj_to_ptr<T>(obj: T) -> *mut T {
    Box::into_raw(Box::new(obj))
}

unsafe fn drop_ptr<T>(ptr: *mut T) {
    if ptr.is_null() {
        return;
    }
    if !is_aligned(ptr) {
        return;
    }

    drop(Box::from_raw(ptr));
}

unsafe fn drop_str_ptr(ptr: *const c_char) {
    if ptr.is_null() {
        return;
    }

    drop(CString::from_raw(ptr as *mut c_char));
}

// This is almost as unsafe as dereferencing with `*`. We just check for nulls and for alignment.
unsafe fn ptr_to_mut<'a, T>(ptr: *mut T) -> Result<&'a mut T> {
    if ptr.is_null() {
        bail!("Pointer is null.");
    }

    // Check for correct alignment. This can catch some bad pointers especially for larger structs such as ingress. But incorrect pointers can still fall through
    // Change this when the function is stabilized
    // if !ptr.is_aligned() {
    if !is_aligned(ptr) {
        bail!("Pointer is not properly aligned.");
    }

    Ok(&mut *ptr)
}

// This is almost as unsafe as dereferencing with `*`. We just check for nulls and for alignment.
unsafe fn ptr_to_ref<'a, T>(ptr: *const T) -> Result<&'a T> {
    match ptr_to_ref_option(ptr)? {
        None => bail!("Pointer is NULL."),
        Some(s) => Ok(s),
    }
}

// This is almost as unsafe as dereferencing with `*`. We just check for nulls and for alignment.
unsafe fn ptr_to_ref_option<'a, T>(ptr: *const T) -> Result<Option<&'a T>> {
    if ptr.is_null() {
        return Ok(None);
    }

    // Check for correct alignment. This can catch some bad pointers especially for larger structs such as ingress. But incorrect pointers can still fall through
    // Change this when the function is stabilized
    // if !ptr.is_aligned() {
    if !is_aligned(ptr) {
        bail!("Pointer is not properly aligned.");
    }

    Ok(Some(&*ptr))
}

unsafe fn store_to_ptr<T>(ptr: *mut T, val: T) -> CResult {
    match unsafe { ptr_to_mut(ptr) } {
        Err(err) => {
            update_last_error(err);
            CResult::SpotflowError
        }
        Ok(ingress) => {
            *ingress = val;
            CResult::SpotflowOk
        }
    }
}

unsafe fn buffer_to_slice<'a, T>(buffer: *const T, length: size_t) -> Result<&'a [T]> {
    if buffer.is_null() {
        bail!("Buffer pointer is null.");
    }

    if !is_aligned(buffer) {
        bail!("Buffer pointer is not properly aligned.");
    }

    let slice = unsafe { slice::from_raw_parts(buffer, length) };

    Ok(slice)
}

fn is_aligned<T>(ptr: *const T) -> bool {
    ptr as usize % core::mem::align_of::<T>() == 0
}

// These functions catch panics and update last error with either a returned error or a caught panic
fn call_safe<F, T>(func: F) -> Result<T, CResult>
where
    F: FnOnce() -> T + UnwindSafe,
{
    let hook = panic::take_hook();
    panic::set_hook(Box::new(|_| {}));
    let result = panic::catch_unwind(func);
    panic::set_hook(hook);

    match result {
        Ok(ok) => Ok(ok),
        Err(e) => {
            update_last_error_with_panic(e);
            Err(CResult::SpotflowError)
        }
    }
}

fn call_safe_with_result<F, T>(func: F) -> Result<T, CResult>
where
    F: FnOnce() -> Result<T> + UnwindSafe,
{
    match call_safe(func) {
        Ok(Ok(res)) => Ok(res),
        Ok(Err(e)) => {
            update_last_error(e);
            Err(CResult::SpotflowError)
        }
        Err(e) => Err(e),
    }
}

fn call_safe_with_unit_result<F>(func: F) -> CResult
where
    F: FnOnce() -> Result<()> + UnwindSafe,
{
    match call_safe_with_result(func) {
        Ok(_) => CResult::SpotflowOk,
        Err(e) => e,
    }
}
