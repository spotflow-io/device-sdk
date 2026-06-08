#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <spotflow/downloader.h>

#include "ota/spotflow_ota_fw_custom.h"
#include "ota/spotflow_ota_fw_main.h"
#include "ota/spotflow_ota_identity.h"
#include "ota/spotflow_ota_persistence.h"
#include "ota/spotflow_ota_platform.h"
#include "ota/spotflow_ota_state.h"

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static SPOTFLOW_DEFINE_DOWNLOADER(main_firmware_downloader);

struct main_firmware_flash_ctx {
	int write_err;
};

static void notify_main_firmware_phase(enum spotflow_ota_phase phase);
static enum spotflow_ota_result fail_main_firmware(void);
static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx);

void spotflow_ota_fw_main_reset(void)
{
	k_mutex_lock(&main_firmware_downloader.mutex, K_FOREVER);
	main_firmware_downloader.state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE;
	main_firmware_downloader.cancel_requested = false;
	k_mutex_unlock(&main_firmware_downloader.mutex);
}

enum spotflow_ota_result spotflow_ota_fw_main_process_artifact(uint64_t attempt_id,
							       size_t artifact_index,
							       const struct spotflow_ota_artifact* artifact)
{
	if (artifact == NULL || artifact->url[0] == '\0' || artifact->secret[0] == '\0') {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	if (spotflow_ota_state_store_main_firmware_artifact(attempt_id, artifact_index,
							  artifact) < 0) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	if (spotflow_is_update_canceled()) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD);

	struct spotflow_download_request request = {
		.url = artifact->url,
		.secret = artifact->secret,
	};
	struct main_firmware_flash_ctx flash_ctx = {
		.write_err = 0,
	};
	int rc = spotflow_ota_platform_begin_image_write();

	if (rc < 0) {
		LOG_ERR("Failed to initialize main firmware image writer: %d", rc);
		return fail_main_firmware();
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_DOWNLOADING);

	rc = spotflow_download_artifact(&main_firmware_downloader, &request, download_block_cb,
					&flash_ctx);
	if (rc == -ECANCELED) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}
	if (rc < 0) {
		LOG_ERR("Main firmware download failed: %d", rc);
		return fail_main_firmware();
	}
	if (flash_ctx.write_err != 0) {
		LOG_ERR("Main firmware flash write failed: %d", flash_ctx.write_err);
		return fail_main_firmware();
	}

	if (spotflow_is_update_canceled()) {
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_UPGRADE);

	uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH];

	rc = spotflow_ota_identity_get_downloaded_build_id(expected_build_id);
	if (rc < 0) {
		LOG_ERR("Failed to read downloaded main firmware build ID: %d", rc);
		return fail_main_firmware();
	}

	struct spotflow_ota_probation probation = {
		.attempt_id = attempt_id,
		.artifact_index = (uint32_t)artifact_index,
	};

	strncpy(probation.slug, artifact->slug, sizeof(probation.slug) - 1);
	strncpy(probation.version, artifact->version, sizeof(probation.version) - 1);
	memcpy(probation.expected_build_id, expected_build_id, sizeof(probation.expected_build_id));

	rc = spotflow_ota_persistence_save_probation(&probation);
	if (rc < 0) {
		LOG_ERR("Failed to persist main firmware probation record: %d", rc);
		return fail_main_firmware();
	}

	rc = spotflow_ota_platform_request_test_upgrade();
	if (rc < 0) {
		LOG_ERR("Failed to request main firmware test upgrade: %d", rc);
		return fail_main_firmware();
	}

	notify_main_firmware_phase(SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
	spotflow_ota_platform_reboot();

	return SPOTFLOW_OTA_RESULT_PENDING;
}

static void notify_main_firmware_phase(enum spotflow_ota_phase phase)
{
	struct spotflow_ota_main_firmware_state state;

	if (spotflow_ota_state_set_main_firmware_phase(phase, &state) < 0) {
		return;
	}

	spotflow_on_main_firmware_update_progressed(&state);
}

static enum spotflow_ota_result fail_main_firmware(void)
{
	struct spotflow_ota_main_firmware_state state;

	if (spotflow_ota_state_set_main_firmware_result(SPOTFLOW_OTA_RESULT_FAILED, &state) == 0) {
		spotflow_on_main_firmware_update_progressed(&state);
	}

	return SPOTFLOW_OTA_RESULT_FAILED;
}

static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx)
{
	ARG_UNUSED(downloader);

	struct main_firmware_flash_ctx* ctx = callback_ctx;

	if (ctx->write_err != 0) {
		return;
	}

	if (block->data_len == 0 && !block->is_last) {
		return;
	}

	ctx->write_err =
	    spotflow_ota_platform_write_image_block(block->data, block->data_len, block->is_last);
}
