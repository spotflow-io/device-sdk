#include "net/spotflow_ble_transport.h"
#include "net/spotflow_ble_transport_internal.h"

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
	g_spotflow_ble_transport_state.config_rx.callback = callback;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	return 0;
}

void spotflow_ble_transport_abort(void) {}
