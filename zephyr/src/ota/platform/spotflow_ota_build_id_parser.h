#ifndef SPOTFLOW_OTA_BUILD_ID_PARSER_H
#define SPOTFLOW_OTA_BUILD_ID_PARSER_H

#include "spotflow_build_id.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Incrementally extract the build ID from native-endian Zephyr descriptors.
 *
 * Feed executable payload bytes from offset zero, excluding container headers/trailers.
 * State spans calls; input buffers are borrowed only for each call. No image I/O is done.
 *
 * The first magic at a BINDESC_ALIGNMENT boundary relative to payload offset zero
 * selects the descriptor list. Other entries are skipped with their alignment padding.
 * There is no fixed search window. Scanning stops at the build ID, an end marker or
 * a descriptor error; it does not restart at a later magic value.
 */
struct spotflow_ota_build_id_parser {
	uint64_t magic;
	size_t payload_scan_offset;
	size_t remaining;
	uint8_t header[4];
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t used;
	uint8_t phase;
	int error;
};

/** @brief Initialize or reset a build-ID parser; NULL is ignored. */
void spotflow_ota_build_id_parser_init(struct spotflow_ota_build_id_parser* parser);

/**
 * @brief Consume executable payload bytes in order.
 * @param parser Initialized parser context; NULL is ignored.
 * @param data Payload bytes, or NULL when len is zero.
 * @param len Number of bytes to consume.
 * @note Input must be contiguous. Scanning stops at the ID or a descriptor error.
 * Errors are retained for get_build_id(); valid later input is ignored after stopping.
 */
void spotflow_ota_build_id_parser_feed(struct spotflow_ota_build_id_parser* parser,
				       const uint8_t* data, size_t len);

/**
 * @brief Copy the retained build ID without consuming it; may succeed before payload EOF.
 * @param parser Parser context.
 * @param build_id Receives the build ID.
 * @return Zero on success, -ENOENT if incomplete or missing, -EINVAL for an invalid
 * argument or descriptor size, or -ENOSYS for an unpatched all-zero ID.
 */
int spotflow_ota_build_id_parser_get_build_id(const struct spotflow_ota_build_id_parser* parser,
					      uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_BUILD_ID_PARSER_H */
