#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "ota/spotflow_ota_cbor.h"
#include "ota/spotflow_ota_net.h"

#include "spotflow_ota_test_fakes.h"

LOG_MODULE_REGISTER(spotflow_ota);

static uint8_t published_payload[128];

int spotflow_mqtt_publish_ota_cbor_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	if (len > sizeof(published_payload)) {
		return -ENOMEM;
	}

	fake_mqtt->publish_count++;
	memcpy(published_payload, payload, len);
	fake_mqtt->last_payload = published_payload;
	fake_mqtt->last_payload_len = len;

	return fake_mqtt->publish_result;
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_ota_test_fakes_reset();
	spotflow_ota_net_reset();
}

static void expect_payload(const struct spotflow_ota_cbor_update_results* expected_message)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	uint8_t expected_payload[128];
	size_t expected_len;

	zassert_ok(spotflow_ota_cbor_encode_update_results(
	    expected_message, expected_payload, sizeof(expected_payload), &expected_len));

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

	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 11,
		.succeeded_count = 1,
		.succeeded = { 0 },
		.failed_count = 1,
		.failed = { 1 },
		.canceled_count = 1,
		.canceled = { 3 },
	};

	expect_payload(&expected_message);
}

ZTEST(spotflow_ota_net, test_encode_and_send_pending_attempt_error)
{
	zassert_ok(spotflow_ota_net_prepare_attempt_error(
	    12, SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE));
	zassert_ok(spotflow_ota_net_send_pending_message());

	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 12,
		.has_attempt_error = true,
		.attempt_error = SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE,
	};

	expect_payload(&expected_message);
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

	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 21,
		.failed_count = 1,
		.failed = { 2 },
	};

	expect_payload(&expected_message);
}

ZTEST_SUITE(spotflow_ota_net, NULL, NULL, before_each, NULL, NULL);
