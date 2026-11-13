#include "spotflow_ble_transport_internal.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static int notify_frame(struct bt_conn* conn, const uint8_t* frame, size_t frame_len);
static int map_notify_error(int rc);
static int send_framed_message(uint8_t message_type, uint8_t* sequence_counter, uint8_t* payload,
			       size_t len, const uint32_t* connection_generation);

int spotflow_ble_transport_encode_next_frame(uint8_t message_type, uint8_t sequence,
					     const uint8_t* payload, size_t len,
					     size_t frame_payload_max, size_t offset,
					     struct spotflow_ble_encoded_frame* frame,
					     size_t* next_offset)
{
	if (payload == NULL || len == 0 || frame == NULL || next_offset == NULL) {
		return -EINVAL;
	}

	if (len > UINT16_MAX) {
		return -EMSGSIZE;
	}

	if (frame_payload_max > SPOTFLOW_TX_FRAME_BUFFER_SIZE) {
		return -EMSGSIZE;
	}

	if (offset >= len) {
		return -EINVAL;
	}

	bool first = offset == 0;
	size_t header_len = first ? SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN
				  : SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN;

	if (frame_payload_max <= header_len) {
		return -EMSGSIZE;
	}

	size_t fragment_capacity = frame_payload_max - header_len;
	size_t remaining = len - offset;
	size_t fragment_len = MIN(fragment_capacity, remaining);
	bool last = (offset + fragment_len) == len;
	uint8_t flags = 0;

	if (fragment_capacity == 0U) {
		return -EMSGSIZE;
	}

	if (first) {
		flags |= SPOTFLOW_FRAME_IS_FIRST;
	}
	if (last) {
		flags |= SPOTFLOW_FRAME_IS_LAST;
	}

	frame->data[0] = message_type;
	frame->data[1] = flags;
	frame->data[2] = sequence;
	if (first) {
		sys_put_le16(len, &frame->data[3]);
		memcpy(&frame->data[SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN], &payload[offset],
		       fragment_len);
	} else {
		memcpy(&frame->data[SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN], &payload[offset],
		       fragment_len);
	}

	frame->len = header_len + fragment_len;
	*next_offset = offset + fragment_len;
	return 0;
}

int spotflow_ble_transport_send_framed_message(uint8_t message_type, uint8_t* sequence_counter,
					       uint8_t* payload, size_t len)
{
	return send_framed_message(message_type, sequence_counter, payload, len, NULL);
}

int spotflow_ble_transport_send_framed_message_on_connection(uint8_t message_type,
							     uint8_t* sequence_counter,
							     uint8_t* payload, size_t len,
							     uint32_t connection_generation)
{
	return send_framed_message(message_type, sequence_counter, payload, len,
				   &connection_generation);
}

static int send_framed_message(uint8_t message_type, uint8_t* sequence_counter, uint8_t* payload,
			       size_t len, const uint32_t* connection_generation)
{
	if (sequence_counter == NULL || payload == NULL) {
		LOG_ERR("Sequence counter and payload must not be NULL");
		return -EINVAL;
	}

	/* The processing thread owns telemetry, configuration reports, and OTA
	 * results. transfer_lock serializes artifact-request producers. Different
	 * types may interleave because the gateway reassembles them independently;
	 * a future producer of the same type must serialize its complete messages.
	 */
	struct bt_conn* conn = NULL;
	uint8_t sequence = 0;

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	if (connection_generation != NULL &&
	    *connection_generation != g_spotflow_ble_transport_state.tx.connection_generation) {
		k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
		return -ENOTCONN;
	}
	if (g_spotflow_ble_transport_state.tx.conn != NULL &&
	    g_spotflow_ble_transport_state.tx.notifications_enabled) {
		conn = bt_conn_ref(g_spotflow_ble_transport_state.tx.conn);
		sequence = (*sequence_counter)++;
	}
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	if (conn == NULL) {
		return connection_generation == NULL ? -EAGAIN : -ENOTCONN;
	}

	if (len > UINT16_MAX) {
		bt_conn_unref(conn);
		return -EMSGSIZE;
	}

	uint16_t mtu = bt_gatt_get_mtu(conn);
	size_t notify_payload_max = mtu > SPOTFLOW_BLE_ATT_NOTIFY_HEADER_LEN
		? mtu - SPOTFLOW_BLE_ATT_NOTIFY_HEADER_LEN
		: 0;

	if (notify_payload_max <= SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN) {
		bt_conn_unref(conn);
		return -EMSGSIZE;
	}

	struct spotflow_ble_encoded_frame frame;
	size_t offset = 0;
	size_t next_offset = 0;
	int rc;

	while (offset < len) {
		rc = spotflow_ble_transport_encode_next_frame(
			message_type, sequence, payload, len,
			MIN(notify_payload_max, SPOTFLOW_TX_FRAME_BUFFER_SIZE), offset, &frame,
			&next_offset);
		if (rc < 0) {
			bt_conn_unref(conn);
			return rc;
		}

		rc = notify_frame(conn, frame.data, frame.len);
		if (rc < 0) {
			bt_conn_unref(conn);
			return map_notify_error(rc);
		}

		offset = next_offset;
	}

	bt_conn_unref(conn);
	return 0;
}

static int notify_frame(struct bt_conn* conn, const uint8_t* frame, size_t frame_len)
{
	return bt_gatt_notify(conn, spotflow_ble_tx_stream_attr_get(), frame, frame_len);
}

static int map_notify_error(int rc)
{
	if (rc == -EAGAIN || rc == -EBUSY || rc == -ENOTCONN || rc == -ENOMEM) {
		return -EAGAIN;
	}

	return rc;
}
