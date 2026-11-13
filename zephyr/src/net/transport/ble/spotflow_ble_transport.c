#include "spotflow_ble_transport.h"
#include "spotflow_ble_transport_internal.h"

#include <errno.h>
#include <zephyr/bluetooth/gatt.h>

struct spotflow_ble_transport_state g_spotflow_ble_transport_state;

int spotflow_ble_transport_start(void)
{
	return spotflow_ble_transport_start_impl();
}

bool spotflow_ble_transport_is_ready(void)
{
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	bool ready = g_spotflow_ble_transport_state.tx.conn != NULL &&
		g_spotflow_ble_transport_state.tx.notifications_enabled;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	return ready;
}

int spotflow_ble_transport_send_ingest_cbor(uint8_t* payload, size_t len)
{
	return spotflow_ble_transport_send_framed_message(
		SPOTFLOW_MSG_TELEMETRY, &g_spotflow_ble_transport_state.tx.telemetry_sequence,
		payload, len);
}

int spotflow_ble_transport_send_config_cbor(uint8_t* payload, size_t len)
{
	return spotflow_ble_transport_send_framed_message(
		SPOTFLOW_MSG_REPORTED_CONFIGURATION,
		&g_spotflow_ble_transport_state.tx.reported_config_sequence, payload, len);
}

int spotflow_ble_transport_subscribe_config(spotflow_transport_message_cb callback)
{
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	g_spotflow_ble_transport_state.config_callback = callback;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	return 0;
}

void spotflow_ble_transport_abort(void) {}

int spotflow_ble_transport_send_control(bool accepted, uint8_t message_type, uint8_t sequence,
					uint32_t connection_generation)
{
	uint8_t frame[] = { accepted ? 0x00 : 0x01, message_type, sequence };
	struct bt_conn* conn = NULL;

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	/* Check and reference the originating connection under the same lock. A
	 * callback may have outlived its connection; never acknowledge it to a new one.
	 */
	if (g_spotflow_ble_transport_state.tx.connection_generation == connection_generation &&
	    g_spotflow_ble_transport_state.tx.conn != NULL &&
	    g_spotflow_ble_transport_state.tx.notifications_enabled) {
		conn = bt_conn_ref(g_spotflow_ble_transport_state.tx.conn);
	}
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	if (conn == NULL) {
		return -EAGAIN;
	}
	int rc = bt_gatt_notify(conn, spotflow_ble_tx_stream_attr_get(), frame, sizeof(frame));
	bt_conn_unref(conn);
	return rc == -EAGAIN || rc == -EBUSY || rc == -ENOTCONN || rc == -ENOMEM ? -EAGAIN : rc;
}

int spotflow_ble_transport_send_ota_cbor(uint8_t* payload, size_t len)
{
	return spotflow_ble_transport_send_framed_message(
		SPOTFLOW_MSG_OTA_D2C, &g_spotflow_ble_transport_state.tx.ota_sequence, payload,
		len);
}

int spotflow_ble_transport_subscribe_ota(spotflow_transport_message_cb callback)
{
	if (!IS_ENABLED(CONFIG_SPOTFLOW_OTA)) {
		return -ENOTSUP;
	}
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	g_spotflow_ble_transport_state.ota_callback = callback;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	return 0;
}
