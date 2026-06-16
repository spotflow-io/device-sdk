#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(spotflow_ble_sample, LOG_LEVEL_INF);

int main(void)
{
	uint32_t log_counter = 0;

	LOG_INF("Starting Spotflow BLE sample");

	while (true) {
		LOG_INF("Hello from Zephyr to Spotflow BLE: %u", log_counter);
		log_counter++;
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
