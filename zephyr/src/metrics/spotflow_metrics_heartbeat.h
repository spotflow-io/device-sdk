/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_HEARTBEAT_H_
#define SPOTFLOW_METRICS_HEARTBEAT_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize heartbeat subsystem
 *
 * Starts the periodic heartbeat timer that sends device uptime messages.
 * Heartbeats use a separate buffer with higher priority than regular metrics.
 */
void spotflow_metrics_heartbeat_init(void);

/**
 * @brief Poll and process pending heartbeat message
 *
 * Checks for a pending heartbeat and publishes it via MQTT.
 * Called by processor thread before processing regular metrics.
 *
 * @return 1 if heartbeat processed, 0 if no heartbeat pending, negative errno on error
 */
int spotflow_poll_and_process_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_HEARTBEAT_H_ */
