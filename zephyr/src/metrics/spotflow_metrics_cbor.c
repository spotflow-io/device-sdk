#include "metrics/spotflow_metrics_cbor.h"

#include <string.h>
#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define ZCBOR_STATE_DEPTH 2

static int encode_common(const char* metric_name,
			 const struct spotflow_metric_dimension* dimensions,
			 size_t dimensions_count, const char* aggregation_interval,
			 uint64_t sequence_number, uint8_t* buffer, size_t buffer_len,
			 zcbor_state_t* state)
{
	bool succ;

	zcbor_new_encode_state(state, ZCBOR_STATE_DEPTH, buffer, buffer_len, 1);

	succ = zcbor_map_start_encode(state, 10);

	succ = succ && zcbor_tstr_put_lit(state, "messageType");
	succ = succ && zcbor_tstr_put_lit(state, "METRIC");

	succ = succ && zcbor_tstr_put_lit(state, "metricName");
	succ = succ && zcbor_tstr_put_term(state, metric_name, SIZE_MAX);

	if (dimensions_count > 0) {
		succ = succ && zcbor_tstr_put_lit(state, "labels");
		succ = succ && zcbor_map_start_encode(state, dimensions_count);
		for (size_t i = 0; i < dimensions_count; i++) {
			succ = succ && zcbor_tstr_put_term(state, dimensions[i].key, SIZE_MAX);
			succ = succ && zcbor_tstr_put_term(state, dimensions[i].value, SIZE_MAX);
		}
		succ = succ && zcbor_map_end_encode(state, dimensions_count);
	}

	if (aggregation_interval != NULL) {
		succ = succ && zcbor_tstr_put_lit(state, "aggregationInterval");
		succ = succ && zcbor_tstr_put_term(state, aggregation_interval, SIZE_MAX);
	}

	succ = succ && zcbor_tstr_put_lit(state, "deviceUptimeMs");
	succ = succ && zcbor_uint64_put(state, k_uptime_get());

	succ = succ && zcbor_tstr_put_lit(state, "sequenceNumber");
	succ = succ && zcbor_uint64_put(state, sequence_number);

	if (!succ) {
		LOG_DBG("Failed to encode common metric fields: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	return 0;
}

static int finish_encode(zcbor_state_t* state, uint8_t* buffer, uint8_t** cbor_data,
			 size_t* cbor_len)
{
	bool succ = zcbor_map_end_encode(state, 10);
	if (!succ) {
		LOG_DBG("Failed to finish metrics CBOR map: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	*cbor_len = state->payload - buffer;

	uint8_t* data = k_malloc(*cbor_len);
	if (!data) {
		LOG_DBG("Failed to allocate metrics CBOR payload");
		return -ENOMEM;
	}

	memcpy(data, state->payload_bak, *cbor_len);
	*cbor_data = data;
	return 0;
}

int spotflow_metrics_cbor_encode_int(const char* metric_name,
				     const struct spotflow_metric_dimension* dimensions,
				     size_t dimensions_count,
				     const struct spotflow_metric_stats_int* stats,
				     const int64_t* samples, size_t samples_count,
				     const char* aggregation_interval, uint64_t sequence_number,
				     uint8_t** cbor_data, size_t* cbor_len)
{
	uint8_t buffer[CONFIG_SPOTFLOW_CBOR_METRIC_MAX_LEN];
	zcbor_state_t state[ZCBOR_STATE_DEPTH];

	int rc = encode_common(metric_name, dimensions, dimensions_count, aggregation_interval,
			       sequence_number, buffer, sizeof(buffer), state);
	if (rc < 0) {
		return rc;
	}

	bool succ = true;
	succ = succ && zcbor_tstr_put_lit(state, "sum");
	succ = succ && zcbor_int64_put(state, stats->sum);

	succ = succ && zcbor_tstr_put_lit(state, "count");
	succ = succ && zcbor_uint64_put(state, stats->count);

	succ = succ && zcbor_tstr_put_lit(state, "min");
	succ = succ && zcbor_int64_put(state, stats->min);

	succ = succ && zcbor_tstr_put_lit(state, "max");
	succ = succ && zcbor_int64_put(state, stats->max);

	if (samples != NULL && samples_count > 0) {
		succ = succ && zcbor_tstr_put_lit(state, "samples");
		succ = succ && zcbor_list_start_encode(state, samples_count);
		for (size_t i = 0; i < samples_count; i++) {
			succ = succ && zcbor_int64_put(state, samples[i]);
		}
		succ = succ && zcbor_list_end_encode(state, samples_count);
	}

	if (!succ) {
		LOG_DBG("Failed to encode int metric body: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	return finish_encode(state, buffer, cbor_data, cbor_len);
}

int spotflow_metrics_cbor_encode_float(const char* metric_name,
				       const struct spotflow_metric_dimension* dimensions,
				       size_t dimensions_count,
				       const struct spotflow_metric_stats_float* stats,
				       const double* samples, size_t samples_count,
				       const char* aggregation_interval, uint64_t sequence_number,
				       uint8_t** cbor_data, size_t* cbor_len)
{
	uint8_t buffer[CONFIG_SPOTFLOW_CBOR_METRIC_MAX_LEN];
	zcbor_state_t state[ZCBOR_STATE_DEPTH];

	int rc = encode_common(metric_name, dimensions, dimensions_count, aggregation_interval,
			       sequence_number, buffer, sizeof(buffer), state);
	if (rc < 0) {
		return rc;
	}

	bool succ = true;
	succ = succ && zcbor_tstr_put_lit(state, "sum");
	succ = succ && zcbor_float64_put(state, stats->sum);

	succ = succ && zcbor_tstr_put_lit(state, "count");
	succ = succ && zcbor_uint64_put(state, stats->count);

	succ = succ && zcbor_tstr_put_lit(state, "min");
	succ = succ && zcbor_float64_put(state, stats->min);

	succ = succ && zcbor_tstr_put_lit(state, "max");
	succ = succ && zcbor_float64_put(state, stats->max);

	if (samples != NULL && samples_count > 0) {
		succ = succ && zcbor_tstr_put_lit(state, "samples");
		succ = succ && zcbor_list_start_encode(state, samples_count);
		for (size_t i = 0; i < samples_count; i++) {
			succ = succ && zcbor_float64_put(state, samples[i]);
		}
		succ = succ && zcbor_list_end_encode(state, samples_count);
	}

	if (!succ) {
		LOG_DBG("Failed to encode float metric body: %d", zcbor_peek_error(state));
		return -EINVAL;
	}

	return finish_encode(state, buffer, cbor_data, cbor_len);
}
