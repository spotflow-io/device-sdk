/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_H_
#define SPOTFLOW_METRICS_SYSTEM_H_

#include <stdbool.h>
#include <zephyr/kernel.h>

/* Map collection interval to ISO 8601 duration string for metric aggregation */
#if CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL == 60
#define SPOTFLOW_METRICS_SYSTEM_INTERVAL_STR "PT1M"
#elif CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL == 600
#define SPOTFLOW_METRICS_SYSTEM_INTERVAL_STR "PT10M"
#elif CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL == 3600
#define SPOTFLOW_METRICS_SYSTEM_INTERVAL_STR "PT1H"
#endif

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
 * spotflow_metrics_init() and network initialization.
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
int spotflow_metrics_system_enable_thread_stack(struct k_thread *thread);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_H_ */
