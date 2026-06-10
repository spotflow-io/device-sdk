#include <errno.h>
#include <string.h>

#include <zcbor_decode.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/ztest.h>

#include <spotflow/ota.h>

#include "net/spotflow_mqtt.h"
#include "net/spotflow_session_metadata.h"
#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_cbor.h"
#include "ota/spotflow_ota_state.h"
#include "ota/spotflow_ota_persistence.h"
#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_settings.h"
#include "spotflow_ota_test_wait.h"

LOG_MODULE_REGISTER(spotflow_net);

#define KEY_MESSAGE_TYPE 0x00
#define KEY_DEVICE_RUN_ID 0x1E
#define KEY_LAST_UPDATE_ATTEMPT_ID 41
#define SESSION_METADATA_MESSAGE_TYPE 1

struct decoded_session_metadata {
	uint32_t message_type;
	uint64_t device_run_id;
	bool has_last_update_attempt_id;
	uint64_t last_update_attempt_id;
};

static uint8_t published_payload[128];
static size_t published_payload_len;
static uint32_t ota_subscription_count;
static uint8_t fake_random_seed;
static spotflow_mqtt_message_cb ota_callback;

static const uint8_t valid_update_artifacts_payload[] = {
	/* map(3) */
	0xa3,
	/* messageType: UPDATE_ARTIFACTS */
	0x00,
	0x06,
	/* updateAttemptId: 1 */
	0x18,
	0x20,
	0x01,
	/* manifest: array(1) */
	0x18,
	0x22,
	0x81,
	/* artifact: map(6) */
	0xa6,
	/* artifactType: FIRMWARE */
	0x18,
	0x23,
	0x00,
	/* slug: "main" */
	0x18,
	0x24,
	0x64,
	'm',
	'a',
	'i',
	'n',
	/* isMain: true */
	0x18,
	0x25,
	0xf5,
	/* url: "https://a" */
	0x18,
	0x26,
	0x69,
	'h',
	't',
	't',
	'p',
	's',
	':',
	'/',
	'/',
	'a',
	/* secret: "secret" */
	0x18,
	0x27,
	0x66,
	's',
	'e',
	'c',
	'r',
	'e',
	't',
	/* version: "1.0.0" */
	0x18,
	0x28,
	0x65,
	'1',
	'.',
	'0',
	'.',
	'0',
};

static const uint8_t valid_two_artifacts_payload[] = {
	/* map(3) */
	0xa3,
	/* messageType: UPDATE_ARTIFACTS */
	0x00,
	0x06,
	/* updateAttemptId: 1 */
	0x18,
	0x20,
	0x01,
	/* manifest: array(2) */
	0x18,
	0x22,
	0x82,
	/* artifact: map(6) */
	0xa6,
	/* artifactType: FIRMWARE */
	0x18,
	0x23,
	0x00,
	/* slug: "main" */
	0x18,
	0x24,
	0x64,
	'm',
	'a',
	'i',
	'n',
	/* isMain: true */
	0x18,
	0x25,
	0xf5,
	/* url: "https://a" */
	0x18,
	0x26,
	0x69,
	'h',
	't',
	't',
	'p',
	's',
	':',
	'/',
	'/',
	'a',
	/* secret: "secret" */
	0x18,
	0x27,
	0x66,
	's',
	'e',
	'c',
	'r',
	'e',
	't',
	/* version: "1.0.0" */
	0x18,
	0x28,
	0x65,
	'1',
	'.',
	'0',
	'.',
	'0',
	/* artifact: map(6) */
	0xa6,
	/* artifactType: FIRMWARE */
	0x18,
	0x23,
	0x00,
	/* slug: "aux" */
	0x18,
	0x24,
	0x63,
	'a',
	'u',
	'x',
	/* isMain: false */
	0x18,
	0x25,
	0xf4,
	/* url: "https://b" */
	0x18,
	0x26,
	0x69,
	'h',
	't',
	't',
	'p',
	's',
	':',
	'/',
	'/',
	'b',
	/* secret: "secret2" */
	0x18,
	0x27,
	0x67,
	's',
	'e',
	'c',
	'r',
	'e',
	't',
	'2',
	/* version: "1.0.1" */
	0x18,
	0x28,
	0x65,
	'1',
	'.',
	'0',
	'.',
	'1',
};

static const uint8_t cancel_update_payload[] = {
	/* map(2) */
	0xa2,
	/* messageType: CANCEL_UPDATE */
	0x00,
	0x07,
	/* updateAttemptId: 1 */
	0x18,
	0x20,
	0x01,
};

static const uint8_t report_update_results_payload[] = {
	/* map(2) */
	0xa2,
	/* messageType: REPORT_UPDATE_RESULTS */
	0x00,
	0x08,
	/* updateAttemptId: 1 */
	0x18,
	0x20,
	0x01,
};

static int decode_session_metadata(const uint8_t* payload, size_t len,
				   struct decoded_session_metadata* metadata);
static bool search_uint32_key(zcbor_state_t* state, uint32_t key);
static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected);
enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();

	fake_callbacks->handle_call_count++;
	fake_callbacks->handle_thread = k_current_get();
	fake_callbacks->last_attempt_id = info->attempt_id;
	fake_callbacks->last_is_main = info->is_main;
	fake_callbacks->canceled_visible_during_handle = spotflow_is_update_canceled();
	strncpy(fake_callbacks->last_slug, info->slug, sizeof(fake_callbacks->last_slug) - 1);
	strncpy(fake_callbacks->last_url, info->download_request->url,
		sizeof(fake_callbacks->last_url) - 1);
	strncpy(fake_callbacks->last_secret, info->download_request->secret,
		sizeof(fake_callbacks->last_secret) - 1);
	strncpy(fake_callbacks->last_version, info->version,
		sizeof(fake_callbacks->last_version) - 1);
	k_sem_give(&fake_callbacks->handle_called_sem);

	if (fake_callbacks->block_handle) {
		k_sem_take(&fake_callbacks->handle_continue_sem, K_FOREVER);
	}

	return fake_callbacks->next_handle_result;
}

void spotflow_on_update_canceled(void)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();

	fake_callbacks->cancel_call_count++;
	fake_callbacks->cancel_thread = k_current_get();
	fake_callbacks->canceled_visible_during_notification = spotflow_is_update_canceled();
	k_sem_give(&fake_callbacks->cancel_called_sem);
}

void z_impl_sys_rand_get(void* dst, size_t len)
{
	uint8_t* bytes = dst;

	for (size_t i = 0; i < len; i++) {
		bytes[i] = ++fake_random_seed;
	}
}

int spotflow_mqtt_publish_ingest_cbor_msg(uint8_t* payload, size_t len)
{
	if (len > sizeof(published_payload)) {
		return -ENOMEM;
	}

	memcpy(published_payload, payload, len);
	published_payload_len = len;
	return 0;
}

int spotflow_mqtt_request_ota_subscription(spotflow_mqtt_message_cb callback)
{
	zassert_not_null(callback);
	ota_callback = callback;
	ota_subscription_count++;
	return 0;
}

int spotflow_mqtt_publish_ota_cbor_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	if (len > sizeof(published_payload)) {
		return -ENOMEM;
	}

	fake_mqtt->publish_count++;
	fake_mqtt->last_payload = published_payload;
	fake_mqtt->last_payload_len = len;
	memcpy(published_payload, payload, len);
	return fake_mqtt->publish_result;
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	spotflow_ota_test_settings_reset();
	spotflow_ota_test_fakes_reset();
	memset(published_payload, 0, sizeof(published_payload));
	published_payload_len = 0;
	ota_subscription_count = 0;
	fake_random_seed = 0;
	ota_callback = NULL;
	spotflow_ota_reset();
}

ZTEST(spotflow_ota_facade, test_session_metadata_reports_zero_last_attempt_id_when_empty)
{
	struct decoded_session_metadata metadata;

	zassert_ok(spotflow_ota_init());
	zassert_ok(spotflow_session_metadata_send());
	zassert_ok(decode_session_metadata(published_payload, published_payload_len, &metadata));
	zassert_equal(metadata.message_type, SESSION_METADATA_MESSAGE_TYPE);
	zassert_not_equal(metadata.device_run_id, 0);
	zassert_true(metadata.has_last_update_attempt_id);
	zassert_equal(metadata.last_update_attempt_id, 0);
	zassert_equal(spotflow_ota_get_last_received_attempt_id(), 0);
}

ZTEST(spotflow_ota_facade, test_session_metadata_reports_loaded_last_attempt_id)
{
	struct decoded_session_metadata metadata;
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = 201,
		.artifact_count = 2,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_SUCCEEDED,
			SPOTFLOW_OTA_RESULT_FAILED,
		},
	};

	zassert_ok(spotflow_ota_persistence_save_attempt(&attempt));
	zassert_ok(spotflow_ota_init());
	zassert_ok(spotflow_session_metadata_send());
	zassert_ok(decode_session_metadata(published_payload, published_payload_len, &metadata));
	zassert_true(metadata.has_last_update_attempt_id);
	zassert_equal(metadata.last_update_attempt_id, attempt.attempt_id);
	zassert_equal(spotflow_ota_get_last_received_attempt_id(), attempt.attempt_id);
}

ZTEST(spotflow_ota_facade, test_ota_init_session_requests_subscription)
{
	zassert_ok(spotflow_ota_init_session());
	zassert_equal(ota_subscription_count, 1);
	zassert_not_null(ota_callback);
}

ZTEST(spotflow_ota_facade, test_c2d_handler_accepts_valid_update_message)
{
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	zassert_ok(spotflow_ota_init_session());
	ota_callback((uint8_t*)valid_update_artifacts_payload,
		     sizeof(valid_update_artifacts_payload));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_true(snapshot.has_current_attempt);
	zassert_equal(snapshot.current_attempt_id, 1);
	zassert_equal(snapshot.artifact_count, 1);
	zassert_equal(spotflow_ota_get_last_received_attempt_id(), 1);
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_equal(job.type, SPOTFLOW_OTA_WORKER_JOB_PROCESS_ARTIFACT);
	zassert_equal(job.attempt_id, 1);
}

ZTEST(spotflow_ota_facade,
      test_c2d_handler_classifies_malformed_message_with_trustworthy_attempt_id)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	struct spotflow_ota_state_snapshot snapshot;
	uint8_t payload[sizeof(valid_update_artifacts_payload)];

	memcpy(payload, valid_update_artifacts_payload, sizeof(payload));
	payload[12] = 0x01;

	zassert_ok(spotflow_ota_init_session());
	ota_callback(payload, sizeof(payload));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_true(snapshot.has_current_attempt);
	zassert_true(snapshot.has_attempt_error);
	zassert_equal(snapshot.current_attempt_id, 1);
	zassert_equal(snapshot.attempt_error, SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE);
	zassert_equal(spotflow_ota_get_last_received_attempt_id(), 1);
	zassert_ok(spotflow_ota_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 0);
}

ZTEST(spotflow_ota_facade, test_c2d_handler_ignores_message_without_trustworthy_attempt_id)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	struct spotflow_ota_state_snapshot snapshot;
	uint8_t payload[sizeof(cancel_update_payload)];

	memcpy(payload, cancel_update_payload, sizeof(payload));
	payload[5] = 0x00;

	zassert_ok(spotflow_ota_init_session());
	ota_callback(payload, sizeof(payload));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_false(snapshot.has_current_attempt);
	zassert_equal(spotflow_ota_get_last_received_attempt_id(), 0);
	zassert_ok(spotflow_ota_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 0);
}

ZTEST(spotflow_ota_facade, test_report_request_queues_current_attempt_results)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	struct spotflow_ota_state_action action;
	struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.failed_count = 1,
		.failed = { 0 },
	};

	zassert_ok(spotflow_ota_init_session());
	ota_callback((uint8_t*)valid_update_artifacts_payload,
		     sizeof(valid_update_artifacts_payload));
	zassert_ok(
	    spotflow_ota_state_apply_artifact_result(0, SPOTFLOW_OTA_RESULT_FAILED, &action));
	ota_callback((uint8_t*)report_update_results_payload,
		     sizeof(report_update_results_payload));
	zassert_ok(spotflow_ota_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 1);

	uint8_t expected_payload[32];
	size_t expected_len;
	zassert_ok(spotflow_ota_cbor_encode_update_results(
	    &expected_message, expected_payload, sizeof(expected_payload), &expected_len));
	zassert_equal(fake_mqtt->last_payload_len, expected_len);
	zassert_mem_equal(fake_mqtt->last_payload, expected_payload, expected_len);
}

ZTEST(spotflow_ota_facade, test_version_match_skips_callback_and_reports_success)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.succeeded_count = 1,
		.succeeded = { 0 },
	};

	zassert_ok(spotflow_ota_persistence_save_installed_version("main", "1.0.0"));
	zassert_ok(spotflow_ota_init_session());
	ota_callback((uint8_t*)valid_update_artifacts_payload,
		     sizeof(valid_update_artifacts_payload));

	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 0);
	zassert_ok(spotflow_ota_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_facade, test_rejected_attempt_is_persisted_and_reported)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	uint8_t payload[sizeof(valid_update_artifacts_payload)];
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.has_attempt_error = true,
		.attempt_error = SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE,
	};

	memcpy(payload, valid_update_artifacts_payload, sizeof(payload));
	payload[12] = 0x01;

	zassert_ok(spotflow_ota_init_session());
	ota_callback(payload, sizeof(payload));

	spotflow_ota_test_wait_for_persisted_attempt_error(
	    1, SPOTFLOW_OTA_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE);
	zassert_ok(spotflow_ota_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);

	ota_callback((uint8_t*)report_update_results_payload,
		     sizeof(report_update_results_payload));
	zassert_ok(spotflow_ota_send_pending_message());
	zassert_equal(fake_mqtt->publish_count, 2);
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_facade,
      test_pending_callback_result_is_mapped_to_failed_and_cancels_remaining_artifacts)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_FAILED,
		SPOTFLOW_OTA_RESULT_CANCELED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.failed_count = 1,
		.failed = { 0 },
		.canceled_count = 1,
		.canceled = { 1 },
	};

	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_PENDING;

	zassert_ok(spotflow_ota_init_session());
	ota_callback((uint8_t*)valid_two_artifacts_payload, sizeof(valid_two_artifacts_payload));

	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->handle_call_count, 1);
	zassert_ok(spotflow_ota_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_facade, test_accepted_cancel_notifies_user_code_and_callback_can_return_canceled)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_CANCELED,
	};
	const struct spotflow_ota_cbor_update_results expected_message = {
		.attempt_id = 1,
		.canceled_count = 1,
		.canceled = { 0 },
	};

	fake_callbacks->block_handle = true;
	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_CANCELED;

	zassert_ok(spotflow_ota_init_session());
	ota_callback((uint8_t*)valid_update_artifacts_payload,
		     sizeof(valid_update_artifacts_payload));
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	zassert_false(spotflow_is_update_canceled());

	ota_callback((uint8_t*)cancel_update_payload, sizeof(cancel_update_payload));
	zassert_ok(k_sem_take(&fake_callbacks->cancel_called_sem, K_SECONDS(1)));
	zassert_true(fake_callbacks->canceled_visible_during_notification);
	zassert_true(spotflow_is_update_canceled());

	k_sem_give(&fake_callbacks->handle_continue_sem);
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));
	zassert_equal(fake_callbacks->cancel_call_count, 1);
	zassert_not_equal(fake_callbacks->handle_thread, fake_callbacks->cancel_thread);
	zassert_ok(spotflow_ota_send_pending_message());
	spotflow_ota_test_expect_update_results_payload(&expected_message);
}

ZTEST(spotflow_ota_facade, test_late_cancel_does_not_notify_user_code)
{
	struct spotflow_ota_test_fake_callbacks* fake_callbacks =
	    spotflow_ota_test_fake_callbacks_get();
	const enum spotflow_ota_result expected_results[] = {
		SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};

	fake_callbacks->next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED;

	zassert_ok(spotflow_ota_init_session());
	ota_callback((uint8_t*)valid_update_artifacts_payload,
		     sizeof(valid_update_artifacts_payload));
	zassert_ok(k_sem_take(&fake_callbacks->handle_called_sem, K_SECONDS(1)));
	spotflow_ota_test_wait_for_persisted_attempt(1, expected_results,
						     ARRAY_SIZE(expected_results));

	ota_callback((uint8_t*)cancel_update_payload, sizeof(cancel_update_payload));
	zassert_equal(k_sem_take(&fake_callbacks->cancel_called_sem, K_MSEC(100)), -EAGAIN);
	zassert_false(spotflow_is_update_canceled());
	zassert_equal(fake_callbacks->cancel_call_count, 0);
}

ZTEST_SUITE(spotflow_ota_facade, NULL, NULL, before_each, NULL, NULL);

static int decode_session_metadata(const uint8_t* payload, size_t len,
				   struct decoded_session_metadata* metadata)
{
	if (payload == NULL || len == 0 || metadata == NULL) {
		return -EINVAL;
	}

	*metadata = (struct decoded_session_metadata){ 0 };

	ZCBOR_STATE_D(state, 2, payload, len, 1, 0);

	bool success = zcbor_unordered_map_start_decode(state);
	success = success && search_uint32_key(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_decode(state, &metadata->message_type);
	success = success && search_uint32_key(state, KEY_DEVICE_RUN_ID);
	success = success && zcbor_uint64_decode(state, &metadata->device_run_id);

	if (search_uint32_key(state, KEY_LAST_UPDATE_ATTEMPT_ID)) {
		metadata->has_last_update_attempt_id = true;
		success = success && zcbor_uint64_decode(state, &metadata->last_update_attempt_id);
	}

	success = success && zcbor_unordered_map_end_decode(state);

	return success ? 0 : -EINVAL;
}

static bool search_uint32_key(zcbor_state_t* state, uint32_t key)
{
	return zcbor_unordered_map_search(decode_expected_uint32_key, state, &key);
}

static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected)
{
	return zcbor_uint32_pexpect(state, expected);
}

