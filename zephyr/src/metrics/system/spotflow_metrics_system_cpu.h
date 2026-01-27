/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_CPU_H_
#define SPOTFLOW_METRICS_SYSTEM_CPU_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize CPU metrics
 *
 * Registers cpu_utilization_percent metric.
 *
 * @return Number of metrics registered on success, negative errno on failure
 */
int spotflow_metrics_system_cpu_init(void);

/**
 * @brief Collect and report CPU metrics
 */
void spotflow_metrics_system_cpu_collect(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_CPU_H_ */
