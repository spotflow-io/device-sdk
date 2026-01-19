/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_SYSTEM_STACK_H_
#define SPOTFLOW_METRICS_SYSTEM_STACK_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize stack metrics
 *
 * Registers thread_stack_free_bytes metric.
 *
 * @return Number of metrics registered on success, negative errno on failure
 */
int spotflow_metrics_system_stack_init(void);

/**
 * @brief Collect and report stack metrics
 */
void spotflow_metrics_system_stack_collect(void);

/**
 * @brief Enable stack tracking for a specific thread
 *
 * Only used when CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS is disabled.
 *
 * @param thread Thread to track (or NULL for current thread)
 * @return 0 on success, negative errno on failure
 */
int spotflow_metrics_system_stack_enable_thread(struct k_thread *thread);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_SYSTEM_STACK_H_ */
