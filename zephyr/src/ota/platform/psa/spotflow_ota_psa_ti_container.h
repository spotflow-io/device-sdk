#ifndef SPOTFLOW_OTA_PSA_TI_CONTAINER_H
#define SPOTFLOW_OTA_PSA_TI_CONTAINER_H

#include "ota/platform/spotflow_ota_identity_stream.h"

#include <psa_fwu.h>
#include <spotflow/downloader.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_TI_SECTOR_SIZE 4096U
/* TI reserves a slot header sector, less the manifest integrity and auth fields. */
#define SPOTFLOW_OTA_TI_SLOT_OVERHEAD (SPOTFLOW_OTA_TI_SECTOR_SIZE - 4U - 16U)

/** @brief TI signed-container parser; initialize with spotflow_ota_ti_container_init(). */
struct spotflow_ota_ti_container {
	PSA_FWU_GPESlot_t prefix;
	struct spotflow_ota_identity_stream identity;
	size_t offset;
	size_t max_size;
	size_t payload_end;
	size_t protected_end;
	size_t total_size;
	uint8_t protected_header[4];
	uint8_t tlv_header[4];
	bool complete;
};

/**
 * @brief Initialize or reset a TI signed-container parser.
 * @param container Parser context.
 * @param max_size Maximum complete signed-artifact size in bytes; must be nonzero.
 * @return Zero on success, -EINVAL for a NULL context or zero size limit.
 */
int spotflow_ota_ti_container_init(struct spotflow_ota_ti_container* container, size_t max_size);

/**
 * @brief Validate sequential container bytes and scan only executable descriptors.
 * @param container Parser context with the signed-artifact size limit.
 * @param block Artifact block, including prefix and signature trailer bytes.
 * @return Zero on success, negative errno for invalid framing, bounds or offsets.
 * @note This checks framing; TI's bootloader verifies the image signature.
 */
int spotflow_ota_ti_container_feed(struct spotflow_ota_ti_container* container,
				   const struct spotflow_artifact_block* block);

/**
 * @brief Copy the identity from a completed container.
 * @param container Parser context.
 * @param build_id Receives the executable build ID.
 * @return Zero on success, negative errno if incomplete or missing a valid identity.
 */
int spotflow_ota_ti_container_get_build_id(const struct spotflow_ota_ti_container* container,
					   uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PSA_TI_CONTAINER_H */
