#include "metrics/system/spotflow_metrics_system_connection.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "logging/spotflow_log_backend.h"

#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

static struct spotflow_metric_float* g_cpu_utilization_metric;

/**
 * @brief Get the idle runtime object
 *
 * @return uint32_t
 */
static uint32_t get_idle_runtime()
{
	return ulTaskGetIdleRunTimeCounter();
}

/**
 * @brief Get CPU utilization
 *
 * @return float
 */
static float get_cpu_utilization()
{
	uint32_t idle0_start = get_idle_runtime();
	uint32_t total_start = esp_timer_get_time(); // microseconds

	vTaskDelay(pdMS_TO_TICKS(1000)); // sample window = 1 sec
	uint32_t idle0_end = get_idle_runtime();
	uint32_t total_end = esp_timer_get_time();

	uint32_t idle0_delta = idle0_end - idle0_start;

	uint32_t total_time = total_end - total_start;

	// convert to percentage (approximation)
	float idle0_pct = (idle0_delta * 100.0f) / total_time;

	float cpu0_load = 100.0f - idle0_pct;
	return cpu0_load;
}

/**
 * @brief Initialize CPU metrics
 *
 * @return int
 */
int spotflow_metrics_system_cpu_init(void)
{
	int rc = spotflow_register_metric_float(SPOTFLOW_METRIC_NAME_CPU,
						SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
						&g_cpu_utilization_metric);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to register CPU utilization metric: %d", rc);
		return rc;
	}

	SPOTFLOW_LOG("Registered CPU utilization metric");
	return 1;
}

/**
 * @brief Collect and report CPU metrics
 *
 */
void spotflow_metrics_system_cpu_collect(void)
{
	if (!g_cpu_utilization_metric) {
		SPOTFLOW_LOG("CPU metric not registered");
		return;
	}

	float utilization_0 = get_cpu_utilization();
	int rc = spotflow_report_metric_float(g_cpu_utilization_metric, utilization_0);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to report CPU utilization: %d", rc);
	}

	SPOTFLOW_DEBUG("CPU utilization: %.1f%%", utilization_0);
}
