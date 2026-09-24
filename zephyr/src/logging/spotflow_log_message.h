#ifndef SPOTFLOW_LOG_MESSAGE_H
#define SPOTFLOW_LOG_MESSAGE_H

#include "spotflow_log_cbor.h"

#ifdef __cplusplus
extern "C" {
#endif

struct log_msg;

/** @brief Borrowed compact-package string section, private to the preparation iterator. */
struct spotflow_log_string_data {
	const uint8_t* data;
	size_t len;
};

/**
 * @brief Stack-local prepared message and its iterator data.
 * @note The CBOR view references this structure. Do not copy or move it after preparation.
 * The Zephyr message and formatting buffer must remain alive until encoding finishes.
 */
struct spotflow_log_message {
	struct spotflow_log_cbor_msg cbor;
	struct spotflow_log_string_data strings;
};

/**
 * @brief Prepare a Zephyr log for serialization.
 * @param log_msg Source Zephyr message.
 * @param sequence_number Sequence number, converted to the existing 32-bit wire value.
 * @param message Output view and iterator data; valid only on success.
 * @param formatted Buffer for ordinary log text; may be NULL in compact mode.
 * @param formatted_len Formatting buffer capacity, including the terminating NUL.
 * @return 0 on success, -EINVAL for invalid input/package, -ENOMEM for formatting overflow,
 * or a negative formatting error.
 */
int spotflow_log_message_prepare(struct log_msg* log_msg, size_t sequence_number,
				 struct spotflow_log_message* message, char* formatted,
				 size_t formatted_len);

#ifdef __cplusplus
}
#endif
#endif /* SPOTFLOW_LOG_MESSAGE_H */
