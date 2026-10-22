#ifndef SPOTFLOW_OTA_IDENTITY_STREAM_H
#define SPOTFLOW_OTA_IDENTITY_STREAM_H

#include "spotflow_build_id.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Incremental reader of native-endian Zephyr binary descriptors.
 *
 * Stream only the application payload, excluding container headers/trailers.
 * Zero-initialize before use. Descriptor magic and entries may cross blocks.
 */
struct spotflow_ota_identity_stream {
	uint64_t magic;
	size_t offset;
	size_t remaining;
	uint8_t header[4];
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t used;
	uint8_t phase;
	int error;
};

/**
 * @brief Consume executable payload bytes in order.
 * @param ctx Zero-initialized parser context; NULL is ignored.
 * @param data Payload bytes, or NULL when len is zero.
 * @param len Number of bytes to consume.
 * @note Errors are retained in the context and returned by the identity getter.
 */
void spotflow_ota_identity_stream_feed(struct spotflow_ota_identity_stream* ctx,
				       const uint8_t* data, size_t len);

/**
 * @brief Copy the parsed executable build ID.
 * @param ctx Parser context.
 * @param build_id Receives the build ID.
 * @return Zero on success, -ENOENT if incomplete or missing, -EINVAL for an invalid
 * argument or descriptor size, or -ENOSYS for an unpatched all-zero ID.
 */
int spotflow_ota_identity_stream_get(const struct spotflow_ota_identity_stream* ctx,
				     uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_IDENTITY_STREAM_H */
