/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_backend.h"
#include "spotflow_metrics_aggregator.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Public API Implementation */

int spotflow_report_metric_int(struct spotflow_metric_int* metric, int64_t value)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base* base = &metric->base;

	/* Label-less metrics have max_labels == 0 */
	if (base->max_labels > 0) {
		LOG_ERR("Use spotflow_report_metric_int_with_labels for labeled metrics");
		return -EINVAL;
	}

	/* Type-safe: int metrics always store int values */
	return aggregator_report_value(base, NULL, 0, value, 0.0);
}

int spotflow_report_metric_float(struct spotflow_metric_float* metric, float value)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base* base = &metric->base;

	/* Label-less metrics have max_labels == 0 */
	if (base->max_labels > 0) {
		LOG_ERR("Use spotflow_report_metric_float_with_labels for labeled metrics");
		return -EINVAL;
	}

	/* Type-safe: float metrics always store float values */
	return aggregator_report_value(base, NULL, 0, 0, value);
}

int spotflow_report_metric_int_with_labels(struct spotflow_metric_int* metric, int64_t value,
					   const struct spotflow_label* labels, uint8_t label_count)
{
	if (metric == NULL || labels == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base* base = &metric->base;

	/* Labeled metrics have max_labels > 0 */
	if (base->max_labels == 0) {
		LOG_ERR("Use spotflow_report_metric_int for label-less metrics");
		return -EINVAL;
	}

	if (label_count == 0 || label_count > base->max_labels) {
		LOG_ERR("Invalid label_count: %u (max %u)", label_count, base->max_labels);
		return -EINVAL;
	}

	/* Validate individual label elements */
	for (uint8_t i = 0; i < label_count; i++) {
		if (labels[i].key == NULL || labels[i].value == NULL) {
			LOG_ERR("Label key or value is NULL at index %u", i);
			return -EINVAL;
		}
		if (strlen(labels[i].key) >= SPOTFLOW_MAX_LABEL_KEY_LEN) {
			LOG_WRN("Label key at index %u will be truncated", i);
		}
		if (strlen(labels[i].value) >= SPOTFLOW_MAX_LABEL_VALUE_LEN) {
			LOG_WRN("Label value at index %u will be truncated", i);
		}
	}

	/* Type-safe: int metrics always store int values */
	return aggregator_report_value(base, labels, label_count, value, 0.0);
}

int spotflow_report_metric_float_with_labels(struct spotflow_metric_float* metric, float value,
					     const struct spotflow_label* labels,
					     uint8_t label_count)
{
	if (metric == NULL || labels == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base* base = &metric->base;

	/* Labeled metrics have max_labels > 0 */
	if (base->max_labels == 0) {
		LOG_ERR("Use spotflow_report_metric_float for label-less metrics");
		return -EINVAL;
	}

	if (label_count == 0 || label_count > base->max_labels) {
		LOG_ERR("Invalid label_count: %u (max %u)", label_count, base->max_labels);
		return -EINVAL;
	}

	/* Validate individual label elements */
	for (uint8_t i = 0; i < label_count; i++) {
		if (labels[i].key == NULL || labels[i].value == NULL) {
			LOG_ERR("Label key or value is NULL at index %u", i);
			return -EINVAL;
		}
		if (strlen(labels[i].key) >= SPOTFLOW_MAX_LABEL_KEY_LEN) {
			LOG_WRN("Label key at index %u will be truncated", i);
		}
		if (strlen(labels[i].value) >= SPOTFLOW_MAX_LABEL_VALUE_LEN) {
			LOG_WRN("Label value at index %u will be truncated", i);
		}
	}

	/* Type-safe: float metrics always store float values */
	return aggregator_report_value(base, labels, label_count, 0, value);
}

int spotflow_report_event(struct spotflow_metric_int* metric)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base* base = &metric->base;

	/* Label-less metrics have max_labels == 0 */
	if (base->max_labels > 0) {
		LOG_ERR("Use spotflow_report_event_with_labels for labeled metrics");
		return -EINVAL;
	}

	/* Events report value of 1 (event occurred) */
	return aggregator_report_value(base, NULL, 0, 1, 0.0);
}

int spotflow_report_event_with_labels(struct spotflow_metric_int* metric,
				      const struct spotflow_label* labels, uint8_t label_count)
{
	if (metric == NULL || labels == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base* base = &metric->base;

	/* Labeled metrics have max_labels > 0 */
	if (base->max_labels == 0) {
		LOG_ERR("Use spotflow_report_event for label-less metrics");
		return -EINVAL;
	}

	if (label_count == 0 || label_count > base->max_labels) {
		LOG_ERR("Invalid label_count: %u (max %u)", label_count, base->max_labels);
		return -EINVAL;
	}

	/* Validate individual label elements */
	for (uint8_t i = 0; i < label_count; i++) {
		if (labels[i].key == NULL || labels[i].value == NULL) {
			LOG_ERR("Label key or value is NULL at index %u", i);
			return -EINVAL;
		}
		if (strlen(labels[i].key) >= SPOTFLOW_MAX_LABEL_KEY_LEN) {
			LOG_WRN("Label key at index %u will be truncated", i);
		}
		if (strlen(labels[i].value) >= SPOTFLOW_MAX_LABEL_VALUE_LEN) {
			LOG_WRN("Label value at index %u will be truncated", i);
		}
	}

	/* Events report value of 1 (event occurred) */
	return aggregator_report_value(base, labels, label_count, 1, 0.0);
}
