/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_system.h"
#include "spotflow_metrics_backend.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
#include <zephyr/sys/heap_listener.h>
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_stats.h>
#endif

/* CPU metrics use k_thread_runtime_stats_all_get() from kernel.h */

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
#include "spotflow_reset_helper.h"
#endif

LOG_MODULE_REGISTER(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
static spotflow_metric_int_t* g_heap_free_metric;
static spotflow_metric_int_t* g_heap_allocated_metric;
extern struct sys_heap _system_heap;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
static spotflow_metric_int_t* g_network_tx_metric;
static spotflow_metric_int_t* g_network_rx_metric;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
static spotflow_metric_float_t* g_cpu_utilization_metric;
static uint64_t g_last_idle_cycles = 0;
static uint64_t g_last_total_cycles = 0;
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
static spotflow_metric_int_t* g_connection_state_metric;
#endif

/* Collection timer */
static struct k_work_delayable g_collection_work;

/* Initialization state (Issue 5 fix - use atomic for thread safety):
 * 0 = not initialized
 * 1 = initialization in progress
 * 2 = fully initialized
 */
static atomic_t g_system_metrics_init_state = ATOMIC_INIT(0);

static void collection_timer_handler(struct k_work* work);

int spotflow_metrics_system_init(void)
{
	/* Fast path: already fully initialized */
	if (atomic_get(&g_system_metrics_init_state) == 2) {
		return 0;
	}

	/* Try to claim initialization (0 -> 1) */
	if (!atomic_cas(&g_system_metrics_init_state, 0, 1)) {
		/* Another thread is initializing - wait for completion */
		while (atomic_get(&g_system_metrics_init_state) != 2) {
			k_yield();
		}
		return 0;
	}

	/*
	 * Note: No need to explicitly call spotflow_metrics_init() - the backend
	 * auto-initializes on first metric registration (lazy initialization).
	 */

	LOG_DBG("Initializing system metrics auto-collection");

	int registered_count = 0;

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
	/* Register heap metrics (aggregated over 1 minute) */
	g_heap_free_metric = spotflow_register_metric_int("heap_free_bytes", "PT1M");
	if (!g_heap_free_metric) {
		LOG_ERR("Failed to register heap free metric");
		return -ENOMEM;
	}
	registered_count++;

	g_heap_allocated_metric = spotflow_register_metric_int("heap_allocated_bytes", "PT1M");
	if (!g_heap_allocated_metric) {
		LOG_ERR("Failed to register heap allocated metric");
		return -ENOMEM;
	}
	registered_count++;
	LOG_INF("Registered heap metrics");
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
	/* Register network metrics (labeled, aggregated over 1 minute) */
	/* Max 4 time series to support typical multi-interface devices */
	g_network_tx_metric =
	    spotflow_register_metric_int_with_labels("network_tx_bytes", "PT1M", 4, 1);
	if (!g_network_tx_metric) {
		LOG_ERR("Failed to register network TX metric");
		return -ENOMEM;
	}
	registered_count++;

	g_network_rx_metric =
	    spotflow_register_metric_int_with_labels("network_rx_bytes", "PT1M", 4, 1);
	if (!g_network_rx_metric) {
		LOG_ERR("Failed to register network RX metric");
		return -ENOMEM;
	}
	registered_count++;
	LOG_INF("Registered network metrics");
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
	/* Register CPU utilization metric (aggregated over 1 minute) */
	g_cpu_utilization_metric =
	    spotflow_register_metric_float("cpu_utilization_percent", "PT1M");
	if (!g_cpu_utilization_metric) {
		LOG_ERR("Failed to register CPU utilization metric");
		return -ENOMEM;
	}
	registered_count++;
	LOG_INF("Registered CPU utilization metric");
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	/* Register connection state metric (immediate/event-driven) */
	g_connection_state_metric =
	    spotflow_register_metric_int("connection_mqtt_connected", "PT0S");
	if (!g_connection_state_metric) {
		LOG_ERR("Failed to register connection state metric");
		return -ENOMEM;
	}
	registered_count++;
	LOG_INF("Registered connection state metric");
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
	/* Report reset cause once on boot */
	report_reboot_reason();
#endif

	/* Initialize collection timer */
	k_work_init_delayable(&g_collection_work, collection_timer_handler);

	/* Start periodic collection */
	k_work_schedule(&g_collection_work, K_SECONDS(CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL));

	/* Mark as fully initialized */
	atomic_set(&g_system_metrics_init_state, 2);

	LOG_INF("System metrics initialized: %d metrics registered, collection interval=%d "
		"seconds",
		registered_count, CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL);

	return 0;
}

void spotflow_metrics_system_report_connection_state(bool connected)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	if (atomic_get(&g_system_metrics_init_state) != 2 || !g_connection_state_metric) {
		return;
	}

	int rc = spotflow_report_metric_int(g_connection_state_metric, connected ? 1 : 0);
	if (rc < 0) {
		LOG_ERR("Failed to report connection state: %d", rc);
		return;
	}

	LOG_INF("MQTT connection state: %s", connected ? "connected" : "disconnected");
#endif
}

/**
 * @brief Collect and report heap metrics
 */
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
static void collect_heap_metrics(void)
{
	/* Safety check: metrics must be registered */
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
#endif

/**
 * @brief Callback for reporting network metrics for each interface
 */
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
static void report_network_interface_metrics(struct net_if* iface, void* user_data)
{
	int* if_count = (int*)user_data;

	/* Skip interfaces that are not up */
	if (!net_if_is_up(iface)) {
		return;
	}

	/* Get device and validate */
	const struct device* dev = net_if_get_device(iface);
	if (!dev || !dev->name) {
		LOG_WRN("Interface has no valid device/name");
		return;
	}

	const char* if_name = dev->name;

	/* Get network statistics */
	uint64_t tx_bytes = 0;
	uint64_t rx_bytes = 0;

	/* Access statistics if enabled - use structure field directly */
	struct net_stats* stats = &iface->stats;
	tx_bytes = stats->bytes.sent;
	rx_bytes = stats->bytes.received;

	/* Create labels for interface name */
	spotflow_label_t labels[] = { { .key = "interface", .value = if_name } };

	/* Report TX bytes */
	int rc = spotflow_report_metric_int_with_labels(g_network_tx_metric, (int64_t)tx_bytes,
							labels, 1);
	if (rc < 0) {
		LOG_ERR("Failed to report network TX for %s: %d", if_name, rc);
	}

	/* Report RX bytes */
	rc = spotflow_report_metric_int_with_labels(g_network_rx_metric, (int64_t)rx_bytes, labels,
						    1);
	if (rc < 0) {
		LOG_ERR("Failed to report network RX for %s: %d", if_name, rc);
	}

	LOG_DBG("Network %s: TX=%llu bytes, RX=%llu bytes", if_name, (unsigned long long)tx_bytes,
		(unsigned long long)rx_bytes);

	(*if_count)++;
}

/**
 * @brief Collect and report network metrics
 */
static void collect_network_metrics(void)
{
	int if_count = 0;

	/* Safety check: metrics must be registered */
	if (!g_network_tx_metric || !g_network_rx_metric) {
		LOG_ERR("Network metrics not registered");
		return;
	}

	/* Iterate through all network interfaces using the proper Zephyr API */
	net_if_foreach(report_network_interface_metrics, &if_count);

	if (if_count == 0) {
		LOG_DBG("No active network interfaces found");
	}
}
#endif

/**
 * @brief Collect and report CPU utilization
 */
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
static void collect_cpu_metrics(void)
{
	/* Safety check: metric must be registered */
	if (!g_cpu_utilization_metric) {
		LOG_ERR("CPU metric not registered");
		return;
	}

	/* Get all runtime stats (includes idle thread) */
	k_thread_runtime_stats_t stats;
	int rc = k_thread_runtime_stats_all_get(&stats);
	if (rc < 0) {
		LOG_WRN("Failed to get all thread stats: %d", rc);
		return;
	}

	/*
	 * CPU utilization calculation:
	 * - stats.execution_cycles = total cycles spent in non-idle threads
	 * - stats.idle_cycles = total cycles spent in idle thread
	 * - Utilization = (non-idle cycles / total cycles) × 100
	 */
	uint64_t total_cycles = stats.execution_cycles + stats.idle_cycles;

	/* Calculate CPU utilization percentage */
	if (g_last_total_cycles > 0 && total_cycles > g_last_total_cycles) {
		uint64_t delta_total = total_cycles - g_last_total_cycles;
		uint64_t delta_active = stats.execution_cycles - g_last_idle_cycles;

		if (delta_total > 0) {
			double utilization = 100.0 * ((double)delta_active / (double)delta_total);

			/* Clamp to 0-100 range */
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

	/* Save current values for next iteration */
	g_last_idle_cycles = stats.execution_cycles; /* Store active cycles */
	g_last_total_cycles = total_cycles;
}
#endif


/**
 * @brief Periodic collection timer handler
 */
static void collection_timer_handler(struct k_work* work)
{
	LOG_DBG("Collecting system metrics...");
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
	collect_heap_metrics();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
	collect_network_metrics();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
	collect_cpu_metrics();
#endif

	/* Reschedule for next collection */
	k_work_schedule(&g_collection_work, K_SECONDS(CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL));
}




