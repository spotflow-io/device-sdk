/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_backend.h"
#include "spotflow_metrics_aggregator.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <string.h>
#include <ctype.h>

LOG_MODULE_REGISTER(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Global metric registry - union to support both int and float metrics in same array */
union metric_registry_entry {
	struct spotflow_metric_int int_metric;
	struct spotflow_metric_float float_metric;
};

static union metric_registry_entry g_metric_registry[CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED];
static struct k_mutex g_registry_lock;

/* Initialization state:
 * 0 = not initialized
 * 1 = initialization in progress
 * 2 = fully initialized
 */
static atomic_t g_metrics_init_state = ATOMIC_INIT(0);

/* Forward declarations of static functions */
static void normalize_metric_name(const char *input, char *output, size_t output_size);
static int find_available_slot(void);
static struct spotflow_metric_base *find_metric_by_name(const char *normalized_name);
static struct spotflow_metric_base *register_metric_common(const char *name,
							   enum spotflow_metric_type type,
							   enum spotflow_agg_interval agg_interval,
							   uint16_t max_timeseries,
							   uint8_t max_labels);

/* Public API Implementation */

int spotflow_metrics_init(void)
{
	/* Fast path: already fully initialized */
	if (atomic_get(&g_metrics_init_state) == 2) {
		return 0;
	}

	/* Try to claim initialization (0 -> 1) */
	if (atomic_cas(&g_metrics_init_state, 0, 1)) {
		/* We won the race - perform initialization */
		k_mutex_init(&g_registry_lock);
		memset(g_metric_registry, 0, sizeof(g_metric_registry));

		/* Mark as fully initialized */
		atomic_set(&g_metrics_init_state, 2);

		LOG_INF("Metrics subsystem initialized");
		return 0;
	}

	/* Another thread is initializing - wait for completion */
	while (atomic_get(&g_metrics_init_state) != 2) {
		k_yield();
	}

	return 0;
}

struct spotflow_metric_int *spotflow_register_metric_int(const char *name,
							 enum spotflow_agg_interval agg_interval)
{
	struct spotflow_metric_base *base =
		register_metric_common(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, 1, 0);
	if (base == NULL) {
		return NULL;
	}
	/* The base is the first member of spotflow_metric_int, so we can safely cast */
	return (struct spotflow_metric_int *)base;
}

struct spotflow_metric_float *spotflow_register_metric_float(const char *name,
							     enum spotflow_agg_interval agg_interval)
{
	struct spotflow_metric_base *base =
		register_metric_common(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, 1, 0);
	if (base == NULL) {
		return NULL;
	}
	return (struct spotflow_metric_float *)base;
}

struct spotflow_metric_int *spotflow_register_metric_int_with_labels(
	const char *name, enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
	uint8_t max_labels)
{
	if (max_labels == 0) {
		LOG_ERR("Labeled metric requires max_labels > 0");
		return NULL;
	}

	struct spotflow_metric_base *base = register_metric_common(
		name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, max_timeseries, max_labels);
	if (base == NULL) {
		return NULL;
	}
	return (struct spotflow_metric_int *)base;
}

struct spotflow_metric_float *spotflow_register_metric_float_with_labels(
	const char *name, enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
	uint8_t max_labels)
{
	if (max_labels == 0) {
		LOG_ERR("Labeled metric requires max_labels > 0");
		return NULL;
	}

	struct spotflow_metric_base *base = register_metric_common(
		name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, max_timeseries, max_labels);
	if (base == NULL) {
		return NULL;
	}
	return (struct spotflow_metric_float *)base;
}

int spotflow_report_metric_int(struct spotflow_metric_int *metric, int64_t value)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base *base = &metric->base;

	/* Label-less metrics have max_labels == 0 */
	if (base->max_labels > 0) {
		LOG_ERR("Use spotflow_report_metric_int_with_labels for labeled metrics");
		return -EINVAL;
	}

	/* Type-safe: int metrics always store int values */
	return aggregator_report_value(base, NULL, 0, value, 0.0);
}

int spotflow_report_metric_float(struct spotflow_metric_float *metric, float value)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base *base = &metric->base;

	/* Label-less metrics have max_labels == 0 */
	if (base->max_labels > 0) {
		LOG_ERR("Use spotflow_report_metric_float_with_labels for labeled metrics");
		return -EINVAL;
	}

	/* Type-safe: float metrics always store float values */
	return aggregator_report_value(base, NULL, 0, 0, value);
}

int spotflow_report_metric_int_with_labels(struct spotflow_metric_int *metric, int64_t value,
					   const struct spotflow_label *labels, uint8_t label_count)
{
	if (metric == NULL || labels == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base *base = &metric->base;

	/* Labeled metrics have max_labels > 0 */
	if (base->max_labels == 0) {
		LOG_ERR("Use spotflow_report_metric_int for label-less metrics");
		return -EINVAL;
	}

	if (label_count == 0 || label_count > base->max_labels) {
		LOG_ERR("Invalid label_count: %u (max %u)", label_count, base->max_labels);
		return -EINVAL;
	}

	/* Type-safe: int metrics always store int values */
	return aggregator_report_value(base, labels, label_count, value, 0.0);
}

int spotflow_report_metric_float_with_labels(struct spotflow_metric_float *metric, float value,
					     const struct spotflow_label *labels,
					     uint8_t label_count)
{
	if (metric == NULL || labels == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base *base = &metric->base;

	/* Labeled metrics have max_labels > 0 */
	if (base->max_labels == 0) {
		LOG_ERR("Use spotflow_report_metric_float for label-less metrics");
		return -EINVAL;
	}

	if (label_count == 0 || label_count > base->max_labels) {
		LOG_ERR("Invalid label_count: %u (max %u)", label_count, base->max_labels);
		return -EINVAL;
	}

	/* Type-safe: float metrics always store float values */
	return aggregator_report_value(base, labels, label_count, 0, value);
}

int spotflow_report_event(struct spotflow_metric_int *metric)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base *base = &metric->base;

	/* Label-less metrics have max_labels == 0 */
	if (base->max_labels > 0) {
		LOG_ERR("Use spotflow_report_event_with_labels for labeled metrics");
		return -EINVAL;
	}

	/* Events report value of 1 (event occurred) */
	return aggregator_report_value(base, NULL, 0, 1, 0.0);
}

int spotflow_report_event_with_labels(struct spotflow_metric_int *metric,
				      const struct spotflow_label *labels, uint8_t label_count)
{
	if (metric == NULL || labels == NULL) {
		return -EINVAL;
	}

	struct spotflow_metric_base *base = &metric->base;

	/* Labeled metrics have max_labels > 0 */
	if (base->max_labels == 0) {
		LOG_ERR("Use spotflow_report_event for label-less metrics");
		return -EINVAL;
	}

	if (label_count == 0 || label_count > base->max_labels) {
		LOG_ERR("Invalid label_count: %u (max %u)", label_count, base->max_labels);
		return -EINVAL;
	}

	/* Events report value of 1 (event occurred) */
	return aggregator_report_value(base, labels, label_count, 1, 0.0);
}

/* Static function implementations */

/**
 * @brief Normalize metric name to lowercase alphanumeric with underscores
 */
static void normalize_metric_name(const char *input, char *output, size_t output_size)
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
static struct spotflow_metric_base *find_metric_by_name(const char *normalized_name)
{
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
		struct spotflow_metric_base *base = &g_metric_registry[i].int_metric.base;
		if (base->aggregator_context != NULL &&
		    strcmp(base->name, normalized_name) == 0) {
			return base;
		}
	}
	return NULL;
}

/**
 * @brief Common registration logic
 */
static struct spotflow_metric_base *register_metric_common(const char *name,
							   enum spotflow_metric_type type,
							   enum spotflow_agg_interval agg_interval,
							   uint16_t max_timeseries,
							   uint8_t max_labels)
{
	if (name == NULL) {
		LOG_ERR("Metric name cannot be NULL");
		return NULL;
	}

	/* Auto-initialize metrics subsystem on first use (thread-safe) */
	if (atomic_get(&g_metrics_init_state) != 2) {
		int rc = spotflow_metrics_init();
		if (rc < 0) {
			LOG_ERR("Failed to auto-initialize metrics subsystem: %d", rc);
			return NULL;
		}
	}

	/* Validate parameters */
	if (max_timeseries == 0 || max_timeseries > 256) {
		LOG_ERR("Invalid max_timeseries: %u (must be 1-256)", max_timeseries);
		return NULL;
	}

	if (max_labels > CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC) {
		LOG_ERR("Invalid max_labels: %u (max %d)", max_labels,
			CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC);
		return NULL;
	}

	/* Normalize metric name */
	char normalized_name[256];
	normalize_metric_name(name, normalized_name, sizeof(normalized_name));

	if (strlen(normalized_name) == 0) {
		LOG_ERR("Metric name '%s' normalizes to empty string", name);
		return NULL;
	}

	k_mutex_lock(&g_registry_lock, K_FOREVER);

	/* Check for duplicate metric name */
	struct spotflow_metric_base *existing = find_metric_by_name(normalized_name);
	if (existing != NULL) {
		LOG_ERR("Metric '%s' already registered", normalized_name);
		k_mutex_unlock(&g_registry_lock);
		return NULL;
	}

	/* Find available slot */
	int slot = find_available_slot();
	if (slot < 0) {
		LOG_ERR("Metric registry full (%d/%d)", CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED,
			CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED);
		k_mutex_unlock(&g_registry_lock);
		return NULL;
	}

	struct spotflow_metric_base *metric = &g_metric_registry[slot].int_metric.base;

	/* Initialize metric structure */
	strncpy(metric->name, normalized_name, sizeof(metric->name) - 1);
	metric->name[sizeof(metric->name) - 1] = '\0';

	metric->type = type;
	metric->agg_interval = agg_interval;
	metric->max_timeseries = max_timeseries;
	metric->max_labels = max_labels;
	metric->sequence_number = 0;
	metric->transmitted_messages = 0;

	k_mutex_init(&metric->lock);

	/* Initialize aggregator context */
	int rc = aggregator_register_metric(metric);
	if (rc < 0) {
		LOG_ERR("Failed to initialize aggregator for metric '%s': %d", normalized_name, rc);
		/* Rollback: mark registry slot as available if aggregator registration fails */
		metric->aggregator_context = NULL;
		k_mutex_unlock(&g_registry_lock);
		return NULL; /* Metric NOT registered, error code propagated */
	}

	k_mutex_unlock(&g_registry_lock);

	LOG_INF("Registered metric '%s' (type=%s, agg=%d, max_ts=%u, max_labels=%u)",
		normalized_name, (type == SPOTFLOW_METRIC_TYPE_INT) ? "int" : "float",
		metric->agg_interval, max_timeseries, max_labels);

	return metric;
}
