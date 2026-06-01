#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "ota/spotflow_ota_records_cbor.h"

LOG_MODULE_DECLARE(spotflow_ota);

#define KEY_SCHEMA_VERSION 0
#define KEY_ATTEMPT_ID 1
#define KEY_ATTEMPT_ERROR 2
#define KEY_ARTIFACT_COUNT 3
#define KEY_ACTIONABLE_CANCELLATION 4
#define KEY_ARTIFACT_RESULTS 5

#define KEY_PROBATION_ARTIFACT_INDEX 2
#define KEY_PROBATION_SLUG 3
#define KEY_PROBATION_VERSION 4
#define KEY_PROBATION_EXPECTED_BUILD_ID 5

#define ZCBOR_STATE_DEPTH 4
#define ATTEMPT_RECORD_MAP_ENTRIES 6
#define PROBATION_RECORD_MAP_ENTRIES 5

static bool decode_expected_uint32_key(zcbor_state_t* state, uint32_t expected);
static bool copy_tstr(zcbor_state_t* state, char* buffer, size_t buffer_len, size_t max_len);
static int validate_attempt(const struct spotflow_ota_persisted_attempt* attempt);
static int validate_probation(const struct spotflow_ota_probation* probation);

int spotflow_ota_records_cbor_encode_attempt(const struct spotflow_ota_persisted_attempt* attempt,
					     uint8_t* buffer, size_t len, size_t* encoded_len)
{
	if (attempt == NULL || buffer == NULL || len == 0 || encoded_len == NULL) {
		return -EINVAL;
	}

	int rc = validate_attempt(attempt);

	if (rc < 0) {
		return rc;
	}

	ZCBOR_STATE_E(state, ZCBOR_STATE_DEPTH, buffer, len, 0);

	bool success = zcbor_map_start_encode(state, ATTEMPT_RECORD_MAP_ENTRIES);

	success = success && zcbor_uint32_put(state, KEY_SCHEMA_VERSION);
	success = success && zcbor_uint32_put(state, SPOTFLOW_OTA_RECORDS_CBOR_SCHEMA_VERSION);
	success = success && zcbor_uint32_put(state, KEY_ATTEMPT_ID);
	success = success && zcbor_uint64_put(state, attempt->attempt_id);

	if (attempt->has_attempt_error) {
		success = success && zcbor_uint32_put(state, KEY_ATTEMPT_ERROR);
		success = success && zcbor_uint32_put(state, attempt->attempt_error);
	} else {
		success = success && zcbor_uint32_put(state, KEY_ARTIFACT_COUNT);
		success = success && zcbor_uint32_put(state, (uint32_t)attempt->artifact_count);
		success = success && zcbor_uint32_put(state, KEY_ACTIONABLE_CANCELLATION);
		success = success && zcbor_bool_put(state, attempt->actionable_cancellation);
		success = success && zcbor_uint32_put(state, KEY_ARTIFACT_RESULTS);
		success = success && zcbor_list_start_encode(state, attempt->artifact_count);

		for (size_t i = 0; success && i < attempt->artifact_count; i++) {
			success = success &&
			    zcbor_uint32_put(state, (uint32_t)attempt->artifact_results[i]);
		}

		success = success && zcbor_list_end_encode(state, attempt->artifact_count);
	}
	success = success && zcbor_map_end_encode(state, ATTEMPT_RECORD_MAP_ENTRIES);

	if (!success) {
		LOG_ERR("Failed to encode OTA attempt record: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*encoded_len = state->payload - buffer;
	return 0;
}

int spotflow_ota_records_cbor_decode_attempt(const uint8_t* payload, size_t len,
					     struct spotflow_ota_persisted_attempt* attempt)
{
	if (payload == NULL || len == 0 || attempt == NULL) {
		return -EINVAL;
	}

	*attempt = (struct spotflow_ota_persisted_attempt){ 0 };
	for (size_t i = 0; i < ARRAY_SIZE(attempt->artifact_results); i++) {
		attempt->artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}

	ZCBOR_STATE_D(state, ZCBOR_STATE_DEPTH, payload, len, 1, 0);

	bool success = zcbor_map_start_decode(state);
	uint32_t schema_version;
	uint32_t next_key;
	uint32_t attempt_error;
	uint32_t artifact_count;
	bool actionable_cancellation;
	size_t result_index = 0;

	success = success && decode_expected_uint32_key(state, KEY_SCHEMA_VERSION);
	success = success && zcbor_uint32_decode(state, &schema_version);
	success = success && decode_expected_uint32_key(state, KEY_ATTEMPT_ID);
	success = success && zcbor_uint64_decode(state, &attempt->attempt_id);
	success = success && zcbor_uint32_decode(state, &next_key);

	if (!success || schema_version != SPOTFLOW_OTA_RECORDS_CBOR_SCHEMA_VERSION ||
	    attempt->attempt_id == 0) {
		return -EINVAL;
	}

	if (next_key == KEY_ATTEMPT_ERROR) {
		if (!zcbor_uint32_decode(state, &attempt_error) || !zcbor_map_end_decode(state)) {
			return -EINVAL;
		}

		attempt->has_attempt_error = true;
		attempt->attempt_error = attempt_error;
		return validate_attempt(attempt);
	}

	if (next_key != KEY_ARTIFACT_COUNT || !zcbor_uint32_decode(state, &artifact_count) ||
	    artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS ||
	    !decode_expected_uint32_key(state, KEY_ACTIONABLE_CANCELLATION) ||
	    !zcbor_bool_decode(state, &actionable_cancellation) ||
	    !decode_expected_uint32_key(state, KEY_ARTIFACT_RESULTS) ||
	    !zcbor_list_start_decode(state)) {
		return -EINVAL;
	}

	attempt->artifact_count = artifact_count;
	attempt->actionable_cancellation = actionable_cancellation;

	while (!zcbor_array_at_end(state)) {
		if (result_index >= attempt->artifact_count) {
			return -EINVAL;
		}

		uint32_t result;

		if (!zcbor_uint32_decode(state, &result) || result > SPOTFLOW_OTA_RESULT_CANCELED) {
			return -EINVAL;
		}

		attempt->artifact_results[result_index++] = result;
	}

	if (result_index != attempt->artifact_count || !zcbor_list_end_decode(state) ||
	    !zcbor_map_end_decode(state)) {
		return -EINVAL;
	}

	return validate_attempt(attempt);
}

int spotflow_ota_records_cbor_encode_probation(const struct spotflow_ota_probation* probation,
					       uint8_t* buffer, size_t len, size_t* encoded_len)
{
	if (probation == NULL || buffer == NULL || len == 0 || encoded_len == NULL) {
		return -EINVAL;
	}

	int rc = validate_probation(probation);

	if (rc < 0) {
		return rc;
	}

	ZCBOR_STATE_E(state, ZCBOR_STATE_DEPTH, buffer, len, 0);

	bool success = zcbor_map_start_encode(state, PROBATION_RECORD_MAP_ENTRIES);

	success = success && zcbor_uint32_put(state, KEY_SCHEMA_VERSION);
	success = success && zcbor_uint32_put(state, SPOTFLOW_OTA_RECORDS_CBOR_SCHEMA_VERSION);
	success = success && zcbor_uint32_put(state, KEY_ATTEMPT_ID);
	success = success && zcbor_uint64_put(state, probation->attempt_id);
	success = success && zcbor_uint32_put(state, KEY_PROBATION_ARTIFACT_INDEX);
	success = success && zcbor_uint32_put(state, probation->artifact_index);
	success = success && zcbor_uint32_put(state, KEY_PROBATION_SLUG);
	success =
	    success && zcbor_tstr_put_term(state, probation->slug, sizeof(probation->slug) - 1);
	success = success && zcbor_uint32_put(state, KEY_PROBATION_VERSION);
	success = success &&
	    zcbor_tstr_put_term(state, probation->version, sizeof(probation->version) - 1);
	success = success && zcbor_uint32_put(state, KEY_PROBATION_EXPECTED_BUILD_ID);
	success = success &&
	    zcbor_bstr_encode_ptr(state, probation->expected_build_id, SPOTFLOW_BUILD_ID_LENGTH);
	success = success && zcbor_map_end_encode(state, PROBATION_RECORD_MAP_ENTRIES);

	if (!success) {
		LOG_ERR("Failed to encode OTA probation record: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*encoded_len = state->payload - buffer;
	return 0;
}

int spotflow_ota_records_cbor_decode_probation(const uint8_t* payload, size_t len,
					       struct spotflow_ota_probation* probation)
{
	if (payload == NULL || len == 0 || probation == NULL) {
		return -EINVAL;
	}

	*probation = (struct spotflow_ota_probation){ 0 };

	ZCBOR_STATE_D(state, ZCBOR_STATE_DEPTH, payload, len, 1, 0);

	bool success = zcbor_map_start_decode(state);
	uint32_t schema_version;
	struct zcbor_string build_id;

	success = success && decode_expected_uint32_key(state, KEY_SCHEMA_VERSION);
	success = success && zcbor_uint32_decode(state, &schema_version);
	success = success && decode_expected_uint32_key(state, KEY_ATTEMPT_ID);
	success = success && zcbor_uint64_decode(state, &probation->attempt_id);
	success = success && decode_expected_uint32_key(state, KEY_PROBATION_ARTIFACT_INDEX);
	success = success && zcbor_uint32_decode(state, &probation->artifact_index);
	success = success && decode_expected_uint32_key(state, KEY_PROBATION_SLUG);
	success = success &&
	    copy_tstr(state, probation->slug, sizeof(probation->slug),
		      SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH);
	success = success && decode_expected_uint32_key(state, KEY_PROBATION_VERSION);
	success = success &&
	    copy_tstr(state, probation->version, sizeof(probation->version),
		      SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH);
	success = success && decode_expected_uint32_key(state, KEY_PROBATION_EXPECTED_BUILD_ID);
	success = success && zcbor_bstr_decode(state, &build_id);

	if (!success || schema_version != SPOTFLOW_OTA_RECORDS_CBOR_SCHEMA_VERSION ||
	    build_id.len != SPOTFLOW_BUILD_ID_LENGTH) {
		return -EINVAL;
	}

	memcpy(probation->expected_build_id, build_id.value, build_id.len);

	if (!zcbor_map_end_decode(state)) {
		return -EINVAL;
	}

	return validate_probation(probation);
}

static bool decode_expected_uint32_key(zcbor_state_t* state, uint32_t expected)
{
	return zcbor_uint32_pexpect(state, &expected);
}

static bool copy_tstr(zcbor_state_t* state, char* buffer, size_t buffer_len, size_t max_len)
{
	struct zcbor_string str;

	if (!zcbor_tstr_decode(state, &str) || str.len == 0 || str.len > max_len ||
	    str.len >= buffer_len) {
		return false;
	}

	memcpy(buffer, str.value, str.len);
	buffer[str.len] = '\0';
	return true;
}

static int validate_attempt(const struct spotflow_ota_persisted_attempt* attempt)
{
	if (attempt->attempt_id == 0) {
		return -EINVAL;
	}

	if (attempt->has_attempt_error) {
		if (attempt->attempt_error > SPOTFLOW_OTA_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE) {
			return -EINVAL;
		}

		return 0;
	}

	if (attempt->artifact_count == 0 ||
	    attempt->artifact_count > CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS) {
		return -EINVAL;
	}

	for (size_t i = 0; i < attempt->artifact_count; i++) {
		if (attempt->artifact_results[i] > SPOTFLOW_OTA_RESULT_CANCELED) {
			return -EINVAL;
		}
	}

	return 0;
}

static int validate_probation(const struct spotflow_ota_probation* probation)
{
	if (probation->attempt_id == 0 || probation->slug[0] == '\0' ||
	    probation->version[0] == '\0') {
		return -EINVAL;
	}

	if (strlen(probation->slug) > SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH ||
	    strlen(probation->version) > SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH) {
		return -EINVAL;
	}

	return 0;
}
