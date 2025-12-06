#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cbor.h"
#include "configs/spotflow_config_cbor.h"
#include "configs/spotflow_config_persistance.h"
#include "logging/spotflow_log_backend.h"

#define MAX_KEY_COUNT 4

#define KEY_MESSAGE_TYPE 0x00
#define KEY_MINIMAL_SEVERITY 0x10
#define KEY_COMPILED_MINIMAL_SEVERITY 0x11
#define KEY_DESIRED_CONFIGURATION_VERSION 0x12
#define KEY_ACKNOWLEDGED_DESIRED_CONFIGURATION_VERSION 0x13

#define UPDATE_DESIRED_CONFIGURATION_MESSAGE_TYPE 0x03
#define UPDATE_REPORTED_CONFIGURATION_MESSAGE_TYPE 0x04

/**
 * @brief
 *
 * @param payload
 * @param len
 * @param msg
 * @return int
 */
int spotflow_config_cbor_decode_desired(const uint8_t* payload, size_t len,
					struct spotflow_config_desired_msg* msg)
{
	if (payload == NULL || len == 0) {
		SPOTFLOW_LOG("Invalid payload or length");
		return -1;
	}

	*msg = (struct spotflow_config_desired_msg){ 0 };

	CborParser parser;
	CborValue it, map_it;
	CborError err;

	err = cbor_parser_init(payload, len, 0, &parser, &it);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d", err);
		return -1;
	}

	if (!cbor_value_is_map(&it)) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Unknow map type.");
		return -1;
	}

	err = cbor_value_enter_container(&it, &map_it);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
		return -1;
	}

	while (!cbor_value_at_end(&map_it)) {
		uint64_t key;
		err = cbor_value_get_uint64(&map_it, &key);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}

		err = cbor_value_advance(&map_it);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}

		if (key == KEY_MESSAGE_TYPE) {
			uint64_t msg_type;
			err = cbor_value_get_uint64(&map_it, &msg_type);
			if (err != CborNoError) {
				SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
				return -1;
			}
			if (msg_type != UPDATE_DESIRED_CONFIGURATION_MESSAGE_TYPE) {
				SPOTFLOW_DEBUG("[CONFIG_CBOR] Unknown msg type received");
				return -1;
			}
		} else if (key == KEY_MINIMAL_SEVERITY) {
			uint64_t sev;
			err = cbor_value_get_uint64(&map_it, &sev);
			if (err != CborNoError) {
				SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
				return -1;
			}
			msg->minimal_log_severity = (uint32_t)sev;
			msg->flags |= SPOTFLOW_DESIRED_FLAG_MINIMAL_LOG_SEVERITY;
		} else if (key == KEY_DESIRED_CONFIGURATION_VERSION) {
			uint64_t version;
			err = cbor_value_get_uint64(&map_it, &version);
			if (err != CborNoError) {
				SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
				return -1;
			}
			msg->desired_config_version = version;
		} else {
			// skip unknown keys
			err = cbor_value_skip_tag(&map_it);
			if (err != CborNoError) {
				SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
				return -1;
			}
		}

		err = cbor_value_advance(&map_it);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
	}

	err = cbor_value_leave_container(&it, &map_it);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
		return -1;
	}

	return 0;
}

/**
 * @brief
 *
 * @param msg
 * @param buffer
 * @param len
 * @param encoded_len
 * @return int
 */
int spotflow_config_cbor_encode_reported(struct spotflow_config_reported_msg* msg, uint8_t* buffer,
					 size_t len, size_t* encoded_len)
{
	CborEncoder encoder, map;
	CborError err;
	if (buffer == NULL || len == 0) {
		SPOTFLOW_LOG("Invalid buffer or length");
		return -1;
	}

	cbor_encoder_init(&encoder, buffer, len, 0);

	// Start map with dynamic size
	err = cbor_encoder_create_map(&encoder, &map, CborIndefiniteLength);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
		return -1;
	}

	// Message type
	err = cbor_encode_uint(&map, KEY_MESSAGE_TYPE);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
		return -1;
	}
	err = cbor_encode_uint(&map, UPDATE_REPORTED_CONFIGURATION_MESSAGE_TYPE);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
		return -1;
	}

	// Minimal log severity
	if (msg->flags & SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL) {
		err = cbor_encode_uint(&map, KEY_MINIMAL_SEVERITY);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
		err = cbor_encode_uint(&map, msg->minimal_log_severity);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
	}

	// Compiled minimal log severity
	if (msg->flags & SPOTFLOW_REPORTED_FLAG_COMPILED_MINIMAL_LOG_SEVERITY) {
		err = cbor_encode_uint(&map, KEY_COMPILED_MINIMAL_SEVERITY);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
		err = cbor_encode_uint(&map, msg->compiled_minimal_log_severity);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
	}

	// Acked desired config version
	if (msg->flags & SPOTFLOW_REPORTED_FLAG_ACKED_DESIRED_CONFIG_VERSION) {
		err = cbor_encode_uint(&map, KEY_ACKNOWLEDGED_DESIRED_CONFIGURATION_VERSION);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
		err = cbor_encode_uint(&map, msg->acked_desired_config_version);
		if (err != CborNoError) {
			SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
			return -1;
		}
	}

	err = cbor_encoder_close_container(&encoder, &map);
	if (err != CborNoError) {
		SPOTFLOW_DEBUG("[CONFIG_CBOR] Error Code %d ", err);
		return -1;
	}

	*encoded_len = cbor_encoder_get_buffer_size(&encoder, buffer);
	return 0;
}