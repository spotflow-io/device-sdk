/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_registry.h"
#include "spotflow_metrics_aggregator.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <ctype.h>

LOG_MODULE_REGISTER(spotflow_metrics_registry, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Global metric registry - union to support both int and float metrics in same array
 *
 * IMPORTANT: Both int_metric and float_metric have identical memory layout
 * (single 'base' member as first field), allowing safe access via int_metric.base
 * regardless of actual type. This pattern is used throughout registry operations
 * to simplify slot management.
 */
union metric_registry_entry {
	struct spotflow_metric_int int_metric;
	struct spotflow_metric_float float_metric;
};

static union metric_registry_entry g_metric_registry[CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED];
static K_MUTEX_DEFINE(g_registry_lock);

/* Forward declarations of static functions */
static void normalize_metric_name(const char* input, char* output, size_t output_size);
static int find_available_slot(void);
static struct spotflow_metric_base* find_metric_by_name(const char* normalized_name);
static int validate_metric_params(const char* name, uint16_t max_timeseries, uint8_t max_labels);
static int normalize_and_validate_metric_name(const char* name, char* out_normalized,
					      size_t out_size);
static void init_metric_struct(struct spotflow_metric_base* metric, const char* normalized_name,
			       enum spotflow_metric_type type,
			       enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
			       uint8_t max_labels);
static int register_metric_common(const char* name, enum spotflow_metric_type type,
				  enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
				  uint8_t max_labels, struct spotflow_metric_base** metric_out);

/* Public API Implementation */

int spotflow_register_metric_int(const char* name, enum spotflow_agg_interval agg_interval,
				 struct spotflow_metric_int** metric_out)
{
	struct spotflow_metric_base* base;
	int rc;

	if (metric_out == NULL) {
		LOG_ERR("metric_out cannot be NULL");
		return -EINVAL;
	}

	rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, 1, 0, &base);
	if (rc < 0) {
		return rc;
	}
	/* Validate type matches before cast */
	if (base->type != SPOTFLOW_METRIC_TYPE_INT) {
		LOG_ERR("Type mismatch: expected INT, got %d", base->type);
		return -EINVAL;
	}
	/* The base is the first member of spotflow_metric_int, so we can safely cast */
	*metric_out = (struct spotflow_metric_int*)base;
	return 0;
}

int spotflow_register_metric_float(const char* name, enum spotflow_agg_interval agg_interval,
				   struct spotflow_metric_float** metric_out)
{
	struct spotflow_metric_base* base;
	int rc;

	if (metric_out == NULL) {
		LOG_ERR("metric_out cannot be NULL");
		return -EINVAL;
	}

	rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, 1, 0, &base);
	if (rc < 0) {
		return rc;
	}
	/* Validate type matches before cast */
	if (base->type != SPOTFLOW_METRIC_TYPE_FLOAT) {
		LOG_ERR("Type mismatch: expected FLOAT, got %d", base->type);
		return -EINVAL;
	}
	*metric_out = (struct spotflow_metric_float*)base;
	return 0;
}

int spotflow_register_metric_int_with_labels(const char* name,
					     enum spotflow_agg_interval agg_interval,
					     uint16_t max_timeseries, uint8_t max_labels,
					     struct spotflow_metric_int** metric_out)
{
	struct spotflow_metric_base* base;
	int rc;

	if (metric_out == NULL) {
		LOG_ERR("metric_out cannot be NULL");
		return -EINVAL;
	}

	if (max_labels == 0) {
		LOG_ERR("Labeled metric requires max_labels > 0");
		return -EINVAL;
	}

	rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, max_timeseries,
				    max_labels, &base);
	if (rc < 0) {
		return rc;
	}
	/* Validate type matches before cast */
	if (base->type != SPOTFLOW_METRIC_TYPE_INT) {
		LOG_ERR("Type mismatch: expected INT, got %d", base->type);
		return -EINVAL;
	}
	*metric_out = (struct spotflow_metric_int*)base;
	return 0;
}

int spotflow_register_metric_float_with_labels(const char* name,
					       enum spotflow_agg_interval agg_interval,
					       uint16_t max_timeseries, uint8_t max_labels,
					       struct spotflow_metric_float** metric_out)
{
	struct spotflow_metric_base* base;
	int rc;

	if (metric_out == NULL) {
		LOG_ERR("metric_out cannot be NULL");
		return -EINVAL;
	}

	if (max_labels == 0) {
		LOG_ERR("Labeled metric requires max_labels > 0");
		return -EINVAL;
	}

	rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, max_timeseries,
				    max_labels, &base);
	if (rc < 0) {
		return rc;
	}
	/* Validate type matches before cast */
	if (base->type != SPOTFLOW_METRIC_TYPE_FLOAT) {
		LOG_ERR("Type mismatch: expected FLOAT, got %d", base->type);
		return -EINVAL;
	}
	*metric_out = (struct spotflow_metric_float*)base;
	return 0;
}

/* Static function implementations */

/**
 * @brief Normalize metric name to lowercase alphanumeric with underscores
 */
static void normalize_metric_name(const char* input, char* output, size_t output_size)
{
	size_t i = 0;
	size_t j = 0;

	while (input[i] != '\0' && j < (output_size - 1)) {
		char c = input[i];

		if (isalnum((unsigned char)c)) {
			output[j++] = tolower((unsigned char)c);
		} else if (c == '_' || c == '-' || c == '.' || c == ' ') {
			output[j++] = '_';
		}
		/* Skip other characters */

		i++;
	}

	output[j] = '\0';
}

/**
 * @brief Find available registry slot
 */
static int find_available_slot(void)
{
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
		if (g_metric_registry[i].int_metric.base.aggregator_context == NULL) {
			return i;
		}
	}
	return -1;
}

/**
 * @brief Check if metric name already exists
 *
 * Must be called with g_registry_lock held.
 *
 * @return Pointer to existing metric base if found, NULL otherwise
 */
static struct spotflow_metric_base* find_metric_by_name(const char* normalized_name)
{
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
		struct spotflow_metric_base* base = &g_metric_registry[i].int_metric.base;
		if (base->aggregator_context != NULL && strcmp(base->name, normalized_name) == 0) {
			return base;
		}
	}
	return NULL;
}

/**
 * @brief Validate metric registration parameters
 *
 * @return 0 on success, -EINVAL on validation failure
 */
static int validate_metric_params(const char* name, uint16_t max_timeseries, uint8_t max_labels)
{
	if (name == NULL) {
		LOG_ERR("Metric name cannot be NULL");
		return -EINVAL;
	}

	if (max_timeseries == 0 || max_timeseries > 256) {
		LOG_ERR("Invalid max_timeseries: %u (must be 1-256)", max_timeseries);
		return -EINVAL;
	}

	if (max_labels > CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC) {
		LOG_ERR("Invalid max_labels: %u (max %d)", max_labels,
			CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC);
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Normalize metric name and validate the result
 *
 * @return 0 on success, -EINVAL if normalized name is empty
 */
static int normalize_and_validate_metric_name(const char* name, char* out_normalized,
					      size_t out_size)
{
	normalize_metric_name(name, out_normalized, out_size);

	if (strcmp(name, out_normalized) != 0) {
		LOG_WRN("Metric name '%s' normalized to '%s'", name, out_normalized);
	}

	if (strlen(out_normalized) == 0) {
		LOG_ERR("Metric name '%s' normalizes to empty string", name);
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Initialize metric structure fields
 */
static void init_metric_struct(struct spotflow_metric_base* metric, const char* normalized_name,
			       enum spotflow_metric_type type,
			       enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
			       uint8_t max_labels)
{
	strncpy(metric->name, normalized_name, sizeof(metric->name) - 1);
	metric->name[sizeof(metric->name) - 1] = '\0';

	metric->type = type;
	metric->agg_interval = agg_interval;
	metric->max_timeseries = max_timeseries;
	metric->max_labels = max_labels;
	metric->sequence_number = 0;

	k_mutex_init(&metric->lock);
}

/**
 * @brief Common registration logic
 */
static int register_metric_common(const char* name, enum spotflow_metric_type type,
				  enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
				  uint8_t max_labels, struct spotflow_metric_base** metric_out)
{
	int rc = validate_metric_params(name, max_timeseries, max_labels);
	if (rc < 0) {
		return rc;
	}

	char normalized_name[256];
	rc = normalize_and_validate_metric_name(name, normalized_name, sizeof(normalized_name));
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&g_registry_lock, K_FOREVER);

	/* Check for duplicate metric name */
	if (find_metric_by_name(normalized_name) != NULL) {
		LOG_ERR("Metric '%s' already registered", normalized_name);
		k_mutex_unlock(&g_registry_lock);
		return -EEXIST;
	}

	/* Find available slot */
	int slot = find_available_slot();
	if (slot < 0) {
		LOG_ERR("Metric registry full (%d/%d)", CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED,
			CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED);
		k_mutex_unlock(&g_registry_lock);
		return -ENOSPC;
	}

	struct spotflow_metric_base* metric = &g_metric_registry[slot].int_metric.base;
	init_metric_struct(metric, normalized_name, type, agg_interval, max_timeseries, max_labels);

	/* Initialize aggregator context */
	rc = aggregator_register_metric(metric);
	if (rc < 0) {
		LOG_ERR("Failed to initialize aggregator for metric '%s': %d", normalized_name, rc);
		/* Rollback: fully reset registry slot to avoid zombie entries */
		memset(&g_metric_registry[slot], 0, sizeof(g_metric_registry[slot]));
		k_mutex_unlock(&g_registry_lock);
		return rc;
	}

	k_mutex_unlock(&g_registry_lock);

	LOG_INF("Registered metric '%s' (type=%s, agg=%d, max_ts=%u, max_labels=%u)",
		normalized_name,
		(type == SPOTFLOW_METRIC_TYPE_INT)	   ? "int"
		    : (type == SPOTFLOW_METRIC_TYPE_FLOAT) ? "float"
							   : "unknown",
		metric->agg_interval, max_timeseries, max_labels);

	*metric_out = metric;
	return 0;
}
