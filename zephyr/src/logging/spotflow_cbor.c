#include "spotflow_cbor.h"

#include <zcbor_common.h>
#include <zcbor_encode.h>

#include "spotflow_cbor_output_context.h"
#include "zephyr/logging/log.h"
#include "zephyr/logging/log_core.h"
#include "zephyr/logging/log_ctrl.h"


LOG_MODULE_REGISTER(cbor_spotflow, CONFIG_SPOTFLOW_PROCESSING_BACKEND_LOG_LEVEL);

/* optimized property keys */
#define KEY_MESSAGE_TYPE 0x00
#define KEY_BODY 0x01
#define KEY_BODY_TEMPLATE 0x02
/* (unused for now) */
#define KEY_BODY_TEMPLATE_VALUES 0x03
#define KEY_SEVERITY 0x04
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06

#define ZCBOR_STATE_DEPTH 2

struct message_metadata {
	uint32_t severity;
	uint32_t uptime_ms;
	const char *source;
};

static int encode_cbor_spotflow(const struct message_metadata* metadata,
				const char * formatted_message,
				const char * message_template,
				uint8_t buf[],
				size_t *encoded_len);
static int get_formatted_message(struct spotflow_cbor_output_context *output_context,
				uint8_t *package );
static void extract_metadata(struct message_metadata* metadata, struct log_msg *log_msg);


/*todo split*/
int spotflow_cbor_encode_message(struct log_msg *log_msg,
			struct spotflow_cbor_output_context *output_context,
			uint8_t **cbor_data,
			size_t *cbor_data_len)
{
	__ASSERT(log_msg != NULL, "log_msg is NULL");
	__ASSERT(output_context != NULL, "output_context is NULL");
	__ASSERT(cbor_data != NULL, "cbor_data is NULL");
	__ASSERT(cbor_data_len != NULL, "cbor_data_len is NULL");

	struct message_metadata metadata;
	extract_metadata(&metadata, log_msg);

	/* contains cbprint package as defined there
	https://docs.zephyrproject.org/latest/services/formatted_output.html#cbprintf-package-format */
	size_t plen;
	uint8_t *package = log_msg_get_package(log_msg, &plen);
	/* data typically contains hexdump
	uint8_t *data = log_msg_get_data(msg, &dlen); */
	get_formatted_message(output_context, package);

	/*get message template*/
	struct cbprintf_package_hdr_ext *hdr = (struct cbprintf_package_hdr_ext *)package;
	const char *message_template = hdr->fmt;

	size_t cbor_len;
	int rc = encode_cbor_spotflow(&metadata,
				output_context->log_msg,
				message_template,
				output_context->cbor_buf,
				&cbor_len);
	if (rc < 0) {
		LOG_DBG("Failed to encode spotflow log message %d", rc);
		return rc;
	}

	uint8_t *data = k_malloc(cbor_len);
	if (!data) {
		LOG_DBG("Failed to allocate memory for CBOR data");
		return -ENOMEM;
	}
	/* Copy CBOR data */
	memcpy(data, output_context->cbor_buf, cbor_len);

	*cbor_data_len = cbor_len;
	*cbor_data = data;
	return 0;
}




static int cb_out(int c, void *output_ctx)
{
	__ASSERT(output_ctx != NULL, "output_ctx is NULL");

	struct spotflow_cbor_output_context *ctx = (struct spotflow_cbor_output_context *)output_ctx;
	if (ctx->log_msg_ctr >= CONFIG_SPOTFLOW_LOG_BUFFER_SIZE)
	{
		return -ENOMEM;
	}
	ctx->log_msg[ctx->log_msg_ctr++] = (char)c;
	return 0;
}
static int get_formatted_message(struct spotflow_cbor_output_context *output_context,
				uint8_t *package ){
	__ASSERT(output_context != NULL, "output_context is NULL");
	__ASSERT(package != NULL, "package is NULL");
	output_context->log_msg_ctr = 0;
	int rc = cbpprintf(cb_out, output_context, package);
	if (output_context->log_msg_ctr >= CONFIG_SPOTFLOW_LOG_BUFFER_SIZE)
	{
		return -ENOMEM;
	}
	output_context->log_msg[output_context->log_msg_ctr++] = '\0';
	if (rc < 0)
	{
		LOG_DBG("cbprintf failed to format message: %d", rc);
		return rc;
	}
	/* returning 0 because cbrpintf returns the number of characters printed */
	return 0;
}

/* ; Integer severity values */
/* debug-severity = 30 */
/* info-severity = 40 */
/* warning-severity = 50 */
/* error-severity = 60 */
/* critical-severity = 70 */
static uint32_t level_to_severity_value(uint8_t lvl)
{
	switch (lvl)
	{
	case LOG_LEVEL_ERR:
		return 60;
	case LOG_LEVEL_WRN:
		return 50;
	case LOG_LEVEL_INF:
		return 40;
	case LOG_LEVEL_DBG:
		return 30;
	default:
		return 0; /* unknown level */
	}
}

static void extract_metadata(struct message_metadata* metadata, struct log_msg *log_msg) {
	/* get seconds from the start */
	log_timestamp_t timestamp = log_msg_get_timestamp(log_msg);
	/* convert ticks → microseconds since boot */
	uint32_t us_since_boot = log_output_timestamp_to_us(timestamp);
	metadata ->uptime_ms = us_since_boot / 1000U;

	/*log level*/
	uint8_t level = log_msg_get_level(log_msg);
	metadata->severity=level_to_severity_value(level);

	/*source name*/
	uint8_t domain_id = log_msg_get_domain(log_msg);
	int16_t source_id = log_msg_get_source_id(log_msg);
	const char *sname = source_id >= 0 ? log_source_name_get(domain_id, source_id) : "unknown";
	metadata->source = sname;

}

static int encode_message_metadata_to_cbor(const struct message_metadata* metadata,
				zcbor_state_t *state)
{
	/* severity */
	zcbor_uint32_put(state, KEY_SEVERITY);
	zcbor_uint32_put(state, metadata->severity);

	/* deviceUptimeMs */
	zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
	zcbor_uint32_put(state, metadata->uptime_ms);


	/* labels → nested map with one element */
	zcbor_uint32_put(state, KEY_LABELS);
	zcbor_map_start_encode(state, 1);
	/* key: source (full string name inside labels map) */
	zcbor_tstr_put_lit(state, "source");
	zcbor_tstr_put_term(state, metadata->source, SIZE_MAX);
	bool succ = zcbor_map_end_encode(state, 1); /* finish labels map */
	if (succ != true)
	{
		LOG_DBG("Failed to encode labels map: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	return 0;
}

static int encode_cbor_spotflow(const struct message_metadata* metadata,
				const char * formatted_message,
				const char * message_template,
				uint8_t buf[],
				size_t *encoded_len)
{
	/* zcbor supports state arrays; we need 2 states for nested array */
	zcbor_state_t state[ZCBOR_STATE_DEPTH];

	/* init for encode: 1 root item */
	/* using instead of ZCBOR_STATE_E because we need multiple state because of nested array */
	zcbor_new_encode_state(state, ZCBOR_STATE_DEPTH, buf, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, 1);

	/* start outer map with 6 key/value pairs */
	zcbor_map_start_encode(state, 6);

	/* messageType: "LOG" */
	zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	zcbor_tstr_put_lit(state, "LOG");

	int rc = encode_message_metadata_to_cbor(metadata, state);
	if (rc < 0)
	{
		LOG_DBG("Failed to encode metadata to cbor: %d", rc);
		LOG_DBG("Encoding failed: %d", zcbor_peek_error(state));
		return rc;
	}

	/* body */
	zcbor_uint32_put(state, KEY_BODY);
	zcbor_tstr_put_term(state, formatted_message, SIZE_MAX);

	/* bodyTemplate */
	zcbor_uint32_put(state, KEY_BODY_TEMPLATE);
	zcbor_tstr_put_term(state, message_template, SIZE_MAX);

	/* finish cbor */
	bool succ = zcbor_map_end_encode(state, 6);

	if (succ != true)
	{
		LOG_DBG("Failed to encode cbor: %d", zcbor_peek_error(state));
		LOG_DBG("Encoding failed: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	/* calculate encoded length */
	*encoded_len = state->payload - buf;
	return 0;
}

