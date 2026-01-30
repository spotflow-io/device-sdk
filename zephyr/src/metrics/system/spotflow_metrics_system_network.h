/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_NETWORK_H_
#define SPOTFLOW_METRICS_SYSTEM_NETWORK_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize network metrics
 *
 * Registers network_tx_bytes and network_rx_bytes metrics.
 *
 * @return Number of metrics registered on success, negative errno on failure
 */
int spotflow_metrics_system_network_init(void);

/**
 * @brief Collect and report network metrics
 */
void spotflow_metrics_system_network_collect(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_NETWORK_H_ */
