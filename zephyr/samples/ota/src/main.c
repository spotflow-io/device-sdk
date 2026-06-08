#include <errno.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include <spotflow/ota.h>

#include "net.h"

LOG_MODULE_REGISTER(ota_sample, LOG_LEVEL_INF);

struct ota_sample_flash_ctx {
	struct flash_img_context fctx;
	int write_err;
};

static SPOTFLOW_DEFINE_DOWNLOADER(ota_downloader);

static void download_block_cb(const struct spotflow_artifact_block* block,
			      struct spotflow_downloader* downloader, void* callback_ctx)
{
	ARG_UNUSED(downloader);

	struct ota_sample_flash_ctx* ctx = callback_ctx;

	if (ctx->write_err != 0) {
		return;
	}

	if (block->data_len > 0) {
		int rc = flash_img_buffered_write(&ctx->fctx, block->data, block->data_len,
						  block->is_last);

		if (rc < 0) {
			LOG_ERR("flash_img_buffered_write failed: %d", rc);
			ctx->write_err = rc;
		}
	} else if (block->is_last) {
		int rc = flash_img_buffered_write(&ctx->fctx, NULL, 0, true);

		if (rc < 0) {
			LOG_ERR("flash_img_buffered_write flush failed: %d", rc);
			ctx->write_err = rc;
		}
	}
}

enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info)
{
	if (!boot_is_img_confirmed()) {
		/* Assume that the image was downloaded before the reboot */
		LOG_INF("Confirming current image...");
		int ret = boot_write_img_confirmed();
		if (ret) {
			LOG_ERR("ERROR: Failed to confirm image: %d", ret);
			return SPOTFLOW_OTA_RESULT_FAILED;
		} else {
			LOG_INF("Image confirmed successfully");
			return SPOTFLOW_OTA_RESULT_SUCCEEDED;
		}
	}

	if (info == NULL || info->download_request == NULL || info->download_request->url == NULL ||
	    info->download_request->secret == NULL) {
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	/*
	 * The sample currently uses delegated OTA artifacts to exercise the downloader
	 * streaming path. Publish the test firmware and the sample will download it into
	 * the MCUboot upload slot.
	 */
	LOG_INF("Downloading delegated artifact '%s' version %s", info->slug, info->version);

	if (spotflow_is_update_canceled()) {
		LOG_INF("OTA update canceled before download");
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	struct ota_sample_flash_ctx flash_ctx = { .write_err = 0 };
	int rc = flash_img_init(&flash_ctx.fctx);

	if (rc < 0) {
		LOG_ERR("flash_img_init failed: %d", rc);
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	rc = spotflow_download_artifact(&ota_downloader, info->download_request, download_block_cb,
					&flash_ctx);
	if (rc == -ECANCELED) {
		LOG_INF("OTA update canceled during download");
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}
	if (rc < 0) {
		LOG_ERR("Failed to download artifact '%s': %d", info->slug, rc);
		return SPOTFLOW_OTA_RESULT_FAILED;
	}
	if (flash_ctx.write_err != 0) {
		LOG_ERR("Flash write error during download: %d", flash_ctx.write_err);
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	LOG_INF("Artifact '%s' downloaded to the MCUboot upload slot", info->slug);

	if (spotflow_is_update_canceled()) {
		LOG_INF("OTA update canceled before requesting image upgrade");
		return SPOTFLOW_OTA_RESULT_CANCELED;
	}

	LOG_INF("Requesting upgrade of the downloaded image");
	rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
	if (rc) {
		LOG_ERR("ERROR: Failed to request upgrade: %d", rc);
		return SPOTFLOW_OTA_RESULT_FAILED;
	}

	LOG_INF("Upgrade requested successfully, rebooting in 3 seconds");

	k_sleep(K_SECONDS(3));
	sys_reboot(SYS_REBOOT_COLD);

	/* Unreachable */
	return SPOTFLOW_OTA_RESULT_FAILED;
}

void spotflow_on_update_canceled(void)
{
	LOG_INF("Received OTA update cancellation");
}

int main(void)
{
	/* TODO: Add a Kconfig option and instructions to README */
	/* Uncomment for versions to be downloaded */
	// LOG_INF("I was downloaded from the web!");

	LOG_INF("Starting Spotflow OTA example");

	if (boot_is_img_confirmed()) {
		LOG_INF("Current image is confirmed");
	} else {
		LOG_INF("Current image is NOT confirmed (test mode)");
	}

	/* Wait for the initialization of network device */
	k_sleep(K_SECONDS(1));

	spotflow_sample_net_init();

	LOG_INF("Ready to receive OTA updates");

	return 0;
}
