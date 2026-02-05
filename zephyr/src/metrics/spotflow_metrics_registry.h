/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_METRICS_REGISTRY_H_
#define SPOTFLOW_METRICS_REGISTRY_H_

#include "spotflow_metrics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register a label-less integer metric
 *
 * @param name Metric name (max 255 chars). The name is normalized before registration:
 *             - Alphanumeric characters are converted to lowercase
 *             - Dashes, dots, and spaces are converted to underscores
 *             - Other characters are removed
 *             Example: "My-Metric.Name" becomes "my_metric_name"
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_1HOUR, SPOTFLOW_AGG_INTERVAL_1DAY)
 * @param metric_out Output parameter for the registered metric handle
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid parameters (NULL name or metric_out, empty normalized name)
 *         -EEXIST: Metric with same name already registered
 *         -ENOMEM: Registry full or aggregator allocation failed
 */
int spotflow_register_metric_int(const char* name, enum spotflow_agg_interval agg_interval,
				 struct spotflow_metric_int** metric_out);

/**
 * @brief Register a label-less float metric
 *
 * @param name Metric name (max 255 chars). The name is normalized before registration:
 *             - Alphanumeric characters are converted to lowercase
 *             - Dashes, dots, and spaces are converted to underscores
 *             - Other characters are removed
 *             Example: "My-Metric.Name" becomes "my_metric_name"
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_1HOUR, SPOTFLOW_AGG_INTERVAL_1DAY)
 * @param metric_out Output parameter for the registered metric handle
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid parameters (NULL name or metric_out, empty normalized name)
 *         -EEXIST: Metric with same name already registered
 *         -ENOMEM: Registry full or aggregator allocation failed
 */
int spotflow_register_metric_float(const char* name, enum spotflow_agg_interval agg_interval,
				   struct spotflow_metric_float** metric_out);

/**
 * @brief Register a labeled integer metric
 *
 * @param name Metric name (max 255 chars). The name is normalized before registration:
 *             - Alphanumeric characters are converted to lowercase
 *             - Dashes, dots, and spaces are converted to underscores
 *             - Other characters are removed
 *             Example: "My-Metric.Name" becomes "my_metric_name"
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_1HOUR, SPOTFLOW_AGG_INTERVAL_1DAY)
 * @param max_timeseries Maximum number of unique label combinations (1-256)
 * @param max_labels Maximum labels per report (1-CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC)
 * @param metric_out Output parameter for the registered metric handle
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid parameters (NULL name/metric_out, empty normalized name,
 *                  invalid max_timeseries/max_labels, max_labels=0)
 *         -EEXIST: Metric with same name already registered
 *         -ENOMEM: Registry full or aggregator allocation failed
 */
int spotflow_register_metric_int_with_labels(const char* name,
					     enum spotflow_agg_interval agg_interval,
					     uint16_t max_timeseries, uint8_t max_labels,
					     struct spotflow_metric_int** metric_out);

/**
 * @brief Register a labeled float metric
 *
 * @param name Metric name (max 255 chars). The name is normalized before registration:
 *             - Alphanumeric characters are converted to lowercase
 *             - Dashes, dots, and spaces are converted to underscores
 *             - Other characters are removed
 *             Example: "My-Metric.Name" becomes "my_metric_name"
 * @param agg_interval Aggregation interval (SPOTFLOW_AGG_INTERVAL_NONE, SPOTFLOW_AGG_INTERVAL_1MIN,
 *                     SPOTFLOW_AGG_INTERVAL_1HOUR, SPOTFLOW_AGG_INTERVAL_1DAY)
 * @param max_timeseries Maximum number of unique label combinations (1-256)
 * @param max_labels Maximum labels per report (1-CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC)
 * @param metric_out Output parameter for the registered metric handle
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: Invalid parameters (NULL name/metric_out, empty normalized name,
 *                  invalid max_timeseries/max_labels, max_labels=0)
 *         -EEXIST: Metric with same name already registered
 *         -ENOMEM: Registry full or aggregator allocation failed
 */
int spotflow_register_metric_float_with_labels(const char* name,
					       enum spotflow_agg_interval agg_interval,
					       uint16_t max_timeseries, uint8_t max_labels,
					       struct spotflow_metric_float** metric_out);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_REGISTRY_H_ */
