#include "spotflow_log_cbor.h"

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

/* optimized property keys */
#define KEY_MESSAGE_TYPE 0x00
#define LOGS_MESSAGE_TYPE 0x00
#define KEY_BODY 0x01
#define KEY_BODY_TEMPLATE 0x02
#define KEY_SEVERITY 0x04
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_SEQUENCE_NUMBER 0x0D
#define KEY_COMPACT_BODY_TEMPLATE 46
#define KEY_COMPACT_BODY_TEMPLATE_VALUES 47
#define KEY_COMPACT_EMBEDDED_STRINGS 48

static bool encode_metadata(zcbor_state_t* state, const struct spotflow_log_cbor_msg* msg);
static int encode_compact_body(zcbor_state_t* state, const struct spotflow_log_compact_body* body);

int spotflow_log_cbor_encode(const struct spotflow_log_cbor_msg* msg, uint8_t* buffer, size_t len,
			     size_t* encoded_len)
{
	if (msg == NULL || buffer == NULL || len == 0 || encoded_len == NULL ||
	    msg->source == NULL) {
		LOG_ERR("Invalid log encoding arguments");
		return -EINVAL;
	}
	size_t pairs = 5;
	switch (msg->body_type) {
	case SPOTFLOW_LOG_BODY_TEXT:
		if (msg->body.text.text == NULL) {
			LOG_ERR("Missing log text");
			return -EINVAL;
		}
		pairs += 1 + (msg->body.text.body_template != NULL);
		break;
	case SPOTFLOW_LOG_BODY_COMPACT:
		if ((msg->body.compact.argument_len && msg->body.compact.argument_data == NULL) ||
		    (msg->body.compact.string_count && msg->body.compact.next_string == NULL)) {
			LOG_ERR("Invalid compact log fields");
			return -EINVAL;
		}
		pairs += 1 + (msg->body.compact.argument_len > 0) +
			(msg->body.compact.string_count > 0);
		break;
	default:
		LOG_ERR("Invalid log body type");
		return -EINVAL;
	}

	/* Root and nested maps require two backups in canonical mode. */
	ZCBOR_STATE_E(state, 2, buffer, len, 1);
	bool success = zcbor_map_start_encode(state, pairs) &&
		zcbor_uint32_put(state, KEY_MESSAGE_TYPE) &&
		zcbor_uint32_put(state, LOGS_MESSAGE_TYPE) && encode_metadata(state, msg);
	if (success && msg->body_type == SPOTFLOW_LOG_BODY_COMPACT) {
		int rc = encode_compact_body(state, &msg->body.compact);
		if (rc < 0) {
			LOG_DBG("Failed to encode compact log body: %d", rc);
			return rc;
		}
	} else if (success) {
		success = zcbor_uint32_put(state, KEY_BODY) &&
			zcbor_tstr_put_term(state, msg->body.text.text, SIZE_MAX);
		if (msg->body.text.body_template != NULL) {
			success = success && zcbor_uint32_put(state, KEY_BODY_TEMPLATE) &&
				zcbor_tstr_put_term(state, msg->body.text.body_template, SIZE_MAX);
		}
	}
	success = success && zcbor_map_end_encode(state, pairs);
	if (!success) {
		LOG_DBG("Failed to encode log: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	*encoded_len = state->payload - buffer;
	return 0;
}

static bool encode_metadata(zcbor_state_t* state, const struct spotflow_log_cbor_msg* msg)
{
	return zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER) &&
		zcbor_uint32_put(state, msg->sequence_number) &&
		zcbor_uint32_put(state, KEY_SEVERITY) && zcbor_uint32_put(state, msg->severity) &&
		zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS) &&
		zcbor_uint32_put(state, msg->uptime_ms) && zcbor_uint32_put(state, KEY_LABELS) &&
		zcbor_map_start_encode(state, 1) && zcbor_tstr_put_lit(state, "source") &&
		zcbor_tstr_put_term(state, msg->source, SIZE_MAX) && zcbor_map_end_encode(state, 1);
}

static int encode_compact_body(zcbor_state_t* state, const struct spotflow_log_compact_body* body)
{
	bool success = zcbor_uint32_put(state, KEY_COMPACT_BODY_TEMPLATE) &&
		zcbor_uint64_put(state, body->template_address);
	if (body->argument_len > 0) {
		success = success && zcbor_uint32_put(state, KEY_COMPACT_BODY_TEMPLATE_VALUES) &&
			zcbor_bstr_encode_ptr(state, body->argument_data, body->argument_len);
	}
	if (body->string_count > 0) {
		success = success && zcbor_uint32_put(state, KEY_COMPACT_EMBEDDED_STRINGS) &&
			zcbor_map_start_encode(state, body->string_count);
		size_t cursor = 0;
		for (size_t i = 0; success && i < body->string_count; ++i) {
			struct spotflow_log_embedded_string string;
			int rc = body->next_string(body->string_context, &cursor, &string);
			if (rc != 1) {
				return rc < 0 ? rc : -EINVAL;
			}
			if (string.value == NULL) {
				return -EINVAL;
			}
			success = zcbor_uint32_put(state, string.argument_offset) &&
				zcbor_tstr_encode_ptr(state, string.value, string.len);
		}
		if (success) {
			struct spotflow_log_embedded_string string;
			int rc = body->next_string(body->string_context, &cursor, &string);
			if (rc != 0) {
				return rc < 0 ? rc : -EINVAL;
			}
		}
		success = success && zcbor_map_end_encode(state, body->string_count);
	}
	return success ? 0 : -EINVAL;
}
