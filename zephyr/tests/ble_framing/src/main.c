#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "net/spotflow_ble_transport_internal.h"

LOG_MODULE_REGISTER(spotflow_net, LOG_LEVEL_INF);

struct spotflow_ble_transport_state g_spotflow_ble_transport_state;
const struct bt_gatt_service_static spotflow_svc = { 0 };

ZTEST_SUITE(spotflow_ble_framing, NULL, NULL, NULL, NULL, NULL);

ZTEST(spotflow_ble_framing, test_encode_single_frame)
{
	static const uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
	struct spotflow_ble_encoded_frame frames[4];
	size_t frame_count = 0;

	int rc = spotflow_ble_transport_encode_frames(SPOTFLOW_MSG_TELEMETRY, 0x7A, payload,
						      sizeof(payload), 20, frames,
						      ARRAY_SIZE(frames), &frame_count);

	zassert_equal(rc, 0, "unexpected rc %d", rc);
	zassert_equal(frame_count, 1, "unexpected frame count %u", frame_count);
	zassert_equal(frames[0].len, 9, "unexpected frame len %u", frames[0].len);
	zassert_equal(frames[0].data[0], SPOTFLOW_MSG_TELEMETRY, NULL);
	zassert_equal(frames[0].data[1], SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST, NULL);
	zassert_equal(frames[0].data[2], 0x7A, NULL);
	zassert_equal(frames[0].data[3], sizeof(payload), NULL);
	zassert_equal(frames[0].data[4], 0x00, NULL);
	zassert_mem_equal(&frames[0].data[5], payload, sizeof(payload), NULL);
}

ZTEST(spotflow_ble_framing, test_encode_multiple_frames)
{
	static const uint8_t payload[] =
	    "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	struct spotflow_ble_encoded_frame frames[8];
	size_t frame_count = 0;
	uint8_t reassembled[sizeof(payload) - 1];
	size_t reassembled_len = 0;

	int rc = spotflow_ble_transport_encode_frames(SPOTFLOW_MSG_REPORTED_CONFIGURATION, 0x13,
						      payload, sizeof(payload) - 1, 16, frames,
						      ARRAY_SIZE(frames), &frame_count);

	zassert_equal(rc, 0, "unexpected rc %d", rc);
	zassert_true(frame_count > 1, "expected fragmentation");

	for (size_t i = 0; i < frame_count; i++) {
		zassert_equal(frames[i].data[0], SPOTFLOW_MSG_REPORTED_CONFIGURATION, NULL);
		zassert_equal(frames[i].data[2], 0x13, NULL);

		if (i == 0) {
			zassert_true((frames[i].data[1] & SPOTFLOW_FRAME_IS_FIRST) != 0, NULL);
			zassert_equal(sys_get_le16(&frames[i].data[3]), sizeof(payload) - 1, NULL);
			memcpy(&reassembled[reassembled_len], &frames[i].data[5],
			       frames[i].len - 5);
			reassembled_len += frames[i].len - 5;
		} else {
			zassert_true((frames[i].data[1] & SPOTFLOW_FRAME_IS_FIRST) == 0, NULL);
			memcpy(&reassembled[reassembled_len], &frames[i].data[3],
			       frames[i].len - 3);
			reassembled_len += frames[i].len - 3;
		}
	}

	zassert_true((frames[frame_count - 1].data[1] & SPOTFLOW_FRAME_IS_LAST) != 0, NULL);
	zassert_equal(reassembled_len, sizeof(payload) - 1, NULL);
	zassert_mem_equal(reassembled, payload, sizeof(payload) - 1, NULL);
}

ZTEST(spotflow_ble_framing, test_encode_rejects_too_small_capacity)
{
	static const uint8_t payload[] = { 0x01 };
	struct spotflow_ble_encoded_frame frames[1];
	size_t frame_count = 0;

	int rc = spotflow_ble_transport_encode_frames(SPOTFLOW_MSG_TELEMETRY, 0x01, payload,
						      sizeof(payload), 5, frames,
						      ARRAY_SIZE(frames), &frame_count);

	zassert_equal(rc, -EMSGSIZE, "unexpected rc %d", rc);
}
