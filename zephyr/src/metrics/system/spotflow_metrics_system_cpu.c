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
#include <zephyr/debug/cpu_load.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static struct spotflow_metric_float* g_cpu_utilization_metric;

int spotflow_metrics_system_cpu_init(void)
{
	int rc = spotflow_register_metric_float(SPOTFLOW_METRIC_NAME_CPU,
						SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
						&g_cpu_utilization_metric);
	if (rc < 0) {
		LOG_ERR("Failed to register CPU utilization metric: %d", rc);
		return rc;
	}

	LOG_INF("Registered CPU utilization metric");
	return 1;
}

void spotflow_metrics_system_cpu_collect(void)
{
	if (!g_cpu_utilization_metric) {
		LOG_ERR("CPU metric not registered");
		return;
	}

	int load = cpu_load_get(true);
	if (load < 0) {
		LOG_WRN("Failed to get CPU load: %d", load);
		return;
	}

	/* Convert per mille to percentage */
	float utilization = (float)load / 10.0f;

	int rc = spotflow_report_metric_float(g_cpu_utilization_metric, utilization);
	if (rc < 0) {
		LOG_ERR("Failed to report CPU utilization: %d", rc);
	}

	/* Log using integer format (load is in per mille) */
	LOG_DBG("CPU utilization: %d.%d%%", load / 10, load % 10);
}
