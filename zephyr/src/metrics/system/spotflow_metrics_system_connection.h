/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_CONNECTION_H_
#define SPOTFLOW_METRICS_SYSTEM_CONNECTION_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize connection metrics
 *
 * Registers connection_mqtt_connected metric.
 *
 * @return Number of metrics registered on success, negative errno on failure
 */
int spotflow_metrics_system_connection_init(void);

/**
 * @brief Report connection state
 *
 * @param connected True if connected, false if disconnected
 */
void spotflow_metrics_system_connection_report(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_CONNECTION_H_ */
