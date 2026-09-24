#include "spotflow_log_message.h"

#include "spotflow_log_level.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/sys/cbprintf.h>
#include <zephyr/sys/time_units.h>

#ifdef CONFIG_LOG_OUTPUT
#include <zephyr/logging/log_output.h>
#endif /* CONFIG_LOG_OUTPUT */

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

#ifndef CONFIG_SPOTFLOW_COMPACT_LOGS
struct format_context {
	char* buffer;
	size_t len;
	size_t used;
};

static int cb_out(int c, void* context);
static int get_formatted_message(char* buffer, size_t len, uint8_t* package);
#endif /* !CONFIG_SPOTFLOW_COMPACT_LOGS */
static void extract_metadata(struct spotflow_log_cbor_msg* metadata, struct log_msg* log_msg,
			     size_t sequence_number);
static uint64_t timestamp_to_us(log_timestamp_t timestamp);
#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
static int validate_compact_package(const uint8_t* package, size_t package_len);
static int next_embedded_string(const void* context, size_t* cursor,
				struct spotflow_log_embedded_string* string);
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */

int spotflow_log_message_prepare(struct log_msg* log_msg, size_t sequence_number,
				 struct spotflow_log_message* message, char* formatted,
				 size_t formatted_len)
{
	if (log_msg == NULL || message == NULL) {
		LOG_ERR("Invalid log preparation arguments");
		return -EINVAL;
	}
	*message = (struct spotflow_log_message){ 0 };
	extract_metadata(&message->cbor, log_msg, sequence_number);
	size_t package_len;
	uint8_t* package = log_msg_get_package(log_msg, &package_len);
#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
	ARG_UNUSED(formatted);
	ARG_UNUSED(formatted_len);
	int rc = validate_compact_package(package, package_len);
	if (rc < 0) {
		LOG_DBG("Invalid compact log package: %d", rc);
		return rc;
	}
	struct cbprintf_package_hdr_ext hdr;
	memcpy(&hdr, package, sizeof(hdr));
	size_t args_end = hdr.hdr.desc.len * sizeof(int);
	size_t strings_start = args_end + hdr.hdr.desc.ro_str_cnt;
	message->strings.data = package + strings_start;
	message->strings.len = package_len - strings_start;
	message->cbor.body_type = SPOTFLOW_LOG_BODY_COMPACT;
	message->cbor.body.compact = (struct spotflow_log_compact_body){
		.template_address = (uint64_t)(uintptr_t)hdr.fmt,
		.argument_data = package + sizeof(hdr),
		.argument_len = args_end - sizeof(hdr),
		.string_count = hdr.hdr.desc.str_cnt,
		.next_string = next_embedded_string,
		.string_context = &message->strings,
	};
#else
	if (formatted == NULL || formatted_len == 0 ||
	    package_len < sizeof(struct cbprintf_package_hdr_ext)) {
		LOG_ERR("Invalid log formatting buffer or package");
		return -EINVAL;
	}
	int rc = get_formatted_message(formatted, formatted_len, package);
	if (rc < 0) {
		return rc;
	}
	message->cbor.body_type = SPOTFLOW_LOG_BODY_TEXT;
	message->cbor.body.text.text = formatted;
#if CONFIG_SPOTFLOW_LOG_INCLUDE_BODY_TEMPLATE
	struct cbprintf_package_hdr_ext hdr;
	memcpy(&hdr, package, sizeof(hdr));
	message->cbor.body.text.body_template = hdr.fmt;
#endif /* CONFIG_SPOTFLOW_LOG_INCLUDE_BODY_TEMPLATE */
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */
	return 0;
}

#ifndef CONFIG_SPOTFLOW_COMPACT_LOGS
static int cb_out(int c, void* context)
{
	struct format_context* ctx = context;
	if (ctx->used >= ctx->len) {
		return -ENOMEM;
	}
	ctx->buffer[ctx->used++] = (char)c;
	return 0;
}

static int get_formatted_message(char* buffer, size_t len, uint8_t* package)
{
	struct format_context ctx = { .buffer = buffer, .len = len };
	int rc = cbpprintf(cb_out, &ctx, package);
	if (ctx.used >= len) {
		LOG_DBG("Log message buffer overflow, total length: %zu bytes", ctx.used);
		return -ENOMEM;
	}
	buffer[ctx.used] = '\0';
	if (rc < 0) {
		LOG_DBG("cbprintf failed to format message: %d", rc);
		return rc;
	}
	return 0;
}
#endif /* !CONFIG_SPOTFLOW_COMPACT_LOGS */

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

static void extract_metadata(struct spotflow_log_cbor_msg* metadata, struct log_msg* log_msg,
			     size_t sequence_number)
{
	metadata->sequence_number = sequence_number;

	/* Convert timestamp to milliseconds since boot. */
	log_timestamp_t timestamp = log_msg_get_timestamp(log_msg);
	uint64_t us_since_boot = timestamp_to_us(timestamp);
	metadata->uptime_ms = us_since_boot / 1000U;

	/* log level */
	uint8_t level = log_msg_get_level(log_msg);
	metadata->severity = spotflow_log_level_to_severity(level);

	/* source name */
	uint8_t domain_id = log_msg_get_domain(log_msg);
	int16_t source_id = log_msg_get_source_id(log_msg);
	const char* sname = source_id >= 0 ? log_source_name_get(domain_id, source_id) : "unknown";
	metadata->source = sname;
}

#ifdef CONFIG_SPOTFLOW_COMPACT_LOGS
/* Validate the full package before exposing its borrowed fields to the encoder.
 * Deferred messages have already copied transient argument strings.
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

static int next_embedded_string(const void* context, size_t* cursor,
				struct spotflow_log_embedded_string* string)
{
	if (context == NULL || cursor == NULL || string == NULL) {
		return -EINVAL;
	}
	const struct spotflow_log_string_data* strings = context;
	if (*cursor == strings->len) {
		return 0;
	}
	if (*cursor > strings->len) {
		return -EINVAL;
	}
	size_t position = *cursor;
	size_t offset = strings->data[position++] * sizeof(int);
	const uint8_t* value = strings->data + position;
	const uint8_t* end = memchr(value, '\0', strings->len - position);
	if (end == NULL || offset < sizeof(struct cbprintf_package_hdr_ext)) {
		return -EINVAL;
	}
	*string = (struct spotflow_log_embedded_string){
		.argument_offset = offset - sizeof(struct cbprintf_package_hdr_ext),
		.value = (const char*)value,
		.len = end - value,
	};
	*cursor = position + string->len + 1;
	return 1;
}
#endif /* CONFIG_SPOTFLOW_COMPACT_LOGS */
