#include <stdint.h>
#include <errno.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "spotflow_log_filter.h"
#include "spotflow_log_cbor.h"

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

#define MAX_KEY_COUNT 4

#define KEY_MESSAGE_TYPE 0x00
#define KEY_MINIMAL_SEVERITY 0x10
#define KEY_COMPILED_MINIMAL_SEVERITY 0x11
#define KEY_CONFIGURATION_VERSION 0x12

#define UPDATE_LOG_SEVERITY_MESSAGE_TYPE 0x03
#define REPORT_LOG_SEVERITY_MESSAGE_TYPE 0x04

#define ZCBOR_STATE_DEPTH 1

static volatile uint8_t sent_log_level = CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL;

bool spotflow_log_filter_allow_msg(struct log_msg* log_msg)
{
	uint8_t level = log_msg_get_level(log_msg);
	return level <= sent_log_level;
}

int spotflow_log_filter_update_from_c2d_message(uint8_t* payload, size_t len,
						uint64_t* configuration_version)
{
	if (payload == NULL || len == 0) {
		LOG_ERR("Invalid payload or length");
		return -EINVAL;
	}

	ZCBOR_STATE_D(state, ZCBOR_STATE_DEPTH, payload, len, 1, 0);

	bool success = zcbor_map_start_decode(state);

	uint32_t message_type;
	success = success && zcbor_uint32_expect(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_decode(state, &message_type);

	if (!success) {
		LOG_ERR("Failed to decode message type");
		return -EINVAL;
	}

	if (message_type != UPDATE_LOG_SEVERITY_MESSAGE_TYPE) {
		LOG_DBG("Skipping message with type %d (expected %d)", message_type,
			UPDATE_LOG_SEVERITY_MESSAGE_TYPE);
		return 0;
	}

	uint32_t minimal_severity;
	success = success && zcbor_uint32_expect(state, KEY_MINIMAL_SEVERITY);
	success = success && zcbor_uint32_decode(state, &minimal_severity);

	success = success && zcbor_uint32_expect(state, KEY_CONFIGURATION_VERSION);
	success = success && zcbor_uint64_decode(state, configuration_version);

	if (!success) {
		LOG_ERR("Failed to decode message content");
		return -EINVAL;
	}

	uint8_t log_level = spotflow_cbor_convert_severity_to_log_level(minimal_severity);
	sent_log_level = log_level;
	LOG_DBG("Updated sent log level to %d", log_level);

	return 0;
}

int spotflow_log_filter_encode_report_cbor(uint8_t* buffer, size_t len,
					   uint64_t* configuration_version)
{
	if (buffer == NULL || len == 0) {
		LOG_ERR("Invalid buffer or length");
		return -EINVAL;
	}

	ZCBOR_STATE_E(state, ZCBOR_STATE_DEPTH, buffer, len, 0);

	bool success = zcbor_map_start_encode(state, MAX_KEY_COUNT);

	success = success && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, REPORT_LOG_SEVERITY_MESSAGE_TYPE);

	uint32_t minimal_severity = spotflow_cbor_convert_log_level_to_severity(sent_log_level);
	success = success && zcbor_uint32_put(state, KEY_MINIMAL_SEVERITY);
	success = success && zcbor_uint32_put(state, minimal_severity);

	uint32_t compiled_minimal_severity =
	    spotflow_cbor_convert_log_level_to_severity(CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL);
	success = success && zcbor_uint32_put(state, KEY_COMPILED_MINIMAL_SEVERITY);
	success = success && zcbor_uint32_put(state, compiled_minimal_severity);

	if (configuration_version != NULL) {
		success = success && zcbor_uint32_put(state, KEY_CONFIGURATION_VERSION);
		success = success && zcbor_uint64_put(state, *configuration_version);
	}

	success = success && zcbor_map_end_encode(state, MAX_KEY_COUNT);

	if (!success) {
		LOG_ERR("Failed to encode message content");
		return -EINVAL;
	}

	return 0;
}
