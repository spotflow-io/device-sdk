#ifndef SPOTFLOW_LOG_CBOR_H
#define SPOTFLOW_LOG_CBOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Borrowed embedded string and its byte offset within the argument data. */
struct spotflow_log_embedded_string {
	uint32_t argument_offset;
	const char* value;
	size_t len; /* Excludes the terminating NUL. */
};

/**
 * @brief Iterate borrowed embedded strings without allocating an array.
 * @param context Borrowed immutable iterator data.
 * @param cursor Caller-owned cursor, initialized to zero for each traversal.
 * @param string Output entry, valid until the source message is released.
 * @return 1 for an entry, 0 at the end, negative errno on invalid input.
 */
typedef int (*spotflow_log_string_next_fn)(const void* context, size_t* cursor,
					   struct spotflow_log_embedded_string* string);

/** @brief Ordinary log body; a NULL template omits bodyTemplate. */
struct spotflow_log_text_body {
	const char* text;
	const char* body_template;
};

/** @brief Compact body with opaque argument bytes and a repeatable string iterator. */
struct spotflow_log_compact_body {
	uint64_t template_address;
	const uint8_t* argument_data;
	size_t argument_len;
	size_t string_count;
	spotflow_log_string_next_fn next_string;
	const void* string_context;
};

/** @brief Representation of the log body. */
enum spotflow_log_body_type {
	SPOTFLOW_LOG_BODY_TEXT,
	SPOTFLOW_LOG_BODY_COMPACT,
};

/** @brief Prepared protocol fields; all pointers borrow caller-owned storage. */
struct spotflow_log_cbor_msg {
	uint32_t severity;
	uint32_t uptime_ms;
	uint32_t sequence_number;
	const char* source;
	enum spotflow_log_body_type body_type;
	union {
		struct spotflow_log_text_body text;
		struct spotflow_log_compact_body compact;
	} body;
};

/**
 * @brief Encode prepared log fields into a caller-owned buffer without allocation.
 * @param msg Prepared message; referenced data must remain valid throughout encoding.
 * @param buffer Destination buffer.
 * @param len Destination capacity in bytes.
 * @param encoded_len Encoded length, written only on success.
 * @return 0 on success, -EINVAL for invalid fields or encoding failure, or an iterator error.
 * @note Each call starts a fresh iterator traversal. No input pointers are retained.
 */
int spotflow_log_cbor_encode(const struct spotflow_log_cbor_msg* msg, uint8_t* buffer, size_t len,
			     size_t* encoded_len);

#ifdef __cplusplus
}
#endif
#endif /* SPOTFLOW_LOG_CBOR_H */
