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

static struct spotflow_metric_int *g_heap_free_metric;
static struct spotflow_metric_int *g_heap_allocated_metric;

extern struct sys_heap _system_heap;

int spotflow_metrics_system_heap_init(void)
{
	g_heap_free_metric = spotflow_register_metric_int("heap_free_bytes",
							    SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL);
	if (!g_heap_free_metric) {
		LOG_ERR("Failed to register heap free metric");
		return -ENOMEM;
	}

	g_heap_allocated_metric = spotflow_register_metric_int("heap_allocated_bytes",
							        SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL);
	if (!g_heap_allocated_metric) {
		LOG_ERR("Failed to register heap allocated metric");
		return -ENOMEM;
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

	int rc = spotflow_report_metric_int(g_heap_free_metric, heap_stats.free_bytes);
	if (rc < 0) {
		LOG_ERR("Failed to report heap free: %d", rc);
	}

	rc = spotflow_report_metric_int(g_heap_allocated_metric, heap_stats.allocated_bytes);
	if (rc < 0) {
		LOG_ERR("Failed to report heap allocated: %d", rc);
	}

	LOG_DBG("Heap: free=%zu bytes, allocated=%zu bytes", heap_stats.free_bytes,
		heap_stats.allocated_bytes);
}
