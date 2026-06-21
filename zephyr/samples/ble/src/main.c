#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_registry.h"

LOG_MODULE_REGISTER(spotflow_ble_sample, LOG_LEVEL_INF);

static const char* const long_warning_message =
	"framing-test payload: abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789";

static struct spotflow_metric_int* sample_counter_metric;

int main(void)
{
	uint32_t log_counter = 0;
	int rc = spotflow_register_metric_int("sample_counter", SPOTFLOW_AGG_INTERVAL_NONE,
					      &sample_counter_metric);

	LOG_INF("Starting Spotflow BLE sample");
	if (rc < 0) {
		LOG_ERR("Failed to register sample metric: %d", rc);
	} else {
		LOG_INF("Registered sample metric");
	}

	while (true) {
		log_counter++;
		if (sample_counter_metric != NULL) {
			rc = spotflow_report_metric_int(sample_counter_metric, log_counter);
			if (rc < 0) {
				LOG_WRN("Failed to report sample metric: %d", rc);
			}
		}

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
