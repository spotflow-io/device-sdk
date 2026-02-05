/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system_heap.h"
#include "spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/heap_listener.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static struct spotflow_metric_int* g_heap_free_metric;
static struct spotflow_metric_int* g_heap_allocated_metric;

extern struct sys_heap _system_heap;

int spotflow_metrics_system_heap_init(void)
{
	int rc;

	rc =
	    spotflow_register_metric_int(SPOTFLOW_METRIC_NAME_HEAP_FREE,
					 SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, &g_heap_free_metric);
	if (rc < 0) {
		LOG_ERR("Failed to register heap free metric: %d", rc);
		return rc;
	}

	rc = spotflow_register_metric_int(SPOTFLOW_METRIC_NAME_HEAP_ALLOCATED,
					  SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
					  &g_heap_allocated_metric);
	if (rc < 0) {
		LOG_ERR("Failed to register heap allocated metric: %d", rc);
		return rc;
	}

	LOG_INF("Registered heap metrics");
	return 2;
}

void spotflow_metrics_system_heap_collect(void)
{
	if (!g_heap_free_metric || !g_heap_allocated_metric) {
		LOG_ERR("Heap metrics not registered");
		return;
	}

	struct sys_memory_stats heap_stats;
	int ret = sys_heap_runtime_stats_get(&_system_heap, &heap_stats);
	if (ret < 0) {
		LOG_ERR("Failed to get heap stats: %d", ret);
		return;
	}

	// TODO: Replace with spotflow_report_metric_uint64 when available
	int64_t free_bytes_capped =
	    (heap_stats.free_bytes > INT64_MAX) ? INT64_MAX : (int64_t)heap_stats.free_bytes;
	int64_t allocated_bytes_capped = (heap_stats.allocated_bytes > INT64_MAX)
	    ? INT64_MAX
	    : (int64_t)heap_stats.allocated_bytes;

	int rc = spotflow_report_metric_int(g_heap_free_metric, free_bytes_capped);
	if (rc < 0) {
		LOG_ERR("Failed to report heap free: %d", rc);
	}

	rc = spotflow_report_metric_int(g_heap_allocated_metric, allocated_bytes_capped);
	if (rc < 0) {
		LOG_ERR("Failed to report heap allocated: %d", rc);
	}

	LOG_DBG("Heap: free=%zu bytes, allocated=%zu bytes", heap_stats.free_bytes,
		heap_stats.allocated_bytes);
}
