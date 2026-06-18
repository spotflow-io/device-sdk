#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(spotflow_ble_sample, LOG_LEVEL_INF);

static const char* const long_warning_message =
    "framing-test payload: abcdefghijklmnopqrstuvwxyz 0123456789 "
    "abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
    "abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
    "abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789";

int main(void)
{
	uint32_t log_counter = 0;

	LOG_INF("Starting Spotflow BLE sample");

	while (true) {
		log_counter++;

		if ((log_counter % 10U) == 0U) {
			LOG_WRN("Spotflow BLE framing test %u: %s", log_counter,
				long_warning_message);
		} else {
			LOG_INF("Hello from Zephyr to Spotflow BLE: %u", log_counter);
		}
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
