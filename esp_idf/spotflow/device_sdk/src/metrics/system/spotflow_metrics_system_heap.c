#include "metrics/system/spotflow_metrics_system_heap.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "logging/spotflow_log_backend.h"

#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <esp_heap_caps.h>

static struct spotflow_metric_int* g_heap_free_metric;
static struct spotflow_metric_int* g_heap_allocated_metric;

/**
 * @brief Initialize heap metrics
 *
 * @return int
 */
int spotflow_metrics_system_heap_init(void)
{
	int rc;

	rc =
	    spotflow_register_metric_int(SPOTFLOW_METRIC_NAME_HEAP_FREE,
					 SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, &g_heap_free_metric);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to register heap free metric");
		return rc;
	}

	rc = spotflow_register_metric_int(SPOTFLOW_METRIC_NAME_HEAP_ALLOCATED,
					  SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
					  &g_heap_allocated_metric);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to register heap allocated metric");
		return rc;
	}

	SPOTFLOW_LOG("Registered heap metrics");
	return 2;
}

/**
 * @brief Collect and report heap metrics
 *
 */
void spotflow_metrics_system_heap_collect(void)
{
	if (!g_heap_free_metric || !g_heap_allocated_metric) {
		SPOTFLOW_LOG("Heap metrics not registered");
		return;
	}

	size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
	size_t total_bytes = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
	size_t allocated_bytes = total_bytes - free_bytes;

	int rc = spotflow_report_metric_int(g_heap_free_metric, free_bytes);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to report heap free");
	}

	rc = spotflow_report_metric_int(g_heap_allocated_metric, allocated_bytes);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to report heap allocated");
	}

	SPOTFLOW_DEBUG("Heap: free=%zu bytes, allocated=%zu bytes", free_bytes, allocated_bytes);
}
