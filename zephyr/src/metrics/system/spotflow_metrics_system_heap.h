/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_HEAP_H_
#define SPOTFLOW_METRICS_SYSTEM_HEAP_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize heap metrics
 *
 * Registers heap_free_bytes and heap_allocated_bytes metrics.
 *
 * @return Number of metrics registered on success, negative errno on failure
 */
int spotflow_metrics_system_heap_init(void);

/**
 * @brief Collect and report heap metrics
 */
void spotflow_metrics_system_heap_collect(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_HEAP_H_ */
