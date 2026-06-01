#include <string.h>

#include <zcbor_decode.h>

#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/ztest.h>

#include "net/spotflow_mqtt.h"
#include "net/spotflow_session_metadata.h"
#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_persistence.h"
#include "spotflow_ota_test_settings.h"

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

static int decode_session_metadata(const uint8_t* payload, size_t len,
				   struct decoded_session_metadata* metadata);
static bool search_uint32_key(zcbor_state_t* state, uint32_t key);
static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected);

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
	ota_subscription_count++;
	return 0;
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	spotflow_ota_test_settings_reset();
	memset(published_payload, 0, sizeof(published_payload));
	published_payload_len = 0;
	ota_subscription_count = 0;
	fake_random_seed = 0;
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
