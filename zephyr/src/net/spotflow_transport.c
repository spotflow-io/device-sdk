#include "net/spotflow_transport.h"

#include <zephyr/sys/util.h>

#if CONFIG_SPOTFLOW_TRANSPORT_BLE
#include "net/transport/ble/spotflow_ble_transport.h"
#endif

#if CONFIG_SPOTFLOW_TRANSPORT_MQTT
#include "net/transport/mqtt/spotflow_mqtt.h"
#endif

#include <errno.h>

int spotflow_transport_start(void)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	return spotflow_ble_transport_start();
#else
	return 0;
#endif
}

bool spotflow_transport_is_ready(void)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	return spotflow_ble_transport_is_ready();
#else
	return spotflow_mqtt_is_connected();
#endif
}

int spotflow_transport_send_ingest_cbor(uint8_t* payload, size_t len)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	return spotflow_ble_transport_send_ingest_cbor(payload, len);
#else
	return spotflow_mqtt_publish_ingest_cbor_msg(payload, len);
#endif
}

int spotflow_transport_send_config_cbor(uint8_t* payload, size_t len)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	return spotflow_ble_transport_send_config_cbor(payload, len);
#else
	return spotflow_mqtt_publish_config_cbor_msg(payload, len);
#endif
}

int spotflow_transport_send_ota_cbor(uint8_t* payload, size_t len)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	return -ENOTSUP;
#else
	return spotflow_mqtt_publish_ota_cbor_msg(payload, len);
#endif
}

int spotflow_transport_subscribe_config(spotflow_transport_message_cb callback)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	return spotflow_ble_transport_subscribe_config(callback);
#else
	return spotflow_mqtt_request_config_subscription(callback);
#endif
}

int spotflow_transport_subscribe_ota(spotflow_transport_message_cb callback)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	ARG_UNUSED(callback);
	return -ENOTSUP;
#else
	return spotflow_mqtt_request_ota_subscription(callback);
#endif
}

void spotflow_transport_abort(void)
{
#if CONFIG_SPOTFLOW_TRANSPORT_BLE
	spotflow_ble_transport_abort();
#else
	spotflow_mqtt_abort_mqtt();
#endif
}
