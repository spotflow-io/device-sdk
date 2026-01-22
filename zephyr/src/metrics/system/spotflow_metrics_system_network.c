/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system_network.h"
#include "spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_stats.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static spotflow_metric_int_t *g_network_tx_metric;
static spotflow_metric_int_t *g_network_rx_metric;

static void report_network_interface_metrics(struct net_if *iface, void *user_data);

int spotflow_metrics_system_network_init(void)
{
	g_network_tx_metric = spotflow_register_metric_int_with_labels(
		"network_tx_bytes", SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, 4, 1);
	if (!g_network_tx_metric) {
		LOG_ERR("Failed to register network TX metric");
		return -ENOMEM;
	}

	g_network_rx_metric = spotflow_register_metric_int_with_labels(
		"network_rx_bytes", SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL, 4, 1);
	if (!g_network_rx_metric) {
		LOG_ERR("Failed to register network RX metric");
		return -ENOMEM;
	}

	LOG_INF("Registered network metrics");
	return 2;
}

void spotflow_metrics_system_network_collect(void)
{
	if (!g_network_tx_metric || !g_network_rx_metric) {
		LOG_ERR("Network metrics not registered");
		return;
	}

	int if_count = 0;
	net_if_foreach(report_network_interface_metrics, &if_count);

	if (if_count == 0) {
		LOG_DBG("No active network interfaces found");
	}
}

static void report_network_interface_metrics(struct net_if *iface, void *user_data)
{
	int *if_count = (int *)user_data;

	if (!net_if_is_up(iface)) {
		return;
	}

	const struct device *dev = net_if_get_device(iface);
	if (!dev || !dev->name) {
		LOG_WRN("Interface has no valid device/name");
		return;
	}

	const char *if_name = dev->name;

	struct net_stats *stats = &iface->stats;
	uint64_t tx_bytes = stats->bytes.sent;
	uint64_t rx_bytes = stats->bytes.received;

	spotflow_label_t labels[] = {{.key = "interface", .value = if_name}};

	int rc = spotflow_report_metric_int_with_labels(g_network_tx_metric, (int64_t)tx_bytes,
							labels, 1);
	if (rc < 0) {
		LOG_ERR("Failed to report network TX for %s: %d", if_name, rc);
	}

	rc = spotflow_report_metric_int_with_labels(g_network_rx_metric, (int64_t)rx_bytes, labels,
						    1);
	if (rc < 0) {
		LOG_ERR("Failed to report network RX for %s: %d", if_name, rc);
	}

	LOG_DBG("Network %s: TX=%llu bytes, RX=%llu bytes", if_name, (unsigned long long)tx_bytes,
		(unsigned long long)rx_bytes);

	(*if_count)++;
}
