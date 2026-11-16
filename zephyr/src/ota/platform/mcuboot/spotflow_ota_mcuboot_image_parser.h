#ifndef SPOTFLOW_OTA_MCUBOOT_IMAGE_PARSER_H
#define SPOTFLOW_OTA_MCUBOOT_IMAGE_PARSER_H

#include "ota/platform/spotflow_ota_build_id_parser.h"

#include <spotflow/downloader.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Incremental MCUboot v1 image parser; checks framing, not signatures.
 *
 * The little-endian header defines the payload as ih_img_size bytes at artifact
 * offset ih_hdr_size. Only these bytes reach the build-ID parser.
 * Completion requires the full declared payload within max_size. Header padding
 * and trailing bytes count toward that limit, but trailer contents and presence
 * are not validated.
 */
struct spotflow_ota_mcuboot_image_parser {
	struct spotflow_ota_build_id_parser build_id_parser;
	uint8_t header[32];
	size_t artifact_offset;
	size_t max_size;
	size_t payload_start;
	size_t payload_end;
	bool artifact_complete;
};

/**
 * @brief Initialize or reset an image parser for a new artifact.
 * @param parser Parser context.
 * @param max_size Maximum complete artifact size in bytes; must be nonzero.
 * @return Zero on success, -EINVAL for a NULL context or zero size limit.
 */
int spotflow_ota_mcuboot_image_parser_init(struct spotflow_ota_mcuboot_image_parser* parser,
					   size_t max_size);

/**
 * @brief Inspect the next signed-image block; only payload bytes reach the build-ID parser.
 * @param parser Initialized parser context.
 * @param block Artifact bytes borrowed only for this call; offsets start at zero
 *              and must be contiguous.
 * @return Zero if framing is valid so far, negative errno on failure; abort/reset on error.
 * @note is_last ends the artifact and may accompany an empty block. No image I/O is done.
 */
int spotflow_ota_mcuboot_image_parser_feed(struct spotflow_ota_mcuboot_image_parser* parser,
					   const struct spotflow_artifact_block* block);

/**
 * @brief Copy the retained build ID after a valid final block; repeated calls are safe.
 * @param parser Framing and descriptor context.
 * @param build_id Receives the build ID.
 * @return Zero on success, -EBADMSG before artifact completion, or a build-ID parser error.
 */
int spotflow_ota_mcuboot_image_parser_get_build_id(
	const struct spotflow_ota_mcuboot_image_parser* parser,
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_MCUBOOT_IMAGE_PARSER_H */
