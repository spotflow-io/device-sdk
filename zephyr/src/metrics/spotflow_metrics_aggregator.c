/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_aggregator.h"
#include "spotflow_metrics_cbor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <limits.h>
#include <float.h>

LOG_MODULE_REGISTER(spotflow_metrics_agg, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* External message queue (defined in spotflow_metrics_net.c) */
extern struct k_msgq g_spotflow_metrics_msgq;

static bool labels_equal(const struct metric_timeseries_state* ts,
			 const struct spotflow_label* labels, uint8_t label_count);
static void update_aggregation_int(struct metric_timeseries_state* ts, int64_t value);
static void update_aggregation_float(struct metric_timeseries_state* ts, float value);
static int64_t get_interval_ms(enum spotflow_agg_interval interval);
static int enqueue_metric_message(uint8_t* payload, size_t len);
static struct metric_timeseries_state*
find_or_create_timeseries(struct metric_aggregator_context* ctx,
			  const struct spotflow_label* labels, uint8_t label_count);
static int flush_timeseries(struct spotflow_metric_base* metric, struct metric_timeseries_state* ts,
			    int64_t timestamp_ms);
static void reset_timeseries_state(struct spotflow_metric_base* metric,
				   struct metric_timeseries_state* ts);
static int flush_no_aggregation_metric(struct spotflow_metric_base* metric,
				       const struct spotflow_label* labels, uint8_t label_count,
				       int64_t value_int, float value_float);
static void aggregation_timer_handler(struct k_work* work);

/* Public API Implementation */

int aggregator_register_metric(struct spotflow_metric_base* metric)
{
	/* Contract: This function MUST be atomic - either full success (returns 0) */
	/* or full failure (returns -ENOMEM with no side effects). Caller relies on */
	/* this to safely rollback metric registration on failure. */

	/* Allocate aggregator context */
	struct metric_aggregator_context* ctx = k_malloc(sizeof(*ctx));
	if (!ctx) {
		return -ENOMEM;
	}

	/* Allocate time series array */
	ctx->timeseries = k_calloc(metric->max_timeseries, sizeof(struct metric_timeseries_state));
	if (!ctx->timeseries) {
		k_free(ctx); /* Clean up - maintain atomic semantics */
		return -ENOMEM;
	}

	ctx->metric = metric;
	ctx->timeseries_count = 0;
	ctx->timeseries_capacity = metric->max_timeseries;
	ctx->timer_started = false;

	if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
		k_work_init_delayable(&ctx->aggregation_work, aggregation_timer_handler);
	}

	metric->aggregator_context = ctx;

	LOG_DBG("Registered aggregator for metric '%s' (max_ts=%u)", metric->name,
		metric->max_timeseries);

	return 0;
}

int aggregator_report_value(struct spotflow_metric_base* metric,
			    const struct spotflow_label* labels, uint8_t label_count,
			    int64_t value_int, float value_float)
{
	if (metric == NULL || metric->aggregator_context == NULL) {
		return -EINVAL;
	}

	struct metric_aggregator_context* ctx = metric->aggregator_context;

	k_mutex_lock(&metric->lock, K_FOREVER);

	if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
		int rc = flush_no_aggregation_metric(metric, labels, label_count, value_int,
						     value_float);
		k_mutex_unlock(&metric->lock);
		return rc;
	}

	/* Find or create time series */
	struct metric_timeseries_state* ts = find_or_create_timeseries(ctx, labels, label_count);

	if (ts == NULL) {
		k_mutex_unlock(&metric->lock);
		LOG_WRN("Time series pool full for metric '%s' (%u/%u)", metric->name,
			ctx->timeseries_count, ctx->timeseries_capacity);
		return -ENOSPC;
	}

	/* Update aggregation state */
	if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		update_aggregation_int(ts, value_int);
	} else if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
		update_aggregation_float(ts, value_float);
	} else {
		k_mutex_unlock(&metric->lock);
		LOG_ERR("Invalid metric type: %d", metric->type);
		return -EINVAL;
	}

	/* Start aggregation timer on first report (sliding window) */
	/* Use per-metric flag to prevent race condition with labeled metrics */
	if (!ctx->timer_started) {
		int64_t interval_ms = get_interval_ms(metric->agg_interval);
		if (interval_ms > 0) {
			/* Add 0-10% jitter to first flush to spread out across metrics */
			int32_t jitter_ms = sys_rand32_get() % (interval_ms / 10);
			k_work_schedule(&ctx->aggregation_work, K_MSEC(interval_ms - jitter_ms));
			ctx->timer_started = true;
			LOG_DBG("Started aggregation timer for metric '%s' (interval=%lld ms, "
				"jitter=-%d ms)",
				metric->name, interval_ms, jitter_ms);
		}
	}

	k_mutex_unlock(&metric->lock);
	return 0;
}

/**
 * @brief Compare label arrays for equality
 *
 * Direct string comparison is used instead of hashing since labels are
 * sufficiently small (max 8 labels, key max 16 chars, value max 32 chars).
 */
static bool labels_equal(const struct metric_timeseries_state* ts,
			 const struct spotflow_label* labels, uint8_t label_count)
{
	if (ts->label_count != label_count) {
		return false;
	}

	for (uint8_t i = 0; i < label_count; i++) {
		if (strcmp(ts->labels[i].key, labels[i].key) != 0 ||
		    strcmp(ts->labels[i].value, labels[i].value) != 0) {
			return false;
		}
	}

	return true;
}

static int flush_no_aggregation_metric(struct spotflow_metric_base* metric,
				       const struct spotflow_label* labels, uint8_t label_count,
				       int64_t value_int, float value_float)
{
	uint8_t* cbor_data = NULL;
	size_t cbor_len = 0;

	/* Get and increment sequence number while holding mutex */
	uint64_t seq_num = metric->sequence_number++;

	/* Encode to CBOR */
	int rc = spotflow_metrics_cbor_encode_no_aggregation(metric, labels, label_count, value_int,
							     value_float, k_uptime_get(), seq_num,
							     &cbor_data, &cbor_len);
	if (rc < 0) {
		LOG_ERR("Failed to encode metric '%s': %d", metric->name, rc);
		return rc;
	}

	/* Enqueue message */
	rc = enqueue_metric_message(cbor_data, cbor_len);
	if (rc < 0) {
		/* enqueue_metric_message does NOT free payload on failure */
		/* We must free it here */
		k_free(cbor_data);
		LOG_WRN("Failed to enqueue metric '%s': %d", metric->name, rc);
		return rc;
	}

	/* Ownership transferred to queue - processor will free */
	return 0;
}

/**
 * @brief Reset time series state for next aggregation window
 *
 * @param metric Metric base handle (for type information)
 * @param ts Time series state to reset
 */
static void reset_timeseries_state(struct spotflow_metric_base* metric,
				   struct metric_timeseries_state* ts)
{
	ts->count = 0;
	ts->sum_truncated = false;

	if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		ts->sum_int = 0;
		ts->min_int = INT64_MAX;
		ts->max_int = INT64_MIN;
	} else {
		ts->sum_float = 0.0f;
		ts->min_float = FLT_MAX;
		ts->max_float = -FLT_MAX;
	}
}

/**
 * @brief Flush time series (encode and enqueue message)
 *
 * MUST be called with metric->lock held.
 *
 * Always resets timeseries state regardless of success/failure to maintain
 * correct aggregation window semantics.
 *
 * @param metric Metric base handle
 * @param ts Time series state to flush
 * @param timestamp_ms Device uptime when aggregation window closed
 */
static int flush_timeseries(struct spotflow_metric_base* metric, struct metric_timeseries_state* ts,
			    int64_t timestamp_ms)
{
	uint8_t* cbor_data = NULL;
	size_t cbor_len = 0;

	/* Get and increment sequence number while holding mutex */
	uint64_t seq_num = metric->sequence_number++;

	/* Encode to CBOR */
	int rc =
	    spotflow_metrics_cbor_encode(metric, ts, timestamp_ms, seq_num, &cbor_data, &cbor_len);
	if (rc < 0) {
		LOG_ERR("Failed to encode metric '%s': %d", metric->name, rc);
		reset_timeseries_state(metric, ts);
		return rc;
	}

	/* Enqueue message */
	rc = enqueue_metric_message(cbor_data, cbor_len);
	if (rc < 0) {
		/* enqueue_metric_message does NOT free payload on failure */
		/* We must free it here */
		k_free(cbor_data);
		LOG_WRN("Failed to enqueue metric '%s': %d", metric->name, rc);
		reset_timeseries_state(metric, ts);
		return rc;
	}

	/* Ownership transferred to queue - processor will free */

	reset_timeseries_state(metric, ts);
	return 0;
}

/**
 * @brief Aggregation timer expiration handler
 *
 * Called when aggregation window closes. Flushes all active time series.
 */
static void aggregation_timer_handler(struct k_work* work)
{
	struct k_work_delayable* dwork = k_work_delayable_from_work(work);
	struct metric_aggregator_context* ctx =
	    CONTAINER_OF(dwork, struct metric_aggregator_context, aggregation_work);

	struct spotflow_metric_base* metric = ctx->metric;

	/* Capture timestamp when aggregation window closes */
	int64_t timestamp_ms = k_uptime_get();
	LOG_DBG("Aggregation window closed for metric '%s' at %lld ms", metric->name, timestamp_ms);

	k_mutex_lock(&metric->lock, K_FOREVER);

	LOG_DBG("Aggregation timer expired for metric '%s' (%u active time series)", metric->name,
		ctx->timeseries_count);

	/* Flush all active time series with the same timestamp */
	for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
		struct metric_timeseries_state* ts = &ctx->timeseries[i];

		if (ts->active && ts->count > 0) {
			int rc = flush_timeseries(metric, ts, timestamp_ms);
			if (rc < 0) {
				LOG_ERR("Failed to flush time series for metric '%s': %d",
					metric->name, rc);
			}
		}
	}

	/* Reschedule timer for next aggregation window */
	int64_t interval_ms = get_interval_ms(metric->agg_interval);
	if (interval_ms > 0) {
		k_work_schedule(dwork, K_MSEC(interval_ms));
	}

	k_mutex_unlock(&metric->lock);
}

/**
 * @brief Copy labels to time series with bounds checking
 *
 * @param ts Time series state to copy labels into
 * @param labels Source labels array
 * @param label_count Number of labels to copy
 * @return 0 on success, -1 if any label key/value is NULL
 */
static int copy_labels_to_timeseries(struct metric_timeseries_state* ts,
				     const struct spotflow_label* labels, uint8_t label_count)
{
	for (uint8_t i = 0; i < label_count; i++) {
		if (!labels[i].key || !labels[i].value) {
			LOG_ERR("Label key or value is NULL at index %u", i);
			return -1;
		}
		/* No need to present warning - user was already informed about truncation
		 * in the validation phase of report metric function in metrics backend */
		strncpy(ts->labels[i].key, labels[i].key, SPOTFLOW_MAX_LABEL_KEY_LEN - 1);
		ts->labels[i].key[SPOTFLOW_MAX_LABEL_KEY_LEN - 1] = '\0';

		strncpy(ts->labels[i].value, labels[i].value, SPOTFLOW_MAX_LABEL_VALUE_LEN - 1);
		ts->labels[i].value[SPOTFLOW_MAX_LABEL_VALUE_LEN - 1] = '\0';
	}

	return 0;
}

/**
 * @brief Initialize aggregation state for time series
 *
 * Sets min/max to sentinel values based on metric type.
 *
 * @param ts Time series state to initialize
 * @param type Metric type (int or float)
 */
static void init_timeseries_aggregation_state(struct metric_timeseries_state* ts,
					      enum spotflow_metric_type type)
{
	if (type == SPOTFLOW_METRIC_TYPE_INT) {
		ts->min_int = INT64_MAX;
		ts->max_int = INT64_MIN;
	} else {
		ts->min_float = FLT_MAX;
		ts->max_float = -FLT_MAX;
	}
}

/**
 * @brief Find or create time series slot
 *
 * Uses direct string comparison for label matching. O(n) linear search
 * is acceptable since max_timeseries ≤ 256 and labels are small.
 *
 * When pool is full, attempts to evict a timeseries with count == 0
 * (no values reported in current aggregation window).
 */
static struct metric_timeseries_state*
find_or_create_timeseries(struct metric_aggregator_context* ctx,
			  const struct spotflow_label* labels, uint8_t label_count)
{
	struct metric_timeseries_state* inactive_slot = NULL;
	struct metric_timeseries_state* evictable_slot = NULL;

	/* Scan all slots: find matching, inactive, or evictable (count == 0) */
	for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
		struct metric_timeseries_state* ts = &ctx->timeseries[i];

		if (ts->active) {
			if (labels_equal(ts, labels, label_count)) {
				return ts; /* Found existing match */
			}
			if (ts->count == 0 && evictable_slot == NULL) {
				evictable_slot = ts; /* Candidate for eviction */
			}
		} else if (inactive_slot == NULL) {
			inactive_slot = ts; /* First inactive slot */
		}
	}

	/* Prefer inactive slot, fall back to evicting idle timeseries */
	struct metric_timeseries_state* ts = inactive_slot;
	if (ts == NULL) {
		ts = evictable_slot;
		if (ts != NULL) {
			LOG_DBG("Evicting idle timeseries for metric '%s'", ctx->metric->name);
		}
		/* Note: timeseries_count is NOT incremented for evicted slots
		 * since the slot was already counted as active. We only increment
		 * when using a previously inactive slot. */
	} else {
		ctx->timeseries_count++;
	}

	if (ts == NULL) {
		return NULL; /* Pool full, no evictable slots */
	}

	/* Initialize time series */
	memset(ts, 0, sizeof(*ts));
	ts->active = true;
	ts->label_count = label_count;

	if (copy_labels_to_timeseries(ts, labels, label_count) < 0) {
		ts->active = false;
		return NULL;
	}

	init_timeseries_aggregation_state(ts, ctx->metric->type);

	LOG_DBG("Initialized time series for metric '%s' (active=%u/%u)", ctx->metric->name,
		ctx->timeseries_count, ctx->timeseries_capacity);

	return ts;
}

/**
 * @brief Update integer aggregation state
 */
static void update_aggregation_int(struct metric_timeseries_state* ts, int64_t value)
{
	ts->count++;

	/* Sum with overflow detection */
	int64_t old_sum = ts->sum_int;
	ts->sum_int += value;

	/* Check for overflow */
	if ((value > 0 && ts->sum_int < old_sum) || (value < 0 && ts->sum_int > old_sum)) {
		ts->sum_truncated = true;
	}

	/* Min/Max */
	if (value < ts->min_int) {
		ts->min_int = value;
	}
	if (value > ts->max_int) {
		ts->max_int = value;
	}
}

/**
 * @brief Update float aggregation state
 */
static void update_aggregation_float(struct metric_timeseries_state* ts, float value)
{
	ts->count++;

	/* Sum */
	ts->sum_float += value;

	/* Min/Max */
	if (value < ts->min_float) {
		ts->min_float = value;
	}
	if (value > ts->max_float) {
		ts->max_float = value;
	}
}

/**
 * @brief Get aggregation interval in milliseconds
 */
static int64_t get_interval_ms(enum spotflow_agg_interval interval)
{
	switch (interval) {
	case SPOTFLOW_AGG_INTERVAL_NONE:
		return 0;
	case SPOTFLOW_AGG_INTERVAL_1MIN:
		return 60 * 1000;
	case SPOTFLOW_AGG_INTERVAL_10MIN:
		return 10 * 60 * 1000;
	case SPOTFLOW_AGG_INTERVAL_1HOUR:
		return 60 * 60 * 1000;
	default:
		return 60 * 1000; /* Default to 1 minute */
	}
}

/**
 * @brief Enqueue message to transmission queue
 *
 * Called by flush_timeseries() to enqueue encoded messages.
 *
 * Memory ownership:
 * - On success: ownership of payload transfers to queue (processor will free)
 * - On failure: caller retains ownership and must free payload
 */
static int enqueue_metric_message(uint8_t* payload, size_t len)
{
	if (payload == NULL || len == 0) {
		return -EINVAL;
	}

	/* Allocate message structure */
	struct spotflow_mqtt_metrics_msg* msg = k_malloc(sizeof(*msg));
	if (!msg) {
		return -ENOMEM;
	}

	msg->payload = payload;
	msg->len = len;

	/* Enqueue message (non-blocking) */
	int rc = k_msgq_put(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT);
	if (rc != 0) {
		/* Queue full - free message structure only, caller frees payload */
		k_free(msg);
		LOG_WRN("Metrics queue full, dropping message (%zu bytes)", len);
		return -ENOBUFS;
	}

	LOG_DBG("Enqueued metric message (%zu bytes)", len);
	return 0;
}
