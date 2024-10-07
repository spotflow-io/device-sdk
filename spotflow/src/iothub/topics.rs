pub(super) const METHODS_PREFIX: &str = "$iothub/methods/POST/";
pub(super) const TWIN_RESPONSE_PREFIX: &str = "$iothub/twin/res/";
pub(super) const UPDATE_DESIRED_PROPERTIES_PREFIX: &str = "$iothub/twin/PATCH/properties/desired/";

pub(super) fn publish_topic(device_id: &str) -> String {
    format!("devices/{device_id}/messages/events/")
}

pub(super) fn c2d_topic(device_id: &str) -> String {
    format!("devices/{device_id}/messages/devicebound/")
}

pub(crate) fn response_topic(status: i32, request_id: &str) -> String {
    format!("$iothub/methods/res/{status}/?$rid={request_id}")
}

pub(crate) fn patch_reported_properties(rid: &str) -> String {
    format!("$iothub/twin/PATCH/properties/reported/?$rid={rid}")
}

pub(crate) fn get_twins(rid: &str) -> String {
    format!("$iothub/twin/GET/?$rid={rid}")
}
