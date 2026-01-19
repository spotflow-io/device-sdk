/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system_cpu.h"
#include "spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static spotflow_metric_float_t *g_cpu_utilization_metric;
static uint64_t g_last_idle_cycles;
static uint64_t g_last_total_cycles;

int spotflow_metrics_system_cpu_init(void)
{
	g_cpu_utilization_metric = spotflow_register_metric_float(
		"cpu_utilization_percent", SPOTFLOW_METRICS_SYSTEM_INTERVAL_STR);
	if (!g_cpu_utilization_metric) {
		LOG_ERR("Failed to register CPU utilization metric");
		return -ENOMEM;
	}

	g_last_idle_cycles = 0;
	g_last_total_cycles = 0;

	LOG_INF("Registered CPU utilization metric");
	return 1;
}

void spotflow_metrics_system_cpu_collect(void)
{
	if (!g_cpu_utilization_metric) {
		LOG_ERR("CPU metric not registered");
		return;
	}

	k_thread_runtime_stats_t stats;
	int rc = k_thread_runtime_stats_all_get(&stats);
	if (rc < 0) {
		LOG_WRN("Failed to get all thread stats: %d", rc);
		return;
	}

	uint64_t total_cycles = stats.execution_cycles + stats.idle_cycles;

	if (g_last_total_cycles > 0 && total_cycles > g_last_total_cycles) {
		uint64_t delta_total = total_cycles - g_last_total_cycles;
		uint64_t delta_active = stats.execution_cycles - g_last_idle_cycles;

		if (delta_total > 0) {
			double utilization = 100.0 * ((double)delta_active / (double)delta_total);

			if (utilization < 0.0) {
				utilization = 0.0;
			} else if (utilization > 100.0) {
				utilization = 100.0;
			}

			rc = spotflow_report_metric_float(g_cpu_utilization_metric, utilization);
			if (rc < 0) {
				LOG_ERR("Failed to report CPU utilization: %d", rc);
			}

			LOG_DBG("CPU utilization: %.2f%%", utilization);
		}
	}

	g_last_idle_cycles = stats.execution_cycles;
	g_last_total_cycles = total_cycles;
}
