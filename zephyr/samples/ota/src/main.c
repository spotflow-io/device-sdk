#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>

#include "net.h"

LOG_MODULE_REGISTER(ota_sample, LOG_LEVEL_INF);

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
		LOG_INF("Confirming current image...");
		int ret = boot_write_img_confirmed();
		if (ret) {
			LOG_ERR("ERROR: Failed to confirm image: %d", ret);
		} else {
			LOG_INF("Image confirmed successfully");
		}
	}

	/* Wait for the initialization of network device */
	k_sleep(K_SECONDS(1));

	spotflow_sample_net_init();

	LOG_INF("Ready to receive OTA updates");

	return 0;
}
