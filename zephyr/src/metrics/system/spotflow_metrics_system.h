/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_H_
#define SPOTFLOW_METRICS_SYSTEM_H_

#include <stdbool.h>
#include <zephyr/kernel.h>
#include "../spotflow_metrics_types.h"

/* Map aggregation interval (seconds) to enum */
#if CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL == 0
#define SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL SPOTFLOW_AGG_INTERVAL_NONE
#elif CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL == 60
#define SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL SPOTFLOW_AGG_INTERVAL_1MIN
#elif CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL == 3600
#define SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL SPOTFLOW_AGG_INTERVAL_1HOUR
#elif CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL == 86400
#define SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL SPOTFLOW_AGG_INTERVAL_1DAY
#else
#error "Invalid SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL: must be 0, 60, 3600, or 86400"
#endif

/* System metric names */
#define SPOTFLOW_METRIC_NAME_CONNECTION "connection_mqtt_connected"
#define SPOTFLOW_METRIC_NAME_HEAP_FREE "heap_free_bytes"
#define SPOTFLOW_METRIC_NAME_HEAP_ALLOCATED "heap_allocated_bytes"
#define SPOTFLOW_METRIC_NAME_CPU "cpu_utilization_percent"
#define SPOTFLOW_METRIC_NAME_STACK_FREE "thread_stack_free_bytes"
#define SPOTFLOW_METRIC_NAME_STACK_USED "thread_stack_used_percent"
#define SPOTFLOW_METRIC_NAME_NETWORK_TX "network_tx_bytes"
#define SPOTFLOW_METRIC_NAME_NETWORK_RX "network_rx_bytes"
#define SPOTFLOW_METRIC_NAME_BOOT_RESET "boot_reset"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize system metrics auto-collection
 *
 * Registers system metrics (memory, heap, network, CPU, etc.) and
 * starts periodic collection timer.
 *
 * Should be called once during system initialization, after
 * network initialization.
 *
 * @return 0 on success, negative errno on failure
 */
int spotflow_metrics_system_init(void);

/**
 * @brief Report MQTT connection state change
 *
 * Called by MQTT layer when connection state changes.
 * Immediately reports connection state as event metric.
 *
 * @param connected True if connected, false if disconnected
 */
void spotflow_metrics_system_report_connection_state(bool connected);

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
/**
 * @brief Enable stack metrics collection for a specific thread
 *
 * Adds the thread to the list of tracked threads. Stack metrics will be
 * collected periodically for this thread.
 *
 * @param thread Thread to monitor (or NULL for current thread)
 * @return 0 on success, negative errno on failure
 *         -ENOMEM: Maximum tracked threads limit reached
 *         -EINVAL: Invalid thread pointer or already tracked
 */
int spotflow_metrics_system_enable_thread_stack(struct k_thread* thread);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_H_ */
