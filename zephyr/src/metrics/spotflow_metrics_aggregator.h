/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_AGGREGATOR_H_
#define SPOTFLOW_METRICS_AGGREGATOR_H_

#include "spotflow_metrics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register metric with aggregator
 *
 * Allocates aggregation context and time series storage.
 * Contract: MUST be atomic - either full success (returns 0)
 * or full failure (returns -ENOMEM with no side effects).
 *
 * @param metric Metric base to register
 *
 * @return 0 on success, -ENOMEM on failure
 */
int aggregator_register_metric(struct spotflow_metric_base *metric);

/**
 * @brief Report value to aggregator
 *
 * Updates aggregation state (sum, count, min, max) for the given
 * label combination. Creates new time series if needed.
 *
 * @param metric Metric base handle
 * @param labels Label array (NULL for label-less)
 * @param label_count Number of labels (0 for label-less)
 * @param value_int Integer value (if metric type is INT)
 * @param value_float Float value (if metric type is FLOAT)
 *
 * @return 0 on success, negative errno on failure
 *         -ENOSPC: Time series pool full
 *         -ENOMEM: Allocation failure
 */
int aggregator_report_value(
	struct spotflow_metric_base *metric,
	const struct spotflow_label *labels,
	uint8_t label_count,
	int64_t value_int,
	float value_float);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_AGGREGATOR_H_ */
