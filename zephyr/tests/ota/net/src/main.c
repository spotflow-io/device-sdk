#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "ota/spotflow_ota_net.h"

#include "spotflow_ota_test_fakes.h"

static const uint8_t merged_results_payload[] = {
	/* indefinite map */
	0xbf,
	/* messageType: UPDATE_RESULTS */
	0x00,
	0x09,
	/* updateAttemptId: 11 */
	0x18,
	0x20,
	0x0b,
	/* succeeded: [0] */
	0x18,
	0x2a,
	0x9f,
	0x00,
	0xff,
	/* failed: [1] */
	0x18,
	0x2b,
	0x9f,
	0x01,
	0xff,
	/* canceled: [3] */
	0x18,
	0x2c,
	0x9f,
	0x03,
	0xff,
	/* end map */
	0xff,
};

static const uint8_t attempt_error_payload[] = {
	/* indefinite map */
	0xbf,
	/* messageType: UPDATE_RESULTS */
	0x00,
	0x09,
	/* updateAttemptId: 12 */
	0x18,
	0x20,
	0x0c,
	/* updateAttemptError: CANNOT_PARSE_MESSAGE */
	0x18,
	0x2d,
	0x03,
	/* end map */
	0xff,
};

static const uint8_t replacement_payload[] = {
	/* indefinite map */
	0xbf,
	/* messageType: UPDATE_RESULTS */
	0x00,
	0x09,
	/* updateAttemptId: 21 */
	0x18,
	0x20,
	0x15,
	/* failed: [2] */
	0x18,
	0x2b,
	0x9f,
	0x02,
	0xff,
	/* end map */
	0xff,
};

int spotflow_mqtt_publish_ota_cbor_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	fake_mqtt->publish_count++;
	fake_mqtt->last_payload = payload;
	fake_mqtt->last_payload_len = len;

	return fake_mqtt->publish_result;
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_ota_test_fakes_reset();
	spotflow_ota_net_reset();
}

static void expect_payload(const uint8_t* expected_payload, size_t expected_len)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	zassert_equal(fake_mqtt->last_payload_len, expected_len);
	zassert_mem_equal(fake_mqtt->last_payload, expected_payload, expected_len);
}

ZTEST(spotflow_ota_net, test_merge_succeeded_failed_and_canceled_arrays)
{
	const enum spotflow_ota_result first_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_PENDING,
	};
	const enum spotflow_ota_result second_results[] = {
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_FAILED,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_CANCELED,
	};

	zassert_ok(spotflow_ota_net_prepare_results(11, first_results, ARRAY_SIZE(first_results)));
	zassert_ok(
	    spotflow_ota_net_prepare_results(11, second_results, ARRAY_SIZE(second_results)));
	zassert_ok(spotflow_ota_net_send_pending_message());

	expect_payload(merged_results_payload, sizeof(merged_results_payload));
}

ZTEST(spotflow_ota_net, test_encode_and_send_pending_attempt_error)
{
	zassert_ok(spotflow_ota_net_prepare_attempt_error(
	    12, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE));
	zassert_ok(spotflow_ota_net_send_pending_message());

	expect_payload(attempt_error_payload, sizeof(attempt_error_payload));
}

ZTEST(spotflow_ota_net, test_preserve_pending_message_after_eagain)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	const enum spotflow_ota_result results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_PENDING,
	};

	zassert_ok(spotflow_ota_net_prepare_results(13, results, ARRAY_SIZE(results)));
	fake_mqtt->publish_result = -EAGAIN;

	zassert_equal(spotflow_ota_net_send_pending_message(), -EAGAIN);
	zassert_equal(fake_mqtt->publish_count, 1);

	fake_mqtt->publish_result = 0;
	zassert_ok(spotflow_ota_net_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 2);
}

ZTEST(spotflow_ota_net, test_clear_pending_message_after_publish_success)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	const enum spotflow_ota_result results[] = {
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_FAILED,
	};

	zassert_ok(spotflow_ota_net_prepare_results(14, results, ARRAY_SIZE(results)));
	zassert_ok(spotflow_ota_net_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 1);

	zassert_ok(spotflow_ota_net_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 1);
}

ZTEST(spotflow_ota_net, test_replace_pending_data_for_new_attempt)
{
	const enum spotflow_ota_result old_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_PENDING,
	};
	const enum spotflow_ota_result new_results[] = {
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_PENDING,
		SPOTFLOW_OTA_RESULT_FAILED,
	};

	zassert_ok(spotflow_ota_net_prepare_results(20, old_results, ARRAY_SIZE(old_results)));
	zassert_ok(spotflow_ota_net_prepare_results(21, new_results, ARRAY_SIZE(new_results)));
	zassert_ok(spotflow_ota_net_send_pending_message());

	expect_payload(replacement_payload, sizeof(replacement_payload));
}

ZTEST_SUITE(spotflow_ota_net, NULL, NULL, before_each, NULL, NULL);
