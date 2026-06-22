#include "net/spotflow_ble_transport_internal.h"

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

extern const struct bt_gatt_service_static spotflow_svc;

void spotflow_ble_transport_reset_config_rx_state(void)
{
	g_spotflow_ble_transport_state.config_rx.active = false;
	g_spotflow_ble_transport_state.config_rx.sequence = 0;
	g_spotflow_ble_transport_state.config_rx.total_len = 0;
	g_spotflow_ble_transport_state.config_rx.received_len = 0;
}

int spotflow_ble_transport_process_config_rx_frame(const void* buf, uint16_t len, uint8_t flags)
{
	if ((flags & BT_GATT_WRITE_FLAG_CMD) == 0 || len < 3) {
		spotflow_ble_transport_reset_config_rx_state();
		return -EINVAL;
	}

	const uint8_t* frame = buf;
	if (frame[0] != SPOTFLOW_MSG_DESIRED_CONFIGURATION) {
		LOG_DBG("Ignored BLE RX stream write with message type 0x%02x", frame[0]);
		return 0;
	}

	spotflow_transport_message_cb config_callback;
	uint8_t callback_buffer[SPOTFLOW_CONFIG_RX_BUFFER_SIZE];
	size_t callback_len = 0;
	bool invoke_callback = false;
	uint8_t fragment_flags = frame[1];
	bool is_first = (fragment_flags & SPOTFLOW_FRAME_IS_FIRST) != 0;
	bool is_last = (fragment_flags & SPOTFLOW_FRAME_IS_LAST) != 0;
	uint8_t sequence = frame[2];

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	config_callback = g_spotflow_ble_transport_state.config_rx.callback;

	if (config_callback == NULL) {
		k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
		return 0;
	}

	if (is_first) {
		if (len < 5) {
			spotflow_ble_transport_reset_config_rx_state();
			k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
			return -EINVAL;
		}

		size_t total_len = sys_get_le16(&frame[3]);
		size_t fragment_len = len - 5;

		if (total_len == 0 ||
		    total_len > sizeof(g_spotflow_ble_transport_state.config_rx.buffer) ||
		    fragment_len > total_len) {
			spotflow_ble_transport_reset_config_rx_state();
			k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
			return -EINVAL;
		}

		memcpy(g_spotflow_ble_transport_state.config_rx.buffer, &frame[5], fragment_len);
		g_spotflow_ble_transport_state.config_rx.active = !is_last;
		g_spotflow_ble_transport_state.config_rx.sequence = sequence;
		g_spotflow_ble_transport_state.config_rx.total_len = total_len;
		g_spotflow_ble_transport_state.config_rx.received_len = fragment_len;

		if (is_last) {
			if (fragment_len != total_len) {
				spotflow_ble_transport_reset_config_rx_state();
				k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
				return -EINVAL;
			}

			memcpy(callback_buffer, g_spotflow_ble_transport_state.config_rx.buffer,
			       total_len);
			callback_len = total_len;
			invoke_callback = true;
			spotflow_ble_transport_reset_config_rx_state();
		}
	} else {
		if (!g_spotflow_ble_transport_state.config_rx.active ||
		    g_spotflow_ble_transport_state.config_rx.sequence != sequence) {
			spotflow_ble_transport_reset_config_rx_state();
			k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
			return -EINVAL;
		}

		size_t fragment_len = len - 3;
		if ((g_spotflow_ble_transport_state.config_rx.received_len + fragment_len) >
		    g_spotflow_ble_transport_state.config_rx.total_len) {
			spotflow_ble_transport_reset_config_rx_state();
			k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
			return -EINVAL;
		}

		memcpy(&g_spotflow_ble_transport_state.config_rx
				.buffer[g_spotflow_ble_transport_state.config_rx.received_len],
		       &frame[3], fragment_len);
		g_spotflow_ble_transport_state.config_rx.received_len += fragment_len;

		if (is_last) {
			if (g_spotflow_ble_transport_state.config_rx.received_len !=
			    g_spotflow_ble_transport_state.config_rx.total_len) {
				spotflow_ble_transport_reset_config_rx_state();
				k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
				return -EINVAL;
			}

			memcpy(callback_buffer, g_spotflow_ble_transport_state.config_rx.buffer,
			       g_spotflow_ble_transport_state.config_rx.total_len);
			callback_len = g_spotflow_ble_transport_state.config_rx.total_len;
			invoke_callback = true;
			spotflow_ble_transport_reset_config_rx_state();
		}
	}

	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	if (invoke_callback) {
		config_callback(callback_buffer, callback_len);
	}

	return 0;
}

/*
 * Spotflow BLE frames wrap opaque payload bytes sent on TX/RX stream characteristics.
 *
 * First fragment:
 * byte 0: message type
 * byte 1: flags
 * byte 2: sequence number
 * byte 3: total length low byte
 * byte 4: total length high byte
 * byte 5+: fragment payload
 *
 * Continuation fragment:
 * byte 0: message type
 * byte 1: flags
 * byte 2: sequence number
 * byte 3+: fragment payload
 *
 * Flags:
 * BIT(0): first fragment
 * BIT(1): last fragment
 */
int spotflow_ble_transport_encode_frames(uint8_t message_type, uint8_t sequence,
					 const uint8_t* payload, size_t len,
					 size_t frame_payload_max,
					 struct spotflow_ble_encoded_frame* frames,
					 size_t max_frames, size_t* frame_count)
{
	if (payload == NULL || frames == NULL || frame_count == NULL) {
		return -EINVAL;
	}

	if (len > UINT16_MAX) {
		return -EMSGSIZE;
	}

	if (frame_payload_max <= 5) {
		return -EMSGSIZE;
	}

	size_t offset = 0;
	size_t encoded_frame_count = 0;
	bool first = true;

	while (offset < len) {
		size_t header_len = first ? 5 : 3;
		size_t fragment_capacity = frame_payload_max - header_len;
		size_t remaining = len - offset;
		size_t fragment_len = MIN(fragment_capacity, remaining);
		bool last = (offset + fragment_len) == len;
		uint8_t flags = 0;

		if (encoded_frame_count >= max_frames) {
			return -ENOSPC;
		}

		if (fragment_capacity == 0U) {
			return -EMSGSIZE;
		}

		if (first) {
			flags |= SPOTFLOW_FRAME_IS_FIRST;
		}
		if (last) {
			flags |= SPOTFLOW_FRAME_IS_LAST;
		}

		frames[encoded_frame_count].data[0] = message_type;
		frames[encoded_frame_count].data[1] = flags;
		frames[encoded_frame_count].data[2] = sequence;
		if (first) {
			sys_put_le16(len, &frames[encoded_frame_count].data[3]);
			memcpy(&frames[encoded_frame_count].data[5], &payload[offset],
			       fragment_len);
		} else {
			memcpy(&frames[encoded_frame_count].data[3], &payload[offset],
			       fragment_len);
		}

		frames[encoded_frame_count].len = header_len + fragment_len;
		encoded_frame_count++;
		offset += fragment_len;
		first = false;
	}

	*frame_count = encoded_frame_count;
	return 0;
}

int spotflow_ble_transport_send_framed_message(uint8_t message_type, uint8_t* sequence_counter,
					       uint8_t* payload, size_t len)
{
	struct bt_conn* conn = NULL;
	uint8_t sequence = 0;

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	if (g_spotflow_ble_transport_state.tx.conn != NULL &&
	    g_spotflow_ble_transport_state.tx.notifications_enabled) {
		conn = bt_conn_ref(g_spotflow_ble_transport_state.tx.conn);
		sequence = (*sequence_counter)++;
	}
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	if (conn == NULL) {
		return -EAGAIN;
	}

	if (len > UINT16_MAX) {
		bt_conn_unref(conn);
		return -EMSGSIZE;
	}

	uint16_t mtu = bt_gatt_get_mtu(conn);
	size_t notify_payload_max = mtu > 3 ? mtu - 3 : 0;

	if (notify_payload_max <= 5) {
		bt_conn_unref(conn);
		return -EMSGSIZE;
	}

	struct spotflow_ble_encoded_frame frames[SPOTFLOW_BLE_MAX_ENCODED_FRAMES];
	size_t frame_count = 0;
	int rc = spotflow_ble_transport_encode_frames(
		message_type, sequence, payload, len,
		MIN(notify_payload_max, SPOTFLOW_TX_FRAME_BUFFER_SIZE), frames, ARRAY_SIZE(frames),
		&frame_count);
	if (rc < 0) {
		bt_conn_unref(conn);
		return rc;
	}

	for (size_t i = 0; i < frame_count; i++) {
		rc = notify_frame(conn, frames[i].data, frames[i].len);
		if (rc < 0) {
			bt_conn_unref(conn);
			return map_notify_error(rc);
		}
	}

	bt_conn_unref(conn);
	return 0;
}

static int notify_frame(struct bt_conn* conn, const uint8_t* frame, size_t frame_len)
{
	/* Attribute 8 is the TX Stream characteristic value in spotflow_svc. */
	return bt_gatt_notify(conn, &spotflow_svc.attrs[8], frame, frame_len);
}

static int map_notify_error(int rc)
{
	if (rc == -EAGAIN || rc == -EBUSY || rc == -ENOTCONN || rc == -ENOMEM) {
		return -EAGAIN;
	}

	return rc;
}
