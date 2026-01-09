/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_aggregator.h"
#include "spotflow_metrics_cbor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <limits.h>
#include <float.h>

LOG_MODULE_REGISTER(spotflow_metrics_agg, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* External message queue (defined in spotflow_metrics_net.c) */
extern struct k_msgq g_spotflow_metrics_msgq;

/**
 * @brief Compare label arrays for equality
 *
 * Direct string comparison is used instead of hashing since labels are
 * sufficiently small (max 8 labels, key max 16 chars, value max 32 chars).
 */
static bool labels_equal(
	const struct metric_timeseries_state *ts,
	const spotflow_label_t *labels,
	uint8_t label_count)
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

/**
 * @brief Get aggregation interval in milliseconds
 */
static int64_t get_interval_ms(spotflow_agg_interval_t interval)
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
		return 60 * 1000;  /* Default to 1 minute */
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
static int enqueue_metric_message(uint8_t *payload, size_t len)
{
	if (payload == NULL || len == 0) {
		return -EINVAL;
	}

	/* Allocate message structure */
	struct spotflow_mqtt_metrics_msg *msg = k_malloc(sizeof(*msg));
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
		return -ENOMEM;
	}

	LOG_DBG("Enqueued metric message (%zu bytes)", len);
	return 0;
}

/**
 * @brief Forward declaration of aggregation timer handler
 */
static void aggregation_timer_handler(struct k_work *work);

/**
 * @brief Flush time series (encode and enqueue message)
 *
 * MUST be called with metric->lock held.
 *
 * @param metric Metric base handle
 * @param ts Time series state to flush
 * @param timestamp_ms Device uptime when aggregation window closed
 */
static int flush_timeseries(struct spotflow_metric_base *metric, struct metric_timeseries_state *ts,
			    int64_t timestamp_ms)
{
	uint8_t *cbor_data = NULL;
	size_t cbor_len = 0;

	/* Get and increment sequence number while holding mutex */
	uint64_t seq_num = metric->sequence_number++;

	/* Encode to CBOR */
	int rc = spotflow_metrics_cbor_encode(metric, ts, timestamp_ms, seq_num,
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

	/* Increment message counter */
	metric->transmitted_messages++;

	/* Reset time series state for next aggregation window */
	ts->count = 0;
	ts->sum_truncated = false;

	if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		ts->sum_int = 0;
		ts->min_int = INT64_MAX;
		ts->max_int = INT64_MIN;
	} else {
		ts->sum_float = 0.0;
		ts->min_float = DBL_MAX;
		ts->max_float = -DBL_MAX;
	}

	return 0;
}

/**
 * @brief Aggregation timer expiration handler
 *
 * Called when aggregation window closes. Flushes all active time series.
 */
static void aggregation_timer_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct metric_aggregator_context *ctx =
		CONTAINER_OF(dwork, struct metric_aggregator_context, aggregation_work);

	struct spotflow_metric_base *metric = ctx->metric;

	/* Capture timestamp when aggregation window closes */
	int64_t timestamp_ms = k_uptime_get();
	LOG_DBG("Aggregation window closed for metric '%s' at %lld ms",
		metric->name, timestamp_ms);

	k_mutex_lock(&metric->lock, K_FOREVER);

	LOG_DBG("Aggregation timer expired for metric '%s' (%u active time series)",
		metric->name, ctx->timeseries_count);

	/* Flush all active time series with the same timestamp */
	for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
		struct metric_timeseries_state *ts = &ctx->timeseries[i];

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
 * @brief Find or create time series slot
 *
 * Uses direct string comparison for label matching. O(n) linear search
 * is acceptable since max_timeseries ≤ 256 and labels are small.
 */
static struct metric_timeseries_state *find_or_create_timeseries(
	struct metric_aggregator_context *ctx,
	const spotflow_label_t *labels,
	uint8_t label_count)
{
	/* First, try to find existing time series with matching labels */
	for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
		if (ctx->timeseries[i].active &&
		    labels_equal(&ctx->timeseries[i], labels, label_count)) {
			return &ctx->timeseries[i];
		}
	}

	/* Not found - create new time series if space available */
	if (ctx->timeseries_count >= ctx->timeseries_capacity) {
		return NULL;  /* Pool full */
	}

	/* Find first inactive slot */
	for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
		if (!ctx->timeseries[i].active) {
			struct metric_timeseries_state *ts = &ctx->timeseries[i];

			/* Initialize new time series */
			memset(ts, 0, sizeof(*ts));
			ts->active = true;
			ts->label_count = label_count;

			/* Copy labels with validation */
			for (uint8_t j = 0; j < label_count; j++) {
				/* Validate and copy key */
				if (!labels[j].key) {
					LOG_ERR("Label key is NULL");
					ts->active = false;
					return NULL;
				}
				size_t key_len = strlen(labels[j].key);
				if (key_len >= SPOTFLOW_MAX_LABEL_KEY_LEN) {
					LOG_ERR("Label key too long: %zu chars (max %d)",
						key_len, SPOTFLOW_MAX_LABEL_KEY_LEN - 1);
					ts->active = false;
					return NULL;
				}
				strncpy(ts->labels[j].key, labels[j].key,
					SPOTFLOW_MAX_LABEL_KEY_LEN - 1);
				ts->labels[j].key[SPOTFLOW_MAX_LABEL_KEY_LEN - 1] = '\0';

				/* Validate and copy value */
				if (!labels[j].value) {
					LOG_ERR("Label value is NULL");
					ts->active = false;
					return NULL;
				}
				size_t value_len = strlen(labels[j].value);
				if (value_len >= SPOTFLOW_MAX_LABEL_VALUE_LEN) {
					LOG_ERR("Label value too long: %zu chars (max %d)",
						value_len, SPOTFLOW_MAX_LABEL_VALUE_LEN - 1);
					ts->active = false;
					return NULL;
				}
				strncpy(ts->labels[j].value, labels[j].value,
					SPOTFLOW_MAX_LABEL_VALUE_LEN - 1);
				ts->labels[j].value[SPOTFLOW_MAX_LABEL_VALUE_LEN - 1] = '\0';
			}

			/* Initialize aggregation state */
			if (ctx->metric->type == SPOTFLOW_METRIC_TYPE_INT) {
				ts->min_int = INT64_MAX;
				ts->max_int = INT64_MIN;
			} else {
				ts->min_float = DBL_MAX;
				ts->max_float = -DBL_MAX;
			}

			ctx->timeseries_count++;

			LOG_DBG("Created new time series for metric '%s' (total=%u/%u)",
				ctx->metric->name, ctx->timeseries_count,
				ctx->timeseries_capacity);

			return ts;
		}
	}

	return NULL;  /* Should not reach here */
}

/**
 * @brief Update integer aggregation state
 */
static void update_aggregation_int(struct metric_timeseries_state *ts, int64_t value)
{
	ts->count++;

	/* Sum with overflow detection */
	int64_t old_sum = ts->sum_int;
	ts->sum_int += value;

	/* Check for overflow */
	if ((value > 0 && ts->sum_int < old_sum) ||
	    (value < 0 && ts->sum_int > old_sum)) {
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
static void update_aggregation_float(struct metric_timeseries_state *ts, double value)
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

/* Public API Implementation */

int aggregator_register_metric(struct spotflow_metric_base *metric)
{
	/* Contract: This function MUST be atomic - either full success (returns 0) */
	/* or full failure (returns -ENOMEM with no side effects). Caller relies on */
	/* this to safely rollback metric registration on failure. */

	/* Allocate aggregator context */
	struct metric_aggregator_context *ctx = k_malloc(sizeof(*ctx));
	if (!ctx) {
		return -ENOMEM;
	}

	/* Allocate time series array */
	ctx->timeseries = k_calloc(metric->max_timeseries, sizeof(struct metric_timeseries_state));
	if (!ctx->timeseries) {
		k_free(ctx);  /* Clean up - maintain atomic semantics */
		return -ENOMEM;
	}

	ctx->metric = metric;
	ctx->timeseries_count = 0;
	ctx->timeseries_capacity = metric->max_timeseries;
	ctx->timer_started = false;

	/* Always initialize aggregation timer work structure (even for PT0S) */
	/* to prevent NULL pointer dereference if work is accidentally accessed */
	k_work_init_delayable(&ctx->aggregation_work, aggregation_timer_handler);

	metric->aggregator_context = ctx;

	LOG_DBG("Registered aggregator for metric '%s' (max_ts=%u)",
		metric->name, metric->max_timeseries);

	return 0;
}

int aggregator_report_value(
	struct spotflow_metric_base *metric,
	const spotflow_label_t *labels,
	uint8_t label_count,
	int64_t value_int,
	double value_float)
{
	if (metric == NULL || metric->aggregator_context == NULL) {
		return -EINVAL;
	}

	struct metric_aggregator_context *ctx = metric->aggregator_context;

	k_mutex_lock(&metric->lock, K_FOREVER);

	/* Find or create time series */
	struct metric_timeseries_state *ts =
		find_or_create_timeseries(ctx, labels, label_count);

	if (ts == NULL) {
		k_mutex_unlock(&metric->lock);
		LOG_WRN("Time series pool full for metric '%s' (%u/%u)",
			metric->name, ctx->timeseries_count, ctx->timeseries_capacity);
		return -ENOSPC;
	}

	/* Start aggregation timer on first report (sliding window) */
	/* Use per-metric flag to prevent race condition with labeled metrics */
	if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE && !ctx->timer_started) {
		int64_t interval_ms = get_interval_ms(metric->agg_interval);
		if (interval_ms > 0) {
			k_work_schedule(&ctx->aggregation_work, K_MSEC(interval_ms));
			ctx->timer_started = true;
			LOG_DBG("Started aggregation timer for metric '%s' (interval=%lld ms)",
				metric->name, interval_ms);
		}
	}

	/* Update aggregation state */
	if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		update_aggregation_int(ts, value_int);
	} else {
		update_aggregation_float(ts, value_float);
	}

	/* For PT0S (no aggregation), flush immediately with current timestamp */
	if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
		int rc = flush_timeseries(metric, ts, k_uptime_get());
		k_mutex_unlock(&metric->lock);
		return rc;
	}

	k_mutex_unlock(&metric->lock);
	return 0;
}

void aggregator_unregister_metric(struct spotflow_metric_base *metric)
{
	if (metric == NULL || metric->aggregator_context == NULL) {
		return;
	}

	struct metric_aggregator_context *ctx = metric->aggregator_context;

	k_mutex_lock(&metric->lock, K_FOREVER);

	/* Cancel pending timer */
	if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
		k_work_cancel_delayable(&ctx->aggregation_work);
	}

	/* Free time series array */
	k_free(ctx->timeseries);

	/* Free context */
	k_free(ctx);

	metric->aggregator_context = NULL;

	k_mutex_unlock(&metric->lock);

	LOG_DBG("Unregistered aggregator for metric '%s'", metric->name);
}
