#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include <spotflow/ota.h>

#include "net.h"
#include "ota/spotflow_ota_download.h"

LOG_MODULE_REGISTER(ota_sample, LOG_LEVEL_INF);

#define OTA_DOWNLOAD_MAX_ATTEMPTS 5
#define OTA_DOWNLOAD_RETRY_DELAY_SEC 5

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
	 * The sample currently uses delegated OTA artifacts to exercise the existing PoC
	 * download-to-flash path. Publish the test firmware as a non-main artifact and the
	 * sample will download it into the MCUboot upload slot.
	 */
	LOG_INF("Downloading delegated artifact '%s' version %s", info->slug, info->version);

	int rc;
	bool downloaded = false;

	for (int attempt = 1; attempt <= OTA_DOWNLOAD_MAX_ATTEMPTS; attempt++) {
		if (spotflow_is_update_canceled()) {
			LOG_INF("OTA update canceled before download attempt %d/%d",
				attempt, OTA_DOWNLOAD_MAX_ATTEMPTS);
			return SPOTFLOW_OTA_RESULT_CANCELED;
		}

		LOG_INF("Download attempt %d/%d for artifact '%s'", attempt,
			OTA_DOWNLOAD_MAX_ATTEMPTS, info->slug);

		/*
		* TODO: Replace by info->download_request->url and info->download_request->secret
		* when the TLS handshake works
		*/
		rc = spotflow_ota_download_and_flash(
		    "https://roberthusak.cz/tmp/fota/rw612/image_confirmed", NULL);
		if (rc == 0) {
			downloaded = true;
			break;
		}

		LOG_ERR("Failed to download artifact '%s' (attempt %d/%d): %d", info->slug,
			attempt, OTA_DOWNLOAD_MAX_ATTEMPTS, rc);
		if (attempt < OTA_DOWNLOAD_MAX_ATTEMPTS) {
			k_sleep(K_SECONDS(OTA_DOWNLOAD_RETRY_DELAY_SEC));
		}
	}
	if (!downloaded) {
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
