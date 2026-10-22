#ifndef SPOTFLOW_OTA_PLATFORM_H
#define SPOTFLOW_OTA_PLATFORM_H

#include "spotflow_build_id.h"

#include <stdbool.h>
#include <spotflow/downloader.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bootloader state of the executable currently running. */
enum spotflow_ota_running_image_state {
	SPOTFLOW_OTA_RUNNING_IMAGE_UNCONFIRMED,
	SPOTFLOW_OTA_RUNNING_IMAGE_CONFIRMED,
	SPOTFLOW_OTA_RUNNING_IMAGE_REJECTED,
};

/** @brief Initialize image management after boot; return zero or negative errno. */
int spotflow_ota_platform_init(void);

/**
 * @brief Read the running image's bootloader state.
 * @param state Receives the normalized state.
 * @return Zero on success, negative errno on query failure or an unsupported state.
 */
int spotflow_ota_platform_get_running_image_state(enum spotflow_ota_running_image_state* state);

/** @brief Request installation with rollback enabled; return zero or negative errno. */
int spotflow_ota_platform_request_upgrade(void);

/**
 * @brief Confirm the running image without rebooting.
 * @param reboot_required True if confirmation must be made durable by an application reboot.
 * @return Zero on success, negative errno on failure. A successful pending request is idempotent.
 */
int spotflow_ota_platform_confirm_image(bool* reboot_required);

/** @brief Request a platform reboot; return zero or negative errno if the call returns. */
int spotflow_ota_platform_reboot(void);

/** @brief Select and prepare inactive storage; return zero or negative errno. */
int spotflow_ota_platform_begin_image_write(void);

/**
 * @brief Write the next sequential artifact block, including native container bytes.
 * @param block Downloaded block; its final flag terminates the artifact stream.
 * @return Zero on success, negative errno on invalid data or write failure.
 */
int spotflow_ota_platform_write_image_block(const struct spotflow_artifact_block* block);

/**
 * @brief Read the completed candidate's identity without exposing native storage.
 * @param build_id Receives the executable build ID.
 * @return Zero on success, negative errno if the identity is missing or invalid.
 */
int spotflow_ota_platform_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);

/** @brief Finalize the candidate before installation; return zero or negative errno. */
int spotflow_ota_platform_finish_image_write(void);

/** @brief Release an incomplete or unused candidate; return zero or negative errno. */
int spotflow_ota_platform_abort_image_write(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PLATFORM_H */
