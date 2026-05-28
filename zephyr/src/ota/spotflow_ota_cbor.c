#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "ota/spotflow_ota_cbor.h"

LOG_MODULE_DECLARE(spotflow_ota);

#define KEY_MESSAGE_TYPE 0x00
#define KEY_UPDATE_ATTEMPT_ID 32
#define KEY_IS_CANCELED 33
#define KEY_MANIFEST 34
#define KEY_ARTIFACT_TYPE 35
#define KEY_SLUG 36
#define KEY_IS_MAIN 37
#define KEY_URL 38
#define KEY_SECRET 39
#define KEY_VERSION 40
#define KEY_SUCCEEDED 42
#define KEY_FAILED 43
#define KEY_CANCELED 44
#define KEY_UPDATE_ATTEMPT_ERROR 45

#define ZCBOR_STATE_DEPTH 5

static bool search_uint32_key(zcbor_state_t* state, uint32_t key);
static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected);
static void set_attempt_error(struct spotflow_ota_cbor_decode_status* status,
			      enum spotflow_ota_cbor_attempt_error error);
static int decode_update_artifacts(zcbor_state_t* state, struct spotflow_ota_cbor_c2d_msg* msg,
				   struct spotflow_ota_cbor_decode_status* status);
static int decode_artifact(zcbor_state_t* state, struct spotflow_ota_cbor_artifact* artifact,
			   enum spotflow_ota_cbor_attempt_error* error);
static bool copy_tstr(zcbor_state_t* state, char* buffer, size_t buffer_len, size_t max_len);
static bool encode_index_array(zcbor_state_t* state, uint32_t key, const uint32_t* indexes,
			       size_t count);

int spotflow_ota_cbor_decode_c2d(const uint8_t* payload, size_t len,
				 struct spotflow_ota_cbor_c2d_msg* msg,
				 struct spotflow_ota_cbor_decode_status* status)
{
	if (payload == NULL || len == 0 || msg == NULL || status == NULL) {
		LOG_ERR("Invalid payload, length, or output parameters");
		return -EINVAL;
	}

	*msg = (struct spotflow_ota_cbor_c2d_msg){ 0 };
	*status = (struct spotflow_ota_cbor_decode_status){ 0 };

	ZCBOR_STATE_D(state, ZCBOR_STATE_DEPTH, payload, len, 1, 0);

	bool success = zcbor_unordered_map_start_decode(state);

	uint32_t message_type;
	success = success && search_uint32_key(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_decode(state, &message_type);

	uint64_t attempt_id;
	success = success && search_uint32_key(state, KEY_UPDATE_ATTEMPT_ID);
	success = success && zcbor_uint64_decode(state, &attempt_id);

	if (!success || attempt_id == 0) {
		LOG_ERR("Failed to decode OTA C2D message header: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	status->has_trustworthy_attempt_id = true;
	status->attempt_id = attempt_id;
	msg->attempt_id = attempt_id;
	msg->type = message_type;

	int rc = 0;

	switch (message_type) {
	case SPOTFLOW_OTA_CBOR_MSG_UPDATE_ARTIFACTS:
		rc = decode_update_artifacts(state, msg, status);
		break;
	case SPOTFLOW_OTA_CBOR_MSG_CANCEL_UPDATE:
	case SPOTFLOW_OTA_CBOR_MSG_REPORT_UPDATE_RESULTS:
		break;
	default:
		set_attempt_error(status, SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		return rc;
	}

	if (!zcbor_unordered_map_end_decode(state)) {
		set_attempt_error(status, SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
		LOG_ERR("Malformed OTA C2D message map: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	return 0;
}

int spotflow_ota_cbor_encode_update_results(const struct spotflow_ota_cbor_update_results* msg,
					    uint8_t* buffer, size_t len, size_t* encoded_len)
{
	if (msg == NULL || buffer == NULL || len == 0 || encoded_len == NULL ||
	    msg->attempt_id == 0) {
		LOG_ERR("Invalid OTA update results message");
		return -EINVAL;
	}

	size_t map_entries = 2;

	if (msg->has_attempt_error) {
		map_entries++;
	} else {
		map_entries += msg->succeeded_count > 0 ? 1 : 0;
		map_entries += msg->failed_count > 0 ? 1 : 0;
		map_entries += msg->canceled_count > 0 ? 1 : 0;
	}

	ZCBOR_STATE_E(state, ZCBOR_STATE_DEPTH, buffer, len, 0);

	bool success = zcbor_map_start_encode(state, map_entries);

	success = success && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, 0x09);
	success = success && zcbor_uint32_put(state, KEY_UPDATE_ATTEMPT_ID);
	success = success && zcbor_uint64_put(state, msg->attempt_id);

	if (msg->has_attempt_error) {
		success = success && zcbor_uint32_put(state, KEY_UPDATE_ATTEMPT_ERROR);
		success = success && zcbor_uint32_put(state, msg->attempt_error);
	} else {
		success = success &&
		    encode_index_array(state, KEY_SUCCEEDED, msg->succeeded, msg->succeeded_count);
		success = success &&
		    encode_index_array(state, KEY_FAILED, msg->failed, msg->failed_count);
		success = success &&
		    encode_index_array(state, KEY_CANCELED, msg->canceled, msg->canceled_count);
	}

	success = success && zcbor_map_end_encode(state, map_entries);

	if (!success) {
		LOG_ERR("Failed to encode OTA update results message: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*encoded_len = state->payload - buffer;

	return 0;
}

static int decode_update_artifacts(zcbor_state_t* state, struct spotflow_ota_cbor_c2d_msg* msg,
				   struct spotflow_ota_cbor_decode_status* status)
{
	bool is_canceled;

	if (search_uint32_key(state, KEY_IS_CANCELED)) {
		if (!zcbor_bool_decode(state, &is_canceled)) {
			set_attempt_error(status,
					  SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
			return -EINVAL;
		}

		msg->payload.update.is_canceled = is_canceled;
	}

	if (!search_uint32_key(state, KEY_MANIFEST) || !zcbor_list_start_decode(state)) {
		set_attempt_error(status, SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
		return -EINVAL;
	}

	bool main_seen = false;

	while (!zcbor_array_at_end(state)) {
		if (msg->payload.update.artifact_count >= SPOTFLOW_OTA_MAX_ARTIFACTS) {
			set_attempt_error(status,
					  SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_ARTIFACT_COUNT_EXCEEDED);
			return -EINVAL;
		}

		struct spotflow_ota_cbor_artifact* artifact =
		    &msg->payload.update.artifacts[msg->payload.update.artifact_count];
		enum spotflow_ota_cbor_attempt_error artifact_error;
		int rc = decode_artifact(state, artifact, &artifact_error);

		if (rc < 0) {
			set_attempt_error(status, artifact_error);
			return rc;
		}

		if (artifact->is_main) {
			if (main_seen) {
				set_attempt_error(
				    status, SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
				return -EINVAL;
			}

			main_seen = true;
		}

		msg->payload.update.artifact_count++;
	}

	if (msg->payload.update.artifact_count == 0 || !zcbor_list_end_decode(state)) {
		set_attempt_error(status, SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE);
		return -EINVAL;
	}

	return 0;
}

static int decode_artifact(zcbor_state_t* state, struct spotflow_ota_cbor_artifact* artifact,
			   enum spotflow_ota_cbor_attempt_error* error)
{
	*artifact = (struct spotflow_ota_cbor_artifact){ 0 };
	artifact->type = SPOTFLOW_OTA_CBOR_ARTIFACT_TYPE_FIRMWARE;

	if (!zcbor_unordered_map_start_decode(state)) {
		*error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE;
		return -EINVAL;
	}

	uint32_t artifact_type;

	if (!search_uint32_key(state, KEY_ARTIFACT_TYPE) ||
	    !zcbor_uint32_decode(state, &artifact_type)) {
		*error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE;
		return -EINVAL;
	}

	if (artifact_type != SPOTFLOW_OTA_CBOR_ARTIFACT_TYPE_FIRMWARE) {
		*error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_UNKNOWN_ARTIFACT_TYPE;
		return -EINVAL;
	}

	if (!search_uint32_key(state, KEY_SLUG) ||
	    !copy_tstr(state, artifact->slug, sizeof(artifact->slug),
		       SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH) ||
	    !search_uint32_key(state, KEY_URL) ||
	    !copy_tstr(state, artifact->url, sizeof(artifact->url),
		       SPOTFLOW_OTA_ARTIFACT_URL_MAX_LENGTH) ||
	    !search_uint32_key(state, KEY_SECRET) ||
	    !copy_tstr(state, artifact->secret, sizeof(artifact->secret),
		       SPOTFLOW_OTA_ARTIFACT_SECRET_MAX_LENGTH) ||
	    !search_uint32_key(state, KEY_VERSION) ||
	    !copy_tstr(state, artifact->version, sizeof(artifact->version),
		       SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH)) {
		*error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE;
		return -EINVAL;
	}

	if (search_uint32_key(state, KEY_IS_MAIN) &&
	    !zcbor_bool_decode(state, &artifact->is_main)) {
		*error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE;
		return -EINVAL;
	}

	if (!zcbor_unordered_map_end_decode(state)) {
		*error = SPOTFLOW_OTA_CBOR_ATTEMPT_ERROR_CANNOT_PARSE_MESSAGE;
		return -EINVAL;
	}

	return 0;
}

static bool search_uint32_key(zcbor_state_t* state, uint32_t key)
{
	return zcbor_unordered_map_search(decode_expected_uint32_key, state, &key);
}

static bool decode_expected_uint32_key(zcbor_state_t* state, void* expected)
{
	return zcbor_uint32_pexpect(state, expected);
}

static void set_attempt_error(struct spotflow_ota_cbor_decode_status* status,
			      enum spotflow_ota_cbor_attempt_error error)
{
	status->has_attempt_error = true;
	status->attempt_error = error;
}

static bool copy_tstr(zcbor_state_t* state, char* buffer, size_t buffer_len, size_t max_len)
{
	struct zcbor_string str;

	if (!zcbor_tstr_decode(state, &str) || str.len > max_len || str.len >= buffer_len) {
		return false;
	}

	memcpy(buffer, str.value, str.len);
	buffer[str.len] = '\0';

	return true;
}

static bool encode_index_array(zcbor_state_t* state, uint32_t key, const uint32_t* indexes,
			       size_t count)
{
	if (count == 0) {
		return true;
	}

	bool success = zcbor_uint32_put(state, key);

	success = success && zcbor_list_start_encode(state, count);

	for (size_t i = 0; success && i < count; i++) {
		success = success && zcbor_uint32_put(state, indexes[i]);
	}

	return success && zcbor_list_end_encode(state, count);
}
