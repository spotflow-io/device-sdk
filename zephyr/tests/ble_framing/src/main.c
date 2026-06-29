#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <string.h>

#include "net/transport/ble/spotflow_ble_transport_internal.h"

LOG_MODULE_REGISTER(spotflow_net, LOG_LEVEL_INF);

struct spotflow_ble_transport_state g_spotflow_ble_transport_state;
const struct bt_gatt_service_static spotflow_svc = { 0 };

static uint8_t callback_payload[SPOTFLOW_CONFIG_RX_BUFFER_SIZE];
static size_t callback_payload_len;
static int callback_count;

const struct bt_gatt_attr* spotflow_ble_tx_stream_attr_get(void)
{
	return NULL;
}

static void config_callback(uint8_t* payload, size_t len)
{
	callback_count++;
	callback_payload_len = len;
	memcpy(callback_payload, payload, len);
}

static void reset_rx_test_state(void)
{
	memset(&g_spotflow_ble_transport_state, 0, sizeof(g_spotflow_ble_transport_state));
	memset(callback_payload, 0, sizeof(callback_payload));
	callback_payload_len = 0;
	callback_count = 0;
	k_mutex_init(&g_spotflow_ble_transport_state.lock);
	g_spotflow_ble_transport_state.config_rx.callback = config_callback;
}

ZTEST_SUITE(spotflow_ble_framing, NULL, NULL, NULL, NULL, NULL);

ZTEST(spotflow_ble_framing, test_encode_single_frame)
{
	static const uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
	struct spotflow_ble_encoded_frame frame;
	size_t next_offset = 0;

	int rc = spotflow_ble_transport_encode_next_frame(SPOTFLOW_MSG_TELEMETRY, 0x7A, payload,
							  sizeof(payload), 20, 0, &frame,
							  &next_offset);

	zassert_equal(rc, 0, "unexpected rc %d", rc);
	zassert_equal(next_offset, sizeof(payload), "unexpected next offset %u", next_offset);
	zassert_equal(frame.len, 9, "unexpected frame len %u", frame.len);
	zassert_equal(frame.data[0], SPOTFLOW_MSG_TELEMETRY);
	zassert_equal(frame.data[1], SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST);
	zassert_equal(frame.data[2], 0x7A);
	zassert_equal(frame.data[3], sizeof(payload));
	zassert_equal(frame.data[4], 0x00);
	zassert_mem_equal(&frame.data[5], payload, sizeof(payload), NULL);
}

ZTEST(spotflow_ble_framing, test_encode_multiple_frames)
{
	static const uint8_t payload[] =
		"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	struct spotflow_ble_encoded_frame frame;
	uint8_t reassembled[sizeof(payload) - 1];
	size_t reassembled_len = 0;
	size_t offset = 0;
	size_t next_offset = 0;
	size_t frame_count = 0;

	while (offset < (sizeof(payload) - 1)) {
		int rc = spotflow_ble_transport_encode_next_frame(
			SPOTFLOW_MSG_REPORTED_CONFIGURATION, 0x13, payload, sizeof(payload) - 1, 16,
			offset, &frame, &next_offset);

		zassert_equal(rc, 0, "unexpected rc %d", rc);
		zassert_equal(frame.data[0], SPOTFLOW_MSG_REPORTED_CONFIGURATION);
		zassert_equal(frame.data[2], 0x13);

		if (offset == 0) {
			zassert_true((frame.data[1] & SPOTFLOW_FRAME_IS_FIRST) != 0, NULL);
			zassert_equal(sys_get_le16(&frame.data[3]), sizeof(payload) - 1);
			memcpy(&reassembled[reassembled_len], &frame.data[5], frame.len - 5);
			reassembled_len += frame.len - 5;
		} else {
			zassert_true((frame.data[1] & SPOTFLOW_FRAME_IS_FIRST) == 0, NULL);
			memcpy(&reassembled[reassembled_len], &frame.data[3], frame.len - 3);
			reassembled_len += frame.len - 3;
		}

		frame_count++;
		offset = next_offset;
	}

	zassert_true(frame_count > 1, "expected fragmentation");
	zassert_true((frame.data[1] & SPOTFLOW_FRAME_IS_LAST) != 0, NULL);
	zassert_equal(reassembled_len, sizeof(payload) - 1);
	zassert_mem_equal(reassembled, payload, sizeof(payload) - 1, NULL);
}

ZTEST(spotflow_ble_framing, test_encode_rejects_too_small_capacity)
{
	static const uint8_t payload[] = { 0x01 };
	struct spotflow_ble_encoded_frame frame;
	size_t next_offset = 0;

	int rc = spotflow_ble_transport_encode_next_frame(
		SPOTFLOW_MSG_TELEMETRY, 0x01, payload, sizeof(payload), 5, 0, &frame, &next_offset);

	zassert_equal(rc, -EMSGSIZE, "unexpected rc %d", rc);
}

ZTEST(spotflow_ble_framing, test_encode_rejects_oversize_frame_capacity)
{
	static const uint8_t payload[] = { 0x01 };
	struct spotflow_ble_encoded_frame frame;
	size_t next_offset = 0;

	int rc = spotflow_ble_transport_encode_next_frame(
		SPOTFLOW_MSG_TELEMETRY, 0x01, payload, sizeof(payload),
		SPOTFLOW_TX_FRAME_BUFFER_SIZE + 1, 0, &frame, &next_offset);

	zassert_equal(rc, -EMSGSIZE, "unexpected rc %d", rc);
}

ZTEST(spotflow_ble_framing, test_encode_rejects_zero_length_payload)
{
	static const uint8_t payload[] = { 0x01 };
	struct spotflow_ble_encoded_frame frame;
	size_t next_offset = 0;

	int rc = spotflow_ble_transport_encode_next_frame(SPOTFLOW_MSG_TELEMETRY, 0x01, payload, 0,
							  20, 0, &frame, &next_offset);

	zassert_equal(rc, -EINVAL, "unexpected rc %d", rc);
}

ZTEST(spotflow_ble_framing, test_decode_single_frame_desired_config)
{
	static const uint8_t payload[] = { 0xA1, 0xB2, 0xC3 };
	uint8_t frame[5 + sizeof(payload)];

	reset_rx_test_state();

	frame[0] = SPOTFLOW_MSG_DESIRED_CONFIGURATION;
	frame[1] = SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST;
	frame[2] = 0x22;
	sys_put_le16(sizeof(payload), &frame[3]);
	memcpy(&frame[5], payload, sizeof(payload));

	int rc = spotflow_ble_transport_process_config_rx_frame(frame, sizeof(frame),
								BT_GATT_WRITE_FLAG_CMD);

	zassert_equal(rc, 0, "unexpected rc %d", rc);
	zassert_equal(callback_count, 1);
	zassert_equal(callback_payload_len, sizeof(payload));
	zassert_mem_equal(callback_payload, payload, sizeof(payload), NULL);
	zassert_false(g_spotflow_ble_transport_state.config_rx.active, NULL);
}

ZTEST(spotflow_ble_framing, test_decode_multiple_frames_from_encoder)
{
	static const uint8_t payload[] =
		"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	struct spotflow_ble_encoded_frame frame;
	size_t offset = 0;
	size_t next_offset = 0;
	int rc;

	reset_rx_test_state();

	while (offset < sizeof(payload) - 1) {
		rc = spotflow_ble_transport_encode_next_frame(SPOTFLOW_MSG_DESIRED_CONFIGURATION,
							      0x33, payload, sizeof(payload) - 1,
							      16, offset, &frame, &next_offset);
		zassert_equal(rc, 0, "unexpected encode rc %d", rc);

		rc = spotflow_ble_transport_process_config_rx_frame(frame.data, frame.len,
								    BT_GATT_WRITE_FLAG_CMD);
		zassert_equal(rc, 0, "unexpected decode rc %d", rc);

		offset = next_offset;
	}

	zassert_equal(callback_count, 1);
	zassert_equal(callback_payload_len, sizeof(payload) - 1);
	zassert_mem_equal(callback_payload, payload, sizeof(payload) - 1, NULL);
	zassert_false(g_spotflow_ble_transport_state.config_rx.active, NULL);
}

ZTEST(spotflow_ble_framing, test_decode_rejects_continuation_without_first)
{
	static const uint8_t frame[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		0,
		0x44,
		0x01,
	};

	reset_rx_test_state();

	int rc = spotflow_ble_transport_process_config_rx_frame(frame, sizeof(frame),
								BT_GATT_WRITE_FLAG_CMD);

	zassert_equal(rc, -EINVAL, "unexpected rc %d", rc);
	zassert_equal(callback_count, 0);
}

ZTEST(spotflow_ble_framing, test_decode_sequence_mismatch_resets_state)
{
	static const uint8_t first_fragment[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_FIRST,
		0x55,
		0x04,
		0x00,
		0xAA,
		0xBB,
	};
	static const uint8_t wrong_sequence_fragment[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION, SPOTFLOW_FRAME_IS_LAST, 0x56, 0xCC, 0xDD,
	};

	reset_rx_test_state();

	int rc = spotflow_ble_transport_process_config_rx_frame(
		first_fragment, sizeof(first_fragment), BT_GATT_WRITE_FLAG_CMD);
	zassert_equal(rc, 0, "unexpected rc %d", rc);
	zassert_true(g_spotflow_ble_transport_state.config_rx.active, NULL);

	rc = spotflow_ble_transport_process_config_rx_frame(
		wrong_sequence_fragment, sizeof(wrong_sequence_fragment), BT_GATT_WRITE_FLAG_CMD);

	zassert_equal(rc, -EINVAL, "unexpected rc %d", rc);
	zassert_equal(callback_count, 0);
	zassert_false(g_spotflow_ble_transport_state.config_rx.active, NULL);
}

ZTEST(spotflow_ble_framing, test_decode_rejects_invalid_first_fragment_lengths)
{
	static const uint8_t zero_total_len[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST,
		0x66,
		0x00,
		0x00,
	};
	static const uint8_t fragment_longer_than_total[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST,
		0x66,
		0x01,
		0x00,
		0xAA,
		0xBB,
	};

	reset_rx_test_state();

	int rc = spotflow_ble_transport_process_config_rx_frame(
		zero_total_len, sizeof(zero_total_len), BT_GATT_WRITE_FLAG_CMD);
	zassert_equal(rc, -EINVAL, "unexpected rc %d", rc);

	rc = spotflow_ble_transport_process_config_rx_frame(fragment_longer_than_total,
							    sizeof(fragment_longer_than_total),
							    BT_GATT_WRITE_FLAG_CMD);
	zassert_equal(rc, -EINVAL, "unexpected rc %d", rc);
	zassert_equal(callback_count, 0);
}

ZTEST(spotflow_ble_framing, test_decode_rejects_incomplete_last_fragment)
{
	static const uint8_t first_fragment[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION, SPOTFLOW_FRAME_IS_FIRST, 0x77, 0x04, 0x00, 0xAA,
	};
	static const uint8_t incomplete_last_fragment[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_LAST,
		0x77,
		0xBB,
	};

	reset_rx_test_state();

	int rc = spotflow_ble_transport_process_config_rx_frame(
		first_fragment, sizeof(first_fragment), BT_GATT_WRITE_FLAG_CMD);
	zassert_equal(rc, 0, "unexpected rc %d", rc);

	rc = spotflow_ble_transport_process_config_rx_frame(
		incomplete_last_fragment, sizeof(incomplete_last_fragment), BT_GATT_WRITE_FLAG_CMD);
	zassert_equal(rc, -EINVAL, "unexpected rc %d", rc);
	zassert_equal(callback_count, 0);
	zassert_false(g_spotflow_ble_transport_state.config_rx.active, NULL);
}

ZTEST(spotflow_ble_framing, test_decode_ignores_other_message_types)
{
	static const uint8_t frame[] = {
		SPOTFLOW_MSG_TELEMETRY,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST,
		0x88,
		0x01,
		0x00,
		0xAA,
	};

	reset_rx_test_state();

	int rc = spotflow_ble_transport_process_config_rx_frame(frame, sizeof(frame),
								BT_GATT_WRITE_FLAG_CMD);

	zassert_equal(rc, 0, "unexpected rc %d", rc);
	zassert_equal(callback_count, 0);
}
