#include <zephyr/bindesc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "net.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

/* Uncomment this function to provide your own device ID in runtime */
/*const char* spotflow_override_device_id()
{
	return "my_nrf7002dk_test";
}*/

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of network device
	k_sleep(K_SECONDS(1));

	spotflow_sample_net_init();

	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
