#ifndef SPOTFLOW_OTA_PSA_TI_IMAGE_PARSER_H
#define SPOTFLOW_OTA_PSA_TI_IMAGE_PARSER_H

#include "ota/platform/spotflow_ota_build_id_parser.h"

#include <psa_fwu.h>
#include <spotflow/downloader.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_TI_SECTOR_SIZE 4096U
/* TI reserves a slot header sector, less the manifest integrity and auth fields. */
#define SPOTFLOW_OTA_TI_SLOT_OVERHEAD (SPOTFLOW_OTA_TI_SECTOR_SIZE - 4U - 16U)

/**
 * @brief Incremental TI signed-image parser; checks framing, not signatures.
 *
 * Only the ih_img_size payload bytes after the 48-byte prefix reach the build-ID
 * parser. An optional protected TLV area follows, then a required unprotected TLV
 * area. Their headers are checked for magic and declared length; individual entries
 * are not inspected. Completion requires the exact declared total size, aligned
 * to four bytes and within max_size.
 */
struct spotflow_ota_ti_image_parser {
	PSA_FWU_GPESlot_t prefix;
	struct spotflow_ota_build_id_parser build_id_parser;
	size_t artifact_offset;
	size_t max_size;
	size_t payload_end;
	size_t protected_end;
	size_t total_size;
	uint8_t protected_header[4];
	uint8_t tlv_header[4];
	bool artifact_complete;
};

/**
 * @brief Initialize or reset a TI image parser for a new artifact.
 * @param parser Parser context.
 * @param max_size Maximum complete signed-artifact size in bytes; must be nonzero.
 * @return Zero on success, -EINVAL for a NULL context or zero size limit.
 */
int spotflow_ota_ti_image_parser_init(struct spotflow_ota_ti_image_parser* parser, size_t max_size);

/**
 * @brief Inspect the next signed-image block; only payload bytes reach the build-ID parser.
 * @param parser Initialized parser context.
 * @param block Artifact bytes borrowed only for this call; offsets start at zero
 *              and must be contiguous.
 * @return Zero if framing is valid so far, negative errno on failure; abort/reset on error.
 * @note is_last ends the artifact and may accompany an empty block. No image I/O is done.
 */
int spotflow_ota_ti_image_parser_feed(struct spotflow_ota_ti_image_parser* parser,
				      const struct spotflow_artifact_block* block);

/**
 * @brief Copy the retained build ID after a valid final block; repeated calls are safe.
 * @param parser Parser context.
 * @param build_id Receives the executable build ID.
 * @return Zero on success, -EBADMSG before artifact completion, or a build-ID parser error.
 */
int spotflow_ota_ti_image_parser_get_build_id(const struct spotflow_ota_ti_image_parser* parser,
					      uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

/**
 * @brief Borrow the parsed TI prefix for PSA start; valid until parser reset.
 * @param parser Initialized parser context with no preceding feed error.
 * @param prefix Receives the prefix pointer; unchanged on error.
 * @return Zero on success, -EAGAIN before prefix completion, -EINVAL for a NULL argument.
 */
int spotflow_ota_ti_image_parser_get_prefix(const struct spotflow_ota_ti_image_parser* parser,
					    const PSA_FWU_GPESlot_t** prefix);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PSA_TI_IMAGE_PARSER_H */
