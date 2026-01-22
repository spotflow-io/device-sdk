/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Spotflow Metrics Sample Application
 *
 * This example demonstrates:
 * - System metrics auto-collection (memory, heap, network, CPU)
 * - Custom application metrics (label-less and labeled)
 * - Integration with logs
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "metrics/spotflow_metrics_backend.h"

#ifdef CONFIG_SPOTFLOW_USE_ETH
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#endif

#ifdef CONFIG_SPOTFLOW_USE_WIFI
#include "../../wifi-common/wifi.h"
#endif

LOG_MODULE_REGISTER(metrics_sample, LOG_LEVEL_INF);

/* Uncomment this function to provide your own device ID in runtime */
/*const char* spotflow_override_device_id()
{
	return "my_custom_device_id";
}*/

/* Metric handles - using type-specific handles */
static spotflow_metric_int_t *g_app_counter_metric;
static spotflow_metric_float_t *g_temperature_metric;
static spotflow_metric_float_t *g_request_duration_metric;

#ifdef CONFIG_SPOTFLOW_USE_ETH
static void turn_on_dhcp_when_device_is_up();
#endif

static int init_application_metrics(void);
static void report_counter_metric(void);
static void report_counter_metric(void);
static void report_temperature_metric(void);
static void report_request_duration_metric(void);
static void temperature_thread_entry();

#define TEMPERATURE_THREAD_STACK_SIZE 2048
#define TEMPERATURE_THREAD_PRIORITY 5

K_THREAD_DEFINE(temperature_thread, TEMPERATURE_THREAD_STACK_SIZE,
		temperature_thread_entry, NULL, NULL, NULL,
		TEMPERATURE_THREAD_PRIORITY, 0, 0);

int main(void)
{
	LOG_INF("========================================");
	LOG_INF("Spotflow Metrics Sample Application");
	LOG_INF("========================================");
	LOG_INF("");
	LOG_INF("This sample demonstrates:");
	LOG_INF("  - System metrics auto-collection");
	LOG_INF("  - Custom label-less metrics");
	LOG_INF("  - Custom labeled metrics");
	LOG_INF("  - Integration with logs");
	LOG_INF("");

	/* Wait for the initialization of Wi-Fi/Ethernet device */
	k_sleep(K_SECONDS(1));

#ifdef CONFIG_SPOTFLOW_USE_WIFI
	LOG_INF("Initializing Wi-Fi...");
	init_wifi();
	connect_to_wifi();
#endif

#ifdef CONFIG_SPOTFLOW_USE_ETH
	LOG_INF("Initializing Ethernet...");
	turn_on_dhcp_when_device_is_up();
#endif

	/* Initialize application metrics */
	if (init_application_metrics() < 0) {
		LOG_ERR("Failed to initialize application metrics");
		return -1;
	}

	LOG_INF("");
	LOG_INF("Starting metric reporting...");
	LOG_INF("");

	/* Main loop - report metrics periodically */
	for (int iteration = 0; iteration < 100; iteration++) {
		LOG_INF("=== Iteration %d ===", iteration);

		/* Report label-less counter every iteration */
		report_counter_metric();

		/* Temperature is reported by temperature_thread every 10 seconds */

		/* Report multiple HTTP requests to demonstrate labeled metrics */
		for (int req = 0; req < 3; req++) {
			report_request_duration_metric();
		}

		/* Add some application logs */
		if (iteration % 10 == 0) {
			LOG_WRN("Periodic health check at iteration %d", iteration);
		}

		k_sleep(K_SECONDS(2));
	}

	LOG_INF("Sample completed successfully");
	return 0;
}

/**
 * @brief Initialize application metrics
 */
static int init_application_metrics(void)
{
	/*
	 * Note: No need to explicitly call spotflow_metrics_init() - the metrics
	 * subsystem auto-initializes on first metric registration (lazy initialization).
	 */

	/* Register label-less integer metric - aggregated over 1 minute */
	g_app_counter_metric = spotflow_register_metric_int("app_counter", SPOTFLOW_AGG_INTERVAL_1MIN);
	if (!g_app_counter_metric) {
		LOG_ERR("Failed to register app_counter metric");
		return -ENOMEM;
	}
	LOG_INF("Registered metric: app_counter (int, 1MIN)");


	/* Register labeled float metric - aggregated over 1 minute */
	/* Supports up to 18 unique label combinations (3 endpoints × 2 methods × 3 statuses) */
	g_request_duration_metric = spotflow_register_metric_float_with_labels(
		"http_request_duration_ms",
		SPOTFLOW_AGG_INTERVAL_1MIN,
		18,  /* max_timeseries: 3 endpoints × 2 methods × 3 statuses = 18 */
		3    /* max_labels */
	);
	if (!g_request_duration_metric) {
		LOG_ERR("Failed to register request_duration metric");
		return -ENOMEM;
	}
	LOG_INF("Registered metric: http_request_duration_ms (float, labeled, 1MIN)");

	return 0;
}

/**
 * @brief Simulate application counter metric
 */
static void report_counter_metric(void)
{
	static int counter = 0;

	counter += 10;

	int rc = spotflow_report_metric_int(g_app_counter_metric, counter);
	if (rc < 0) {
		LOG_ERR("Failed to report counter metric: %d", rc);
	} else {
		LOG_DBG("Reported counter: %d", counter);
	}
}


/**
 * @brief Simulate temperature sensor reading (immediate metric)
 */
static void report_temperature_metric(void)
{
	/* Simulate temperature reading between 20.0 and 25.0 C */
	double temperature = 20.0 + ((double)(sys_rand32_get() % 500) / 100.0);

	int rc = spotflow_report_metric_float(g_temperature_metric, temperature);
	if (rc < 0) {
		LOG_ERR("Failed to report temperature: %d", rc);
	} else {
		LOG_INF("Reported temperature: %.2f C", temperature);
	}
}

/**
 * @brief Temperature monitoring thread entry point
 *
 * Reports temperature metric every 10 seconds in a dedicated thread.
 */
static void temperature_thread_entry()
{
	/* Register label-less float metric - immediate (no aggregation) */
	g_temperature_metric = spotflow_register_metric_float("temperature_celsius", SPOTFLOW_AGG_INTERVAL_NONE);
	if (!g_temperature_metric) {
		LOG_ERR("Failed to register temperature metric");
		return;
	}
	LOG_INF("Registered metric: temperature_celsius (float, NONE)");

	while (true) {
		report_temperature_metric();
		k_sleep(K_SECONDS(10));
	}
}

/**
 * @brief Simulate HTTP request duration with labels
 */
static void report_request_duration_metric(void)
{
	/* Simulate different endpoints and methods */
	const char *endpoints[] = {"/api/users", "/api/products", "/health"};
	const char *methods[] = {"GET", "POST"};
	const char *status_codes[] = {"200", "404", "500"};

	const char *endpoint = endpoints[sys_rand32_get() % 3];
	const char *method = methods[sys_rand32_get() % 2];
	const char *status = status_codes[sys_rand32_get() % 3];

	/* Simulate duration between 10ms and 500ms */
	double duration_ms = 10.0 + ((double)(sys_rand32_get() % 4900) / 10.0);

	/* Define labels */
	spotflow_label_t labels[] = {
		{.key = "endpoint", .value = endpoint},
		{.key = "method", .value = method},
		{.key = "status", .value = status}
	};

	int rc = spotflow_report_metric_float_with_labels(
		g_request_duration_metric,
		duration_ms,
		labels,
		3
	);

	if (rc < 0) {
		LOG_ERR("Failed to report request duration: %d", rc);
	} else {
		LOG_DBG("Reported request: %s %s -> %s (%.2f ms)",
			method, endpoint, status, duration_ms);
	}
}


#ifdef CONFIG_SPOTFLOW_USE_ETH
static void handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
		    struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_IF_UP) {
		LOG_INF("Interface is up -> starting DHCPv4");
		net_dhcpv4_start(iface);
	}
}

static void turn_on_dhcp_when_device_is_up()
{
	static struct net_mgmt_event_callback iface_cb;
	net_mgmt_init_event_callback(&iface_cb, handler, NET_EVENT_IF_UP);
	net_mgmt_add_event_callback(&iface_cb);
}
#endif
