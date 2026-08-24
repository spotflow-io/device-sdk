#include "showcase.h"

#include "net.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(showcase, LOG_LEVEL_INF);

#define TELEMETRY_INTERVAL K_SECONDS(5)

int main(void)
{
	LOG_INF("Starting the Spotflow API showcase");

	int rc = showcase_button_init();

	if (rc < 0) {
		return rc;
	}
	rc = showcase_ota_init();
	if (rc < 0) {
		LOG_ERR("Failed to initialize the OTA showcase: %d", rc);
		return rc;
	}
	rc = showcase_telemetry_init();
	if (rc < 0) {
		LOG_ERR("Failed to initialize telemetry: %d", rc);
		return rc;
	}

	/* Allow network devices to complete their own initialization first. */
	k_sleep(K_SECONDS(1));
	spotflow_sample_net_init();

	while (true) {
		enum showcase_button_action action;

		rc = showcase_button_get_action(&action, TELEMETRY_INTERVAL);
		if (rc == -EAGAIN) {
			showcase_telemetry_emit();
			continue;
		}
		if (rc < 0) {
			LOG_ERR("Failed to receive a button action: %d", rc);
			continue;
		}

		if (action == SHOWCASE_BUTTON_SHORT_PRESS) {
			showcase_ota_handle_short_press();
		} else if (!showcase_ota_handle_long_press()) {
			LOG_ERR("No OTA update is active; creating a coredump");
			k_sleep(K_MSEC(100));
			k_oops();
		}
	}

	return 0;
}
