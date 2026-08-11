#ifndef SPOTFLOW_SESSION_METADATA_PUBLIC_H
#define SPOTFLOW_SESSION_METADATA_PUBLIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Supported value types for a session metadata label. */
enum spotflow_session_label_type {
	SPOTFLOW_SESSION_LABEL_STRING,
	SPOTFLOW_SESSION_LABEL_INT,
	SPOTFLOW_SESSION_LABEL_FLOAT,
	SPOTFLOW_SESSION_LABEL_BOOL,
};

/** A typed label included in Spotflow session metadata. */
struct spotflow_session_label {
	const char* key;
	enum spotflow_session_label_type type;
	union {
		const char* string;
		int64_t integer;
		double floating;
		bool boolean;
	} value;
};

/** A list of session metadata labels. */
struct spotflow_session_metadata_labels {
	const struct spotflow_session_label* items;
	size_t count;
};

/**
 * @brief Return labels included in each Spotflow Session Metadata message.
 *
 * Override this callback in application code to provide session labels. Return
 * an empty structure when no labels should be emitted. The callback runs while
 * the SDK encodes session metadata and must not block or call Spotflow APIs.
 * The returned array, label keys, and string values must remain valid until
 * the SDK has finished encoding the Session Metadata message. Keep the returned
 * labels stable between invocations unless a new session should intentionally
 * use different labels.
 *
 * When count is nonzero, items must be non-NULL. Every key must be a non-NULL,
 * nonempty, NUL-terminated string. String label values must likewise be
 * non-NULL and NUL-terminated, and every label type must be one of the values
 * defined by enum spotflow_session_label_type. If the labels are invalid or do
 * not fit CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE, the SDK logs a warning
 * and sends Session Metadata without application-defined labels.
 *
 * @return Labels to include in the current Session Metadata message.
 */
struct spotflow_session_metadata_labels spotflow_override_session_metadata_labels(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_SESSION_METADATA_PUBLIC_H */
