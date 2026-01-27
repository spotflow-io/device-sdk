/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_BACKEND_H_
#define SPOTFLOW_METRICS_BACKEND_H_

#include "spotflow_metrics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize metrics subsystem
 *
 * OPTIONAL: The metrics subsystem auto-initializes on first metric registration.
 * You can call this explicitly if you want to control initialization timing,
 * but it's not required.
 *
 * Thread-safe: Yes (idempotent, safe to call multiple times)
 *
 * @return 0 on success, negative errno on failure
 */
int spotflow_metrics_init(void);

/**
 * @brief Register a label-less integer metric
 *
 * @param name Metric name (max 255 chars, will be normalized to lowercase/alphanumeric/_)
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_10MIN, SPOTFLOW_AGG_INTERVAL_1HOUR)
 *
 * @return Metric handle on success, NULL on failure
 *         Failures: -ENOMEM (registry full or allocation failed), -EINVAL (invalid params)
 */
struct spotflow_metric_int *spotflow_register_metric_int(
	const char *name,
	enum spotflow_agg_interval agg_interval);

/**
 * @brief Register a label-less float metric
 *
 * @param name Metric name (max 255 chars, will be normalized to lowercase/alphanumeric/_)
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_10MIN, SPOTFLOW_AGG_INTERVAL_1HOUR)
 *
 * @return Metric handle on success, NULL on failure
 */
struct spotflow_metric_float *spotflow_register_metric_float(
	const char *name,
	enum spotflow_agg_interval agg_interval);

/**
 * @brief Register a labeled integer metric
 *
 * @param name Metric name (max 255 chars, will be normalized)
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_10MIN, SPOTFLOW_AGG_INTERVAL_1HOUR)
 * @param max_timeseries Maximum number of unique label combinations (1-256)
 * @param max_labels Maximum labels per report (1-8)
 *
 * @return Metric handle on success, NULL on failure
 */
struct spotflow_metric_int *spotflow_register_metric_int_with_labels(
	const char *name,
	enum spotflow_agg_interval agg_interval,
	uint16_t max_timeseries,
	uint8_t max_labels);

/**
 * @brief Register a labeled float metric
 *
 * @param name Metric name (max 255 chars, will be normalized)
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_10MIN, SPOTFLOW_AGG_INTERVAL_1HOUR)
 * @param max_timeseries Maximum number of unique label combinations (1-256)
 * @param max_labels Maximum labels per report (1-8)
 *
 * @return Metric handle on success, NULL on failure
 */
struct spotflow_metric_float *spotflow_register_metric_float_with_labels(
	const char *name,
	enum spotflow_agg_interval agg_interval,
	uint16_t max_timeseries,
	uint8_t max_labels);

/**
 * @brief Report a label-less integer metric value
 *
 * @param metric Metric handle from registration
 * @param value Integer value to report
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid metric handle
 *         -EAGAIN: Aggregator busy (rare, retry)
 */
int spotflow_report_metric_int(
	struct spotflow_metric_int *metric,
	int64_t value);

/**
 * @brief Report a label-less float metric value
 *
 * @param metric Metric handle from registration
 * @param value Float value to report
 *
 * @return 0 on success, negative errno on failure
 */
int spotflow_report_metric_float(
	struct spotflow_metric_float *metric,
	float value);

/**
 * @brief Report a labeled integer metric value
 *
 * @param metric Metric handle from registration
 * @param value Integer value to report
 * @param labels Array of label key-value pairs
 * @param label_count Number of labels (must be <= max_labels from registration)
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid parameters (too many labels, NULL pointers)
 *         -ENOSPC: Time series pool full (max_timeseries limit reached)
 *         -EAGAIN: Aggregator busy (rare, retry)
 */
int spotflow_report_metric_int_with_labels(
	struct spotflow_metric_int *metric,
	int64_t value,
	const struct spotflow_label *labels,
	uint8_t label_count);

/**
 * @brief Report a labeled float metric value
 *
 * @param metric Metric handle from registration
 * @param value Float value to report
 * @param labels Array of label key-value pairs
 * @param label_count Number of labels
 *
 * @return 0 on success, negative errno on failure
 */
int spotflow_report_metric_float_with_labels(
	struct spotflow_metric_float *metric,
	float value,
	const struct spotflow_label *labels,
	uint8_t label_count);

/**
 * @brief Report an event for a label-less metric
 *
 * Events are point-in-time occurrences that are not aggregated over time.
 * This function is equivalent to calling spotflow_report_metric_int(metric, 1)
 * with PT0S aggregation interval. The value is always 1 (event occurred).
 *
 * @param metric Metric handle from registration (must be label-less)
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid metric handle or metric is labeled
 *         -ENOMEM: Metric queue is full
 */
int spotflow_report_event(struct spotflow_metric_int *metric);

/**
 * @brief Report an event with labels for a labeled metric
 *
 * Events are point-in-time occurrences that are not aggregated over time.
 * This function is equivalent to calling spotflow_report_metric_int_with_labels(metric, 1, ...)
 * with PT0S aggregation interval. The value is always 1 (event occurred).
 *
 * @param metric Metric handle from registration (must be labeled)
 * @param labels Array of label key-value pairs
 * @param label_count Number of labels
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid parameters
 *         -ENOMEM: Metric queue is full
 *         -ENOSPC: Time series pool is full
 */
int spotflow_report_event_with_labels(
	struct spotflow_metric_int *metric,
	const struct spotflow_label *labels,
	uint8_t label_count);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_BACKEND_H_ */
