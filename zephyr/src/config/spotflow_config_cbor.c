#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <zephyr/logging/log.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "config/spotflow_config_cbor.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define MAX_KEY_COUNT 4

#define KEY_MESSAGE_TYPE 0x00
#define KEY_MINIMAL_SEVERITY 0x10
#define KEY_COMPILED_MINIMAL_SEVERITY 0x11
#define KEY_DESIRED_CONFIGURATION_VERSION 0x12
#define KEY_ACKNOWLEDGED_DESIRED_CONFIGURATION_VERSION 0x13

#define UPDATE_DESIRED_CONFIGURATION_MESSAGE_TYPE 0x03
#define UPDATE_REPORTED_CONFIGURATION_MESSAGE_TYPE 0x04

#define ZCBOR_STATE_DEPTH 1

int spotflow_config_cbor_decode_desired(uint8_t* payload, size_t len,
					struct spotflow_config_desired_msg* msg)
{
	if (payload == NULL || len == 0) {
		LOG_ERR("Invalid payload or length");
		return -EINVAL;
	}

	*msg = (struct spotflow_config_desired_msg){ 0 };

	ZCBOR_STATE_D(state, ZCBOR_STATE_DEPTH, payload, len, 1, 0);

	bool success = zcbor_map_start_decode(state);

	success = success && zcbor_uint32_expect(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_expect(state, UPDATE_DESIRED_CONFIGURATION_MESSAGE_TYPE);

	uint32_t key;
	success = success && zcbor_uint32_decode(state, &key);

	if (success && key == KEY_MINIMAL_SEVERITY) {
		msg->contains_minimal_log_severity = true;
		success = success && zcbor_uint32_decode(state, &msg->minimal_log_severity);

		success = success && zcbor_uint32_decode(state, &key);
	}

	if (success && key != KEY_DESIRED_CONFIGURATION_VERSION) {
		LOG_ERR("Desired configuration version key not found");
		return -EINVAL;
	}

	success = success && zcbor_uint64_decode(state, &msg->desired_config_version);

	if (!success) {
		LOG_ERR("Failed to decode desired configuration message: %d",
			zcbor_peek_error(state));
		return -EINVAL;
	}

	return 0;
}

int spotflow_config_cbor_encode_reported(struct spotflow_config_reported_msg* msg, uint8_t* buffer,
					 size_t len, size_t* encoded_len)
{
	if (buffer == NULL || len == 0) {
		LOG_ERR("Invalid buffer or length");
		return -EINVAL;
	}

	ZCBOR_STATE_E(state, ZCBOR_STATE_DEPTH, buffer, len, 0);

	bool success = zcbor_map_start_encode(state, MAX_KEY_COUNT);

	success = success && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	success = success && zcbor_uint32_put(state, UPDATE_REPORTED_CONFIGURATION_MESSAGE_TYPE);

	if (msg->contains_minimal_log_severity) {
		success = success && zcbor_uint32_put(state, KEY_MINIMAL_SEVERITY);
		success = success && zcbor_uint32_put(state, msg->minimal_log_severity);
	}

	if (msg->contains_compiled_minimal_log_severity) {
		success = success && zcbor_uint32_put(state, KEY_COMPILED_MINIMAL_SEVERITY);
		success = success && zcbor_uint32_put(state, msg->compiled_minimal_log_severity);
	}

	if (msg->contains_acked_desired_config_version) {
		success = success &&
		    zcbor_uint32_put(state, KEY_ACKNOWLEDGED_DESIRED_CONFIGURATION_VERSION);
		success = success && zcbor_uint64_put(state, msg->acked_desired_config_version);
	}

	success = success && zcbor_map_end_encode(state, MAX_KEY_COUNT);

	if (!success) {
		LOG_ERR("Failed to encode reported configuration message: %d",
			zcbor_peek_error(state));
		return -EINVAL;
	}

	*encoded_len = state->payload - buffer;

	return 0;
}
