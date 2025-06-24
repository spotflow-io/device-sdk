#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wifi.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

	init_wifi();
	connect_to_wifi();

	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
