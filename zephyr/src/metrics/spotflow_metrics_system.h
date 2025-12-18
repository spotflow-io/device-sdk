/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_H_
#define SPOTFLOW_METRICS_SYSTEM_H_

#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_H_ */
