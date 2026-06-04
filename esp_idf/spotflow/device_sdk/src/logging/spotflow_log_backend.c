#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spotflow.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_cbor.h"
// static const char *TAG = "spotflow_testing"; // Currently the Spotflow_LOG doesn't support tags

typedef enum {
	SPOTFLOW_LOG_PREFIX_NONE,
	SPOTFLOW_LOG_PREFIX_V1_TIME_INT,
	SPOTFLOW_LOG_PREFIX_V1_TIME_UINT,
	SPOTFLOW_LOG_PREFIX_V1_TIME_ULONG,
	SPOTFLOW_LOG_PREFIX_V1_TIME_STRING,
} spotflow_log_prefix_t;

int spotflow_log_backend(const char* fmt, va_list args)
{
	static size_t sequence = 0;
	struct message_metadata metadata = { 0 };
	metadata.sequence_number = sequence;
	sequence++;

	const char* body_fmt = fmt;
	const char* prefix_end = NULL;
	spotflow_log_prefix_t esp_prefix = SPOTFLOW_LOG_PREFIX_NONE;

	/*
	 * ESP-IDF Log V1 embeds the level as a literal char in fmt.
	 * Supported prefixes: ^[EWIDV] \(%(d|i|u|lu|s)\) %s: .*
	 * Args start with timestamp and tag/source; severity is not a va_arg.
	 */
	if ((fmt[0] == 'E' || fmt[0] == 'W' || fmt[0] == 'I' || fmt[0] == 'D' || fmt[0] == 'V') &&
	    strncmp(fmt + 1, " (", 2) == 0) {
		if (strncmp(fmt + 3, "%d) %s:", 7) == 0 || strncmp(fmt + 3, "%i) %s:", 7) == 0) {
			esp_prefix = SPOTFLOW_LOG_PREFIX_V1_TIME_INT;
			prefix_end = fmt + 10;
		} else if (strncmp(fmt + 3, "%u) %s:", 7) == 0) {
			esp_prefix = SPOTFLOW_LOG_PREFIX_V1_TIME_UINT;
			prefix_end = fmt + 10;
		} else if (strncmp(fmt + 3, "%lu) %s:", 8) == 0) {
			esp_prefix = SPOTFLOW_LOG_PREFIX_V1_TIME_ULONG;
			prefix_end = fmt + 11;
		} else if (strncmp(fmt + 3, "%s) %s:", 7) == 0) {
			esp_prefix = SPOTFLOW_LOG_PREFIX_V1_TIME_STRING;
			prefix_end = fmt + 10;
		}
	}

	va_list args_after_prefix;
	va_copy(args_after_prefix, args);
	if (esp_prefix != SPOTFLOW_LOG_PREFIX_NONE) {
		switch (esp_prefix) {
		case SPOTFLOW_LOG_PREFIX_V1_TIME_INT:
			metadata.uptime_ms = va_arg(args_after_prefix, int);
			break;
		case SPOTFLOW_LOG_PREFIX_V1_TIME_UINT:
			metadata.uptime_ms = va_arg(args_after_prefix, unsigned int);
			break;
		case SPOTFLOW_LOG_PREFIX_V1_TIME_ULONG:
			metadata.uptime_ms = va_arg(args_after_prefix, unsigned long);
			break;
		case SPOTFLOW_LOG_PREFIX_V1_TIME_STRING:
			va_arg(args_after_prefix, const char*);
			break;
		case SPOTFLOW_LOG_PREFIX_NONE:
			/* Unreachable: the outer guard excludes NONE; kept for -Wswitch. */
			break;
		}
		metadata.source = va_arg(args_after_prefix, const char*);

		metadata.severity = spotflow_log_cbor_convert_char_log_lvl(fmt[0]);
		body_fmt = prefix_end;
		if (*body_fmt == ' ') {
			body_fmt++;
		}
	}

	// Each va_list consumer needs its own copy; vsnprintf/va_arg leave it exhausted.
	va_list args_len;
	va_copy(args_len, args_after_prefix);
	int len = vsnprintf(NULL, 0, body_fmt, args_len); // Get the required length
	va_end(args_len);

	// If the len is smaller than 0 or if the log is bigger than the set buffer size.
	if (len < 0 || len >= CONFIG_SPOTFLOW_LOG_BUFFER_SIZE) {
		va_end(args_after_prefix);
		return 0;
	}

	// Dynamically assign memory in heap depending on the log size.
	char* buffer = malloc(len + 1); // +1 for null terminator
	if (!buffer) {
		va_end(args_after_prefix);
		return 0;
	}

	va_list args_body;
	va_copy(args_body, args_after_prefix);
	va_end(args_after_prefix);
	len = vsnprintf(buffer, len + 1, body_fmt, args_body);
	va_end(args_body);

	if (len < 0) {
		free(buffer);
		return 0;
	}

	spotflow_log_cbor_send(body_fmt, buffer, &metadata);

	free(buffer);
	// Optionally, call original log output to keep default behavior
	if (original_vprintf) {
		return original_vprintf(fmt, args);
	}
	return len;
}

/**
 * @brief Set the value at low level such that device doesn't generate the logs
 *
 * @param level
 */
void spotflow_log_backend_try_set_runtime_filter(uint8_t level)
{
#if CONFIG_SPOTFLOW_LOG_BACKEND_SET_RUNTIME_FILTERING

	esp_log_level_set("*", level); // Set log level for the provided tag

#endif /* CONFIG_SPOTFLOW_LOG_BACKEND_SET_RUNTIME_FILTERING */
}
