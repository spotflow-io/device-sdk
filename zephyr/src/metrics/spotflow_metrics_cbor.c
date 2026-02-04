/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_cbor.h"

#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics_cbor, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* CBOR Protocol Keys (aligned with cloud backend) */
#define KEY_MESSAGE_TYPE 0x00 /* 0 */
#define KEY_LABELS 0x05 /* 5 */
#define KEY_DEVICE_UPTIME_MS 0x06 /* 6 */
#define KEY_SEQUENCE_NUMBER 0x0D /* 13 */
#define KEY_METRIC_NAME 0x15 /* 21 */
#define KEY_AGGREGATION_INTERVAL 0x16 /* 22 */
#define KEY_SUM 0x18 /* 24 */
#define KEY_SUM_TRUNCATED 0x19 /* 25 */
#define KEY_COUNT 0x1A /* 26 */
#define KEY_MIN 0x1B /* 27 */
#define KEY_MAX 0x1C /* 28 */
#define KEY_SAMPLES 0x1D /* 29 - reserved for future */

/* Message Type */
#define METRIC_MESSAGE_TYPE 0x05
static bool encode_labels(zcbor_state_t* state, const struct metric_label_storage* labels,
			  uint8_t label_count);
static void encode_metric_header(struct spotflow_metric_base* metric, int64_t timestamp_ms,
				 uint64_t sequence_number, zcbor_state_t state[3], bool* succ);
static bool encode_aggregation_stats(zcbor_state_t* state, struct spotflow_metric_base* metric,
				     struct metric_timeseries_state* ts);
static int finalize_cbor_output(uint8_t* buffer, zcbor_state_t* state, uint8_t** cbor_data,
				size_t* cbor_len);

int spotflow_metrics_cbor_encode(struct spotflow_metric_base* metric,
				 struct metric_timeseries_state* ts, int64_t timestamp_ms,
				 uint64_t sequence_number, uint8_t** cbor_data, size_t* cbor_len)
{
	if (metric == NULL || ts == NULL || cbor_data == NULL || cbor_len == NULL) {
		return -EINVAL;
	}

	if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
		LOG_ERR("This function should not be used for non-aggregated metrics");
		return -EINVAL; /* This function is for non-aggregated metrics only */
	}

	uint8_t* buffer = k_malloc(CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE);
	if (!buffer) {
		LOG_ERR("Failed to allocate CBOR encoding buffer");
		return -ENOMEM;
	}
	ZCBOR_STATE_E(state, 1, buffer, CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE, 1);

	bool succ = true;

	/* Calculate actual map entry count (dynamic map size) */
	/* Base entries: messageType, metricName, aggregationInterval, deviceUptimeMs,
	 *               sequenceNumber, sum, count, min, max = 9 */
	uint32_t map_entries = 9;
	if (ts->label_count > 0) {
		map_entries++; /* labels */
	}
	if (ts->sum_truncated) {
		map_entries++; /* sumTruncated */
	}

	/* Start CBOR map with exact entry count */
	succ = succ && zcbor_map_start_encode(state, map_entries);

	encode_metric_header(metric, timestamp_ms, sequence_number, state, &succ);

	/* labels (if labeled metric) */
	if (ts->label_count > 0) {
		succ = succ && encode_labels(state, ts->labels, ts->label_count);
	}

	/* Encode aggregation stats: sum, sumTruncated, count, min, max */
	succ = succ && encode_aggregation_stats(state, metric, ts);

	/* End CBOR map */
	succ = succ && zcbor_map_end_encode(state, map_entries);

	if (!succ) {
		LOG_ERR("CBOR encoding failed: %d", zcbor_peek_error(state));
		k_free(buffer);
		return -EINVAL;
	}

	int ret = finalize_cbor_output(buffer, state, cbor_data, cbor_len);
	if (ret != 0) {
		return ret;
	}

	LOG_DBG("Encoded metric '%s' message (%zu bytes, seq=%llu)", metric->name, *cbor_len,
		(unsigned long long)sequence_number);

	return 0;
}

int spotflow_metrics_cbor_encode_no_aggregation(struct spotflow_metric_base* metric,
						const struct spotflow_label* labels,
						uint8_t label_count, int64_t value_int,
						float value_float, int64_t timestamp_ms,
						uint64_t sequence_number, uint8_t** cbor_data,
						size_t* cbor_len)
{
	if (metric == NULL || cbor_data == NULL || cbor_len == NULL) {
		return -EINVAL;
	}

	if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
		LOG_ERR("This function should not be used for aggregated metrics");
		return -EINVAL; /* This function is for non-aggregated metrics only */
	}

	uint8_t* buffer = k_malloc(CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE);
	if (!buffer) {
		LOG_ERR("Failed to allocate CBOR encoding buffer");
		return -ENOMEM;
	}
	ZCBOR_STATE_E(state, 1, buffer, CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE, 1);

	bool succ = true;

	/* Calculate map entry count */
	/* Base: messageType, metricName, aggregationInterval, deviceUptimeMs,
	 *       sequenceNumber, sum = 6 */
	uint32_t map_entries = 6;
	if (label_count > 0) {
		map_entries++; /* labels */
	}

	/* Start CBOR map */
	succ = succ && zcbor_map_start_encode(state, map_entries);

	encode_metric_header(metric, timestamp_ms, sequence_number, state, &succ);

	/* labels (if present) */
	if (label_count > 0) {
		succ = succ && zcbor_uint32_put(state, KEY_LABELS);
		succ = succ && zcbor_map_start_encode(state, label_count);

		for (uint8_t i = 0; i < label_count && succ; i++) {
			succ = succ &&
			    zcbor_tstr_put_term(state, labels[i].key, SPOTFLOW_MAX_LABEL_KEY_LEN);
			succ = succ &&
			    zcbor_tstr_put_term(state, labels[i].value,
						SPOTFLOW_MAX_LABEL_VALUE_LEN);
		}

		succ = succ && zcbor_map_end_encode(state, label_count);
	}

	/* value (single data point) */
	succ = succ && zcbor_uint32_put(state, KEY_SUM);
	if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
		succ = succ && zcbor_float64_put(state, value_float);
	} else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		succ = succ && zcbor_int64_put(state, value_int);
	} else {
		LOG_ERR("Invalid metric type: %d", metric->type);
		k_free(buffer);
		return -EINVAL;
	}

	/* End CBOR map */
	succ = succ && zcbor_map_end_encode(state, map_entries);

	if (!succ) {
		LOG_ERR("CBOR encoding failed for raw metric: %d", zcbor_peek_error(state));
		k_free(buffer);
		return -EINVAL;
	}

	int ret = finalize_cbor_output(buffer, state, cbor_data, cbor_len);
	if (ret != 0) {
		return ret;
	}

	LOG_DBG("Encoded raw metric '%s' message (%zu bytes, seq=%llu)", metric->name, *cbor_len,
		(unsigned long long)sequence_number);

	return 0;
}

int spotflow_metrics_cbor_encode_heartbeat(int64_t uptime_ms, uint8_t** data, size_t* len)
{
	uint8_t buffer[64]; /* Small static buffer for ~40 bytes output */
	ZCBOR_STATE_E(state, 1, buffer, sizeof(buffer), 1);

	bool succ = true;

	/* Start CBOR map with 4 entries */
	succ = succ && zcbor_map_start_encode(state, 4);

	/* messageType = 5 */
	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, METRIC_MESSAGE_TYPE);

	/* metricName = "uptime_ms" */
	succ = succ && zcbor_uint32_put(state, KEY_METRIC_NAME);
	succ = succ && zcbor_tstr_put_lit(state, "uptime_ms");

	/* deviceUptimeMs */
	succ = succ && zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
	succ = succ && zcbor_int64_put(state, uptime_ms);

	/* sum = uptime value */
	succ = succ && zcbor_uint32_put(state, KEY_SUM);
	succ = succ && zcbor_int64_put(state, uptime_ms);

	succ = succ && zcbor_map_end_encode(state, 4);

	if (!succ) {
		LOG_ERR("Heartbeat CBOR encoding failed");
		return -EINVAL;
	}

	/* Allocate and copy */
	size_t encoded_len = state->payload - buffer;
	*data = k_malloc(encoded_len);
	if (!*data) {
		LOG_ERR("Failed to allocate heartbeat payload");
		return -ENOMEM;
	}

	memcpy(*data, buffer, encoded_len);
	*len = encoded_len;

	return 0;
}

/**
 * @brief Encode labels as CBOR map
 */
static bool encode_labels(zcbor_state_t* state, const struct metric_label_storage* labels,
			  uint8_t label_count)
{
	bool succ = true;

	succ = succ && zcbor_uint32_put(state, KEY_LABELS);
	succ = succ && zcbor_map_start_encode(state, label_count);

	for (uint8_t i = 0; i < label_count && succ; i++) {
		succ =
		    succ && zcbor_tstr_put_term(state, labels[i].key, SPOTFLOW_MAX_LABEL_KEY_LEN);
		succ = succ &&
		    zcbor_tstr_put_term(state, labels[i].value, SPOTFLOW_MAX_LABEL_VALUE_LEN);
	}

	succ = succ && zcbor_map_end_encode(state, label_count);

	return succ;
}

static void encode_metric_header(struct spotflow_metric_base* metric, int64_t timestamp_ms,
				 uint64_t sequence_number, zcbor_state_t state[3], bool* succ)
{
	/* messageType */
	*succ = *succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	*succ = *succ && zcbor_uint32_put(state, METRIC_MESSAGE_TYPE);

	/* metricName */
	*succ = *succ && zcbor_uint32_put(state, KEY_METRIC_NAME);
	*succ = *succ && zcbor_tstr_put_term(state, metric->name, sizeof(metric->name));

	/* aggregationInterval */
	*succ = *succ && zcbor_uint32_put(state, KEY_AGGREGATION_INTERVAL);
	*succ = *succ && zcbor_uint32_put(state, metric->agg_interval);

	/* deviceUptimeMs - 64-bit signed integer per cloud documentation */
	/* Timestamp is captured by aggregator when window closes, not at encoding time */
	*succ = *succ && zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
	*succ = *succ && zcbor_int64_put(state, timestamp_ms);

	/* sequenceNumber (per-metric, passed by caller who increments under mutex) */
	*succ = *succ && zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER);
	*succ = *succ && zcbor_uint64_put(state, sequence_number);
}

static bool encode_aggregation_stats(zcbor_state_t* state, struct spotflow_metric_base* metric,
				     struct metric_timeseries_state* ts)
{
	bool succ = true;

	/* sum */
	succ = succ && zcbor_uint32_put(state, KEY_SUM);
	if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
		succ = succ && zcbor_float64_put(state, ts->sum_float);
	} else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		succ = succ && zcbor_int64_put(state, ts->sum_int);
	} else {
		LOG_ERR("Invalid metric type: %d", metric->type);
		return false;
	}

	/* sumTruncated (only if true) */
	if (ts->sum_truncated) {
		succ = succ && zcbor_uint32_put(state, KEY_SUM_TRUNCATED);
		succ = succ && zcbor_bool_put(state, true);
	}

	/* count */
	succ = succ && zcbor_uint32_put(state, KEY_COUNT);
	succ = succ && zcbor_uint64_put(state, ts->count);

	/* min */
	succ = succ && zcbor_uint32_put(state, KEY_MIN);
	if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
		succ = succ && zcbor_float64_put(state, ts->min_float);
	} else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		succ = succ && zcbor_int64_put(state, ts->min_int);
	} else {
		LOG_ERR("Invalid metric type: %d", metric->type);
		return false;
	}

	/* max */
	succ = succ && zcbor_uint32_put(state, KEY_MAX);
	if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
		succ = succ && zcbor_float64_put(state, ts->max_float);
	} else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
		succ = succ && zcbor_int64_put(state, ts->max_int);
	} else {
		LOG_ERR("Invalid metric type: %d", metric->type);
		return false;
	}

	return succ;
}

static int finalize_cbor_output(uint8_t* buffer, zcbor_state_t* state, uint8_t** cbor_data,
				size_t* cbor_len)
{
	size_t encoded_len = state->payload - buffer;
	uint8_t* data = k_malloc(encoded_len);
	if (!data) {
		k_free(buffer);
		return -ENOMEM;
	}

	memcpy(data, buffer, encoded_len);
	k_free(buffer);
	*cbor_data = data;
	*cbor_len = encoded_len;

	return 0;
}
