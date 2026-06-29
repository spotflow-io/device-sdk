#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "net/transport/ble/spotflow_ble_transport_internal.h"

LOG_MODULE_REGISTER(spotflow_net, LOG_LEVEL_INF);

struct spotflow_ble_transport_state g_spotflow_ble_transport_state;
const struct bt_gatt_service_static spotflow_svc = { 0 };

const struct bt_gatt_attr* spotflow_ble_tx_stream_attr_get(void)
{
	return NULL;
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
	zassert_equal(frame.data[0], SPOTFLOW_MSG_TELEMETRY, NULL);
	zassert_equal(frame.data[1], SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST, NULL);
	zassert_equal(frame.data[2], 0x7A, NULL);
	zassert_equal(frame.data[3], sizeof(payload), NULL);
	zassert_equal(frame.data[4], 0x00, NULL);
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
		zassert_equal(frame.data[0], SPOTFLOW_MSG_REPORTED_CONFIGURATION, NULL);
		zassert_equal(frame.data[2], 0x13, NULL);

		if (offset == 0) {
			zassert_true((frame.data[1] & SPOTFLOW_FRAME_IS_FIRST) != 0, NULL);
			zassert_equal(sys_get_le16(&frame.data[3]), sizeof(payload) - 1, NULL);
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
	zassert_equal(reassembled_len, sizeof(payload) - 1, NULL);
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
