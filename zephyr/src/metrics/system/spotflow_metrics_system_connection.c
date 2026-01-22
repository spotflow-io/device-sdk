/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system_connection.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static struct spotflow_metric_int *g_connection_state_metric;

int spotflow_metrics_system_connection_init(void)
{
	g_connection_state_metric =
		spotflow_register_metric_int("connection_mqtt_connected", SPOTFLOW_AGG_INTERVAL_NONE);
	if (!g_connection_state_metric) {
		LOG_ERR("Failed to register connection state metric");
		return -ENOMEM;
	}

	LOG_INF("Registered connection state metric");
	return 1;
}

void spotflow_metrics_system_connection_report(bool connected)
{
	if (!g_connection_state_metric) {
		return;
	}

	int rc = spotflow_report_metric_int(g_connection_state_metric, connected ? 1 : 0);
	if (rc < 0) {
		LOG_ERR("Failed to report connection state: %d", rc);
		return;
	}

	LOG_INF("MQTT connection state: %s", connected ? "connected" : "disconnected");
}
