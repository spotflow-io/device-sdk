#include "spotflow_log_cbor.h"

#include <zcbor_common.h>
#include <zcbor_encode.h>

#include "spotflow_cbor_output_context.h"
#include "zephyr/logging/log.h"
#include "zephyr/logging/log_core.h"
#include "zephyr/logging/log_ctrl.h"
#include <zephyr/sys/time_units.h>

#ifdef CONFIG_LOG_OUTPUT
#include <zephyr/logging/log_output.h>
#endif /* CONFIG_LOG_OUTPUT */

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

/* optimized property keys */
#define KEY_MESSAGE_TYPE 0x00
#define LOGS_MESSAGE_TYPE 0x00
#define KEY_BODY 0x01
#define KEY_BODY_TEMPLATE 0x02
/* (unused for now) */
#define KEY_BODY_TEMPLATE_VALUES 0x03
#define KEY_SEVERITY 0x04
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_SEQUENCE_NUMBER 0x0D
#define KEY_COMPACT_BODY_TEMPLATE 46
#define KEY_COMPACT_BODY_TEMPLATE_VALUES 47
#define KEY_COMPACT_EMBEDDED_STRINGS 48

struct message_metadata {
	uint32_t severity;
	uint32_t uptime_ms;
	size_t sequence_number;
	const char* source;
};

static int encode_cbor_spotflow(const struct message_metadata* metadata,
				const char* formatted_message, const char* message_template,
				const uint8_t* compact_package, size_t package_len, uint8_t buf[],
				size_t* encoded_len);
static int get_formatted_message(struct spotflow_cbor_output_context* output_context,
				 uint8_t* package);
static void extract_metadata(struct message_metadata* metadata, struct log_msg* log_msg,
			     size_t sequence_number);
static uint64_t timestamp_to_us(log_timestamp_t timestamp);

#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
static int validate_compact_package(const uint8_t* package, size_t package_len);
static bool encode_compact_body(zcbor_state_t* state, const uint8_t* package, size_t package_len);
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */

int spotflow_cbor_encode_log(struct log_msg* log_msg, size_t sequence_number,
			     struct spotflow_cbor_output_context* output_context,
			     uint8_t** cbor_data, size_t* cbor_data_len)
{
	__ASSERT(log_msg != NULL, "log_msg is NULL");
	__ASSERT(output_context != NULL, "output_context is NULL");
	__ASSERT(cbor_data != NULL, "cbor_data is NULL");
	__ASSERT(cbor_data_len != NULL, "cbor_data_len is NULL");

	struct message_metadata metadata;
	extract_metadata(&metadata, log_msg, sequence_number);

	/* contains cbprint package as defined there
	https://docs.zephyrproject.org/latest/services/formatted_output.html#cbprintf-package-format */
	size_t plen;
	uint8_t* package = log_msg_get_package(log_msg, &plen);
	const uint8_t* compact_package = NULL;
	int rc;
#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
	rc = validate_compact_package(package, plen);
	if (rc < 0) {
		return rc;
	}
	compact_package = package;
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */
	if (compact_package == NULL) {
		rc = get_formatted_message(output_context, package);
		if (rc < 0) {
			LOG_DBG("Failed to get formatted message: %d", rc);
			return rc;
		}
	}

	/* get message template */
	struct cbprintf_package_hdr_ext* hdr = (struct cbprintf_package_hdr_ext*)package;
	const char* message_template = hdr->fmt;

	size_t cbor_len;
	rc = encode_cbor_spotflow(&metadata, output_context->log_msg, message_template,
				  compact_package, plen, output_context->cbor_buf, &cbor_len);
	if (rc < 0) {
		LOG_DBG("Failed to encode spotflow log message %d", rc);
		return rc;
	}

	uint8_t* data = k_malloc(cbor_len);
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

static int cb_out(int c, void* output_ctx)
{
	__ASSERT(output_ctx != NULL, "output_ctx is NULL");

	struct spotflow_cbor_output_context* ctx = (struct spotflow_cbor_output_context*)output_ctx;
	if (ctx->log_msg_ctr >= CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
		return -ENOMEM;
	}
	ctx->log_msg[ctx->log_msg_ctr++] = (char)c;
	return 0;
}

static int get_formatted_message(struct spotflow_cbor_output_context* output_context,
				 uint8_t* package)
{
	__ASSERT(output_context != NULL, "output_context is NULL");
	__ASSERT(package != NULL, "package is NULL");
	output_context->log_msg_ctr = 0;
	int rc = cbpprintf(cb_out, output_context, package);
	if (output_context->log_msg_ctr >= CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
		LOG_DBG("Log message buffer overflow, total length: %zu bytes",
			output_context->log_msg_ctr);
		return -ENOMEM;
	}
	output_context->log_msg[output_context->log_msg_ctr++] = '\0';
	if (rc < 0) {
		LOG_DBG("cbprintf failed to format message: %d", rc);
		return rc;
	}
	/* returning 0 because cbprintf returns the number of characters printed */
	return 0;
}

/* Integer severity values */
/* debug-severity = 30 */
/* info-severity = 40 */
/* warning-severity = 50 */
/* error-severity = 60 */
/* critical-severity = 70 */

uint32_t spotflow_cbor_convert_log_level_to_severity(uint8_t lvl)
{
	switch (lvl) {
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

uint8_t spotflow_cbor_convert_severity_to_log_level(uint32_t severity)
{
	switch (severity) {
	case 70:
	case 60:
		return LOG_LEVEL_ERR;
	case 50:
		return LOG_LEVEL_WRN;
	case 40:
		return LOG_LEVEL_INF;
	case 30:
		return LOG_LEVEL_DBG;
	default:
		return LOG_LEVEL_DBG;
	}
}

static uint64_t timestamp_to_us(log_timestamp_t timestamp)
{
#ifdef CONFIG_LOG_OUTPUT
	return log_output_timestamp_to_us(timestamp);
#else
	/* Mirror log_core_init() in Zephyr 3.7 and 4.1-4.4 (also NCS 3.0/3.1).
	 * The logging core does not expose the active timestamp frequency, so
	 * this fallback supports only its default timestamp sources.
	 */
	if (IS_ENABLED(CONFIG_LOG_TIMESTAMP_USE_REALTIME) ||
	    sys_clock_hw_cycles_per_sec() > 1000000U) {
		return (uint64_t)timestamp * 1000U;
	}
	if (IS_ENABLED(CONFIG_LOG_TIMESTAMP_64BIT)) {
		return k_ticks_to_us_floor64(timestamp);
	}
	return k_cyc_to_us_floor64(timestamp);
#endif /* CONFIG_LOG_OUTPUT */
}

static void extract_metadata(struct message_metadata* metadata, struct log_msg* log_msg,
			     size_t sequence_number)
{
	metadata->sequence_number = sequence_number;

	/* Convert timestamp to milliseconds since boot. */
	log_timestamp_t timestamp = log_msg_get_timestamp(log_msg);
	uint64_t us_since_boot = timestamp_to_us(timestamp);
	metadata->uptime_ms = us_since_boot / 1000U;

	/* log level */
	uint8_t level = log_msg_get_level(log_msg);
	metadata->severity = spotflow_cbor_convert_log_level_to_severity(level);

	/* source name */
	uint8_t domain_id = log_msg_get_domain(log_msg);
	int16_t source_id = log_msg_get_source_id(log_msg);
	const char* sname = source_id >= 0 ? log_source_name_get(domain_id, source_id) : "unknown";
	metadata->source = sname;
}

static int encode_message_metadata_to_cbor(const struct message_metadata* metadata,
					   zcbor_state_t* state)
{
	zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER);
	zcbor_uint32_put(state, metadata->sequence_number);
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
	if (succ != true) {
		LOG_DBG("Failed to encode labels map: %d", zcbor_peek_error(state));
		return -EINVAL;
	}
	return 0;
}

static int encode_cbor_spotflow(const struct message_metadata* metadata,
				const char* formatted_message, const char* message_template,
				const uint8_t* compact_package, size_t package_len, uint8_t buf[],
				size_t* encoded_len)
{
	/* Five metadata entries, plus up to three compact body entries. */
	const size_t map_key_value_pairs =
		compact_package ? 8 : (6 + IS_ENABLED(CONFIG_SPOTFLOW_LOG_INCLUDE_BODY_TEMPLATE));

	/* Root map and nested maps require two backups in canonical mode. */
	ZCBOR_STATE_E(state, 2, buf, CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN, 1);

	bool succ;

	/* start outer map */
	succ = zcbor_map_start_encode(state, map_key_value_pairs);

	/* messageType: "LOG" */
	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, LOGS_MESSAGE_TYPE);

	int rc = encode_message_metadata_to_cbor(metadata, state);
	if (rc < 0) {
		LOG_DBG("Failed to encode metadata to cbor: %d", rc);
		LOG_DBG("Encoding failed: %d", zcbor_peek_error(state));
		return rc;
	}

#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
	if (compact_package != NULL) {
		succ = succ && encode_compact_body(state, compact_package, package_len);
	}
#else
	ARG_UNUSED(package_len);
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */
	if (compact_package == NULL) {
		/* body */
		succ = succ && zcbor_uint32_put(state, KEY_BODY);
		succ = succ && zcbor_tstr_put_term(state, formatted_message, SIZE_MAX);

#if CONFIG_SPOTFLOW_LOG_INCLUDE_BODY_TEMPLATE
		/* bodyTemplate */
		succ = succ && zcbor_uint32_put(state, KEY_BODY_TEMPLATE);
		succ = succ && zcbor_tstr_put_term(state, message_template, SIZE_MAX);
#endif
	}

	/* finish cbor */
	succ = succ && zcbor_map_end_encode(state, map_key_value_pairs);

	if (succ != true) {
		LOG_DBG("Failed to encode cbor: %d", zcbor_peek_error(state));
		LOG_DBG("Encoding failed: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	/* calculate encoded length */
	*encoded_len = state->payload - buf;
	return 0;
}

#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
/* Validate before encoding: string positions are word offsets
 * from the package base, while CBOR uses byte offsets after the header.
 * Deferred Zephyr messages have already copied all transient argument strings.
 */
static int validate_compact_package(const uint8_t* package, size_t package_len)
{
	if (package_len < sizeof(struct cbprintf_package_hdr_ext)) {
		return -EINVAL;
	}
	struct cbprintf_package_hdr_ext hdr;
	memcpy(&hdr, package, sizeof(hdr));
	size_t args_end = hdr.hdr.desc.len * sizeof(int);
	size_t strings_start = args_end + hdr.hdr.desc.ro_str_cnt;
	if (args_end < sizeof(hdr) || strings_start > package_len || hdr.hdr.desc.rw_str_cnt) {
		return -EINVAL;
	}
	const uint8_t* cursor = package + strings_start;
	const uint8_t* end = package + package_len;
	/* One bit for each possible eight-bit string-slot index. */
	uint8_t seen[32] = { 0 };
	for (size_t i = 0; i < hdr.hdr.desc.str_cnt; ++i) {
		if (cursor == end) {
			return -EINVAL;
		}
		uint8_t index = *cursor++;
		size_t offset = index * sizeof(int);
		/* Reject duplicate slots, which would produce duplicate CBOR map keys. */
		if (seen[index / 8] & BIT(index % 8)) {
			return -EINVAL;
		}
		seen[index / 8] |= BIT(index % 8);
		/* Only argument slots are supported; embedded format strings are rejected. */
		if (offset < sizeof(hdr) || offset > args_end - sizeof(char*)) {
			return -EINVAL;
		}
		const uint8_t* terminator = memchr(cursor, '\0', end - cursor);
		if (terminator == NULL) {
			return -EINVAL;
		}
		cursor = terminator + 1;
	}
	if (cursor != end) {
		return -EINVAL;
	}
	return 0;
}

static bool encode_compact_body(zcbor_state_t* state, const uint8_t* package, size_t package_len)
{
	struct cbprintf_package_hdr_ext hdr;
	memcpy(&hdr, package, sizeof(hdr));
	size_t args_end = hdr.hdr.desc.len * sizeof(int);
	const uint8_t* strings = package + args_end + hdr.hdr.desc.ro_str_cnt;
	const uint8_t* cursor = strings;
	bool succ = zcbor_uint32_put(state, KEY_COMPACT_BODY_TEMPLATE) &&
		zcbor_uint64_put(state, (uint64_t)(uintptr_t)hdr.fmt);
	if (args_end > sizeof(hdr)) {
		succ = succ && zcbor_uint32_put(state, KEY_COMPACT_BODY_TEMPLATE_VALUES) &&
			zcbor_bstr_encode_ptr(state, package + sizeof(hdr), args_end - sizeof(hdr));
	}
	if (hdr.hdr.desc.str_cnt) {
		succ = succ && zcbor_uint32_put(state, KEY_COMPACT_EMBEDDED_STRINGS) &&
			zcbor_map_start_encode(state, hdr.hdr.desc.str_cnt);
		for (size_t i = 0; succ && i < hdr.hdr.desc.str_cnt; ++i) {
			size_t offset = *cursor++ * sizeof(int);
			const uint8_t* terminator =
				memchr(cursor, '\0', package + package_len - cursor);
			succ = zcbor_uint32_put(state, offset - sizeof(hdr)) &&
				zcbor_tstr_encode_ptr(state, cursor, terminator - cursor);
			cursor = terminator + 1;
		}
		succ = succ && zcbor_map_end_encode(state, hdr.hdr.desc.str_cnt);
	}
	return succ;
}
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */
