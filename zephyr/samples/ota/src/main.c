#include <zephyr/logging/log.h>

#include "net.h"

LOG_MODULE_REGISTER(ota_sample, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Starting Spotflow OTA example");

	/* Wait for the initialization of network device */
	k_sleep(K_SECONDS(1));

	spotflow_sample_net_init();

	LOG_INF("Ready to receive OTA updates");

	return 0;
}
