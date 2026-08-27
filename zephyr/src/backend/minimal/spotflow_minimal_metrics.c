#include "spotflow_minimal_metrics.h"

#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_registry.h"
#include "net/spotflow_transport.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <string.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

#define KEY_MESSAGE_TYPE 0x00
#define KEY_LABELS 0x05
#define KEY_DEVICE_UPTIME_MS 0x06
#define KEY_SEQUENCE_NUMBER 0x0D
#define KEY_METRIC_NAME 0x15
#define KEY_AGGREGATION_INTERVAL 0x16
#define KEY_SUM 0x18
#define KEY_SUM_TRUNCATED 0x19
#define KEY_COUNT 0x1A
#define KEY_MIN 0x1B
#define KEY_MAX 0x1C
#define METRIC_MESSAGE_TYPE 0x05

struct minimal_aggregate {
	union {
		int64_t sum_int;
		float sum_float;
	};
	union {
		int64_t min_int;
		float min_float;
	};
	union {
		int64_t max_int;
		float max_float;
	};
	uint32_t count;
	bool sum_truncated;
};

struct minimal_metric;

struct minimal_series {
	struct minimal_metric* metric;
	const char* label_value;
	struct minimal_aggregate active;
	struct minimal_aggregate pending;
	int64_t pending_timestamp_ms;
	uint32_t pending_sequence;
	bool pending_valid;
	bool used;
};

struct minimal_metric {
	char name[CONFIG_SPOTFLOW_METRICS_MAX_NAME_LENGTH];
	const char* label_key;
	uint32_t next_sequence;
	int64_t deadline_ms;
	uint16_t series_start;
	uint16_t series_capacity;
	enum spotflow_agg_interval agg_interval;
	enum spotflow_metric_type type;
	bool used;
};

static struct minimal_metric g_metrics[CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED];
static struct minimal_series g_series[CONFIG_SPOTFLOW_METRICS_MAX_TIMESERIES];
static uint16_t g_series_used;
static K_MUTEX_DEFINE(g_metrics_lock);
static bool g_initialized;

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
static int64_t g_heartbeat_deadline_ms;
static int64_t g_heartbeat_uptime_ms;
static bool g_heartbeat_pending;
#endif

static int64_t interval_ms(enum spotflow_agg_interval interval)
{
	switch (interval) {
	case SPOTFLOW_AGG_INTERVAL_NONE:
		return 0;
	case SPOTFLOW_AGG_INTERVAL_1MIN:
		return 60 * MSEC_PER_SEC;
	case SPOTFLOW_AGG_INTERVAL_1HOUR:
		return 60 * 60 * MSEC_PER_SEC;
	case SPOTFLOW_AGG_INTERVAL_1DAY:
		return 24 * 60 * 60 * MSEC_PER_SEC;
	default:
		return -EINVAL;
	}
}

static int normalize_name(const char* input, char* output, size_t output_size)
{
	if (input == NULL || strlen(input) >= output_size) {
		return -EINVAL;
	}

	size_t out = 0;
	for (size_t in = 0; input[in] != '\0' && out < output_size - 1; ++in) {
		unsigned char c = (unsigned char)input[in];

		if (isalnum(c)) {
			output[out++] = (char)tolower(c);
		} else if (c == '_' || c == '-' || c == '.' || c == ' ') {
			output[out++] = '_';
		}
	}
	output[out] = '\0';
	return out == 0 ? -EINVAL : 0;
}

static struct minimal_metric* metric_from_handle(const void* handle, enum spotflow_metric_type type)
{
	uintptr_t address = (uintptr_t)handle;
	uintptr_t start = (uintptr_t)&g_metrics[0];
	uintptr_t end = (uintptr_t)&g_metrics[ARRAY_SIZE(g_metrics)];

	if (address < start || address >= end || (address - start) % sizeof(g_metrics[0]) != 0) {
		return NULL;
	}

	struct minimal_metric* metric = (struct minimal_metric*)handle;
	return metric->used && metric->type == type ? metric : NULL;
}

static void aggregate_reset(struct minimal_aggregate* aggregate, enum spotflow_metric_type type)
{
	memset(aggregate, 0, sizeof(*aggregate));
	if (type == SPOTFLOW_METRIC_TYPE_INT) {
		aggregate->min_int = INT64_MAX;
		aggregate->max_int = INT64_MIN;
	} else {
		aggregate->min_float = FLT_MAX;
		aggregate->max_float = -FLT_MAX;
	}
}

static void aggregate_int(struct minimal_aggregate* aggregate, int64_t value)
{
	if (!aggregate->sum_truncated) {
		if (value > 0 && aggregate->sum_int > INT64_MAX - value) {
			aggregate->sum_int = INT64_MAX;
			aggregate->sum_truncated = true;
		} else if (value < 0 && aggregate->sum_int < INT64_MIN - value) {
			aggregate->sum_int = INT64_MIN;
			aggregate->sum_truncated = true;
		} else {
			aggregate->sum_int += value;
		}
	}
	if (value < aggregate->min_int) {
		aggregate->min_int = value;
	}
	if (value > aggregate->max_int) {
		aggregate->max_int = value;
	}
	aggregate->count++;
}

static void aggregate_float(struct minimal_aggregate* aggregate, float value)
{
	aggregate->sum_float += value;
	if (value < aggregate->min_float) {
		aggregate->min_float = value;
	}
	if (value > aggregate->max_float) {
		aggregate->max_float = value;
	}
	aggregate->count++;
}

static int register_metric(const char* name, enum spotflow_metric_type type,
			   enum spotflow_agg_interval agg_interval, const char* label_key,
			   uint16_t max_series, struct minimal_metric** metric_out)
{
	char normalized[CONFIG_SPOTFLOW_METRICS_MAX_NAME_LENGTH];
	int rc;

	if (metric_out == NULL || max_series == 0 || interval_ms(agg_interval) < 0 ||
	    (label_key != NULL &&
	     (label_key[0] == '\0' || strlen(label_key) >= SPOTFLOW_MAX_LABEL_KEY_LEN))) {
		return -EINVAL;
	}
	rc = normalize_name(name, normalized, sizeof(normalized));
	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&g_metrics_lock, K_FOREVER);
	struct minimal_metric* available = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(g_metrics); ++i) {
		if (g_metrics[i].used && strcmp(g_metrics[i].name, normalized) == 0) {
			rc = -EEXIST;
			goto out;
		}
		if (!g_metrics[i].used && available == NULL) {
			available = &g_metrics[i];
		}
	}
	if (available == NULL || max_series > ARRAY_SIZE(g_series) - g_series_used) {
		rc = -ENOSPC;
		goto out;
	}

	memset(available, 0, sizeof(*available));
	strcpy(available->name, normalized);
	available->type = type;
	available->agg_interval = agg_interval;
	available->label_key = label_key;
	available->series_start = g_series_used;
	available->series_capacity = max_series;
	available->used = true;
	for (uint16_t i = 0; i < max_series; ++i) {
		struct minimal_series* series = &g_series[g_series_used + i];
		memset(series, 0, sizeof(*series));
		series->metric = available;
		aggregate_reset(&series->active, type);
		aggregate_reset(&series->pending, type);
	}
	g_series_used += max_series;
	*metric_out = available;
	rc = 0;
out:
	k_mutex_unlock(&g_metrics_lock);
	return rc;
}

static struct minimal_series* find_series(struct minimal_metric* metric, const char* label_value)
{
	struct minimal_series* available = NULL;

	for (uint16_t i = 0; i < metric->series_capacity; ++i) {
		struct minimal_series* series = &g_series[metric->series_start + i];
		if (series->used &&
		    ((label_value == NULL && series->label_value == NULL) ||
		     (label_value != NULL && series->label_value != NULL &&
		      strcmp(label_value, series->label_value) == 0))) {
			return series;
		}
		if (!series->used && available == NULL) {
			available = series;
		}
	}

	if (available != NULL) {
		available->used = true;
		available->label_value = label_value;
	}
	return available;
}

static int report_value(struct minimal_metric* metric, int64_t value_int, float value_float,
			const char* label_value)
{
	if (metric->label_key != NULL &&
	    (label_value == NULL || strlen(label_value) >= SPOTFLOW_MAX_LABEL_VALUE_LEN)) {
		return -EINVAL;
	}

	k_mutex_lock(&g_metrics_lock, K_FOREVER);
	struct minimal_series* series = find_series(metric, label_value);
	if (series == NULL) {
		k_mutex_unlock(&g_metrics_lock);
		return -ENOSPC;
	}

	int64_t now = k_uptime_get();
	if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
		aggregate_reset(&series->pending, metric->type);
		if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
			aggregate_int(&series->pending, value_int);
		} else {
			aggregate_float(&series->pending, value_float);
		}
		series->pending_timestamp_ms = now;
		series->pending_sequence = metric->next_sequence++;
		series->pending_valid = true;
	} else {
		if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
			aggregate_int(&series->active, value_int);
		} else {
			aggregate_float(&series->active, value_float);
		}
		if (metric->deadline_ms == 0) {
			metric->deadline_ms = now + interval_ms(metric->agg_interval);
		}
	}
	k_mutex_unlock(&g_metrics_lock);
	return 0;
}

static bool encode_header(zcbor_state_t* state, const struct minimal_metric* metric,
			  int64_t timestamp_ms, uint32_t sequence_number)
{
	return zcbor_uint32_put(state, KEY_MESSAGE_TYPE) &&
		zcbor_uint32_put(state, METRIC_MESSAGE_TYPE) &&
		zcbor_uint32_put(state, KEY_METRIC_NAME) &&
		zcbor_tstr_put_term(state, metric->name, sizeof(metric->name)) &&
		zcbor_uint32_put(state, KEY_AGGREGATION_INTERVAL) &&
		zcbor_uint32_put(state, metric->agg_interval) &&
		zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS) &&
		zcbor_int64_put(state, timestamp_ms) &&
		zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER) &&
		zcbor_uint64_put(state, sequence_number);
}

static bool encode_label(zcbor_state_t* state, const struct minimal_metric* metric,
			 const struct minimal_series* series)
{
	return zcbor_uint32_put(state, KEY_LABELS) && zcbor_map_start_encode(state, 1) &&
		zcbor_tstr_put_term(state, metric->label_key, SPOTFLOW_MAX_LABEL_KEY_LEN) &&
		zcbor_tstr_put_term(state, series->label_value, SPOTFLOW_MAX_LABEL_VALUE_LEN) &&
		zcbor_map_end_encode(state, 1);
}

static bool encode_value(zcbor_state_t* state, enum spotflow_metric_type type,
			 const struct minimal_aggregate* aggregate, bool sum)
{
	if (type == SPOTFLOW_METRIC_TYPE_INT) {
		return zcbor_int64_put(state, sum ? aggregate->sum_int : aggregate->min_int);
	}
	return zcbor_float64_put(state, sum ? aggregate->sum_float : aggregate->min_float);
}

static int encode_series(struct minimal_series* series, uint8_t* workspace, size_t workspace_size,
			 size_t* encoded_size)
{
	struct minimal_metric* metric = series->metric;
	const struct minimal_aggregate* pending = &series->pending;
	bool aggregated = metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE;
	uint32_t entries = aggregated ? 9 : 6;
	entries += metric->label_key != NULL ? 1 : 0;
	entries += aggregated && pending->sum_truncated ? 1 : 0;
	ZCBOR_STATE_E(state, 2, workspace, workspace_size, 1);

	bool success = zcbor_map_start_encode(state, entries) &&
		encode_header(state, metric, series->pending_timestamp_ms,
			      series->pending_sequence);
	if (success && metric->label_key != NULL) {
		success = encode_label(state, metric, series);
	}
	success = success && zcbor_uint32_put(state, KEY_SUM) &&
		encode_value(state, metric->type, pending, true);
	if (success && aggregated && pending->sum_truncated) {
		success = zcbor_uint32_put(state, KEY_SUM_TRUNCATED) && zcbor_bool_put(state, true);
	}
	if (success && aggregated) {
		success = zcbor_uint32_put(state, KEY_COUNT) &&
			zcbor_uint64_put(state, pending->count) &&
			zcbor_uint32_put(state, KEY_MIN) &&
			encode_value(state, metric->type, pending, false) &&
			zcbor_uint32_put(state, KEY_MAX);
		if (success) {
			success = metric->type == SPOTFLOW_METRIC_TYPE_INT
				? zcbor_int64_put(state, pending->max_int)
				: zcbor_float64_put(state, pending->max_float);
		}
	}
	success = success && zcbor_map_end_encode(state, entries);
	if (!success) {
		return -ENOBUFS;
	}
	*encoded_size = state->payload - workspace;
	return 0;
}

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
static int encode_heartbeat(uint8_t* workspace, size_t workspace_size, size_t* encoded_size)
{
	ZCBOR_STATE_E(state, 1, workspace, workspace_size, 1);
	bool success = zcbor_map_start_encode(state, 4) &&
		zcbor_uint32_put(state, KEY_MESSAGE_TYPE) &&
		zcbor_uint32_put(state, METRIC_MESSAGE_TYPE) &&
		zcbor_uint32_put(state, KEY_METRIC_NAME) &&
		zcbor_tstr_put_lit(state, "uptime_ms") &&
		zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS) &&
		zcbor_int64_put(state, g_heartbeat_uptime_ms) && zcbor_uint32_put(state, KEY_SUM) &&
		zcbor_int64_put(state, g_heartbeat_uptime_ms) && zcbor_map_end_encode(state, 4);
	if (!success) {
		return -ENOBUFS;
	}
	*encoded_size = state->payload - workspace;
	return 0;
}
#endif

void spotflow_minimal_metrics_init(void)
{
	k_mutex_lock(&g_metrics_lock, K_FOREVER);
	if (!g_initialized) {
		g_initialized = true;
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
		g_heartbeat_deadline_ms = 0;
#endif
	}
	k_mutex_unlock(&g_metrics_lock);
}

void spotflow_minimal_metrics_lock(void)
{
	k_mutex_lock(&g_metrics_lock, K_FOREVER);
}

void spotflow_minimal_metrics_unlock(void)
{
	k_mutex_unlock(&g_metrics_lock);
}

int spotflow_minimal_metrics_process(uint8_t* workspace, size_t workspace_size)
{
	if (workspace == NULL || workspace_size == 0) {
		return -EINVAL;
	}
	spotflow_minimal_metrics_init();
	int64_t now = k_uptime_get();
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
	spotflow_minimal_metrics_system_collect_if_due(now);
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
	spotflow_minimal_reset_process();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	k_mutex_lock(&g_metrics_lock, K_FOREVER);
	if (!g_heartbeat_pending && now >= g_heartbeat_deadline_ms) {
		g_heartbeat_uptime_ms = now;
		g_heartbeat_pending = true;
	}
	if (g_heartbeat_pending) {
		int64_t heartbeat_uptime_ms = g_heartbeat_uptime_ms;
		k_mutex_unlock(&g_metrics_lock);
		size_t encoded_size;
		int rc = encode_heartbeat(workspace, workspace_size, &encoded_size);
		if (rc < 0) {
			return rc;
		}
		rc = spotflow_transport_send_ingest_cbor(workspace, encoded_size);
		if (rc == 0) {
			k_mutex_lock(&g_metrics_lock, K_FOREVER);
			if (g_heartbeat_pending && g_heartbeat_uptime_ms == heartbeat_uptime_ms) {
				g_heartbeat_pending = false;
				g_heartbeat_deadline_ms = now +
					(int64_t)CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL *
						MSEC_PER_SEC;
			}
			k_mutex_unlock(&g_metrics_lock);
			return 1;
		}
		if (rc != -EAGAIN) {
			spotflow_transport_abort();
		}
		return rc;
	}
	k_mutex_unlock(&g_metrics_lock);
#endif

	k_mutex_lock(&g_metrics_lock, K_FOREVER);
	for (size_t i = 0; i < ARRAY_SIZE(g_metrics); ++i) {
		struct minimal_metric* metric = &g_metrics[i];
		if (!metric->used || metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE ||
		    metric->deadline_ms == 0 || now < metric->deadline_ms) {
			continue;
		}
		bool blocked = false;
		for (uint16_t j = 0; j < metric->series_capacity; ++j) {
			struct minimal_series* series = &g_series[metric->series_start + j];
			if (series->used && series->pending_valid && series->active.count != 0) {
				blocked = true;
				break;
			}
		}
		if (blocked) {
			continue;
		}
		for (uint16_t j = 0; j < metric->series_capacity; ++j) {
			struct minimal_series* series = &g_series[metric->series_start + j];
			if (series->used && !series->pending_valid && series->active.count != 0) {
				series->pending = series->active;
				series->pending_timestamp_ms = now;
				series->pending_sequence = metric->next_sequence++;
				series->pending_valid = true;
				aggregate_reset(&series->active, metric->type);
			}
		}
		metric->deadline_ms = now + interval_ms(metric->agg_interval);
	}

	struct minimal_series* selected = NULL;
	for (size_t i = 0; i < g_series_used; ++i) {
		if (g_series[i].pending_valid) {
			selected = &g_series[i];
			break;
		}
	}
	if (selected == NULL) {
		k_mutex_unlock(&g_metrics_lock);
		return 0;
	}
	struct minimal_series snapshot = *selected;
	k_mutex_unlock(&g_metrics_lock);

	size_t encoded_size;
	int rc = encode_series(&snapshot, workspace, workspace_size, &encoded_size);
	if (rc < 0) {
		k_mutex_lock(&g_metrics_lock, K_FOREVER);
		if (selected->pending_valid &&
		    selected->pending_sequence == snapshot.pending_sequence) {
			selected->pending_valid = false;
			aggregate_reset(&selected->pending, selected->metric->type);
		}
		k_mutex_unlock(&g_metrics_lock);
		return rc;
	}
	rc = spotflow_transport_send_ingest_cbor(workspace, encoded_size);
	if (rc < 0) {
		if (rc != -EAGAIN) {
			spotflow_transport_abort();
		}
		return rc;
	}

	k_mutex_lock(&g_metrics_lock, K_FOREVER);
	if (selected->pending_valid && selected->pending_sequence == snapshot.pending_sequence) {
		selected->pending_valid = false;
		aggregate_reset(&selected->pending, selected->metric->type);
	}
	k_mutex_unlock(&g_metrics_lock);
	return 1;
}

int spotflow_register_metric_int(const char* name, enum spotflow_agg_interval agg_interval,
				 struct spotflow_metric_int** metric_out)
{
	if (metric_out == NULL) {
		return -EINVAL;
	}
	struct minimal_metric* metric;
	int rc = register_metric(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, NULL, 1, &metric);
	if (rc == 0) {
		*metric_out = (struct spotflow_metric_int*)metric;
	}
	return rc;
}

int spotflow_register_metric_float(const char* name, enum spotflow_agg_interval agg_interval,
				   struct spotflow_metric_float** metric_out)
{
	if (metric_out == NULL) {
		return -EINVAL;
	}
	struct minimal_metric* metric;
	int rc = register_metric(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, NULL, 1, &metric);
	if (rc == 0) {
		*metric_out = (struct spotflow_metric_float*)metric;
	}
	return rc;
}

int spotflow_register_metric_int_with_labels(const char* name,
					     enum spotflow_agg_interval agg_interval,
					     uint16_t max_timeseries, uint8_t max_labels,
					     struct spotflow_metric_int** metric_out)
{
	ARG_UNUSED(name);
	ARG_UNUSED(agg_interval);
	ARG_UNUSED(max_timeseries);
	ARG_UNUSED(max_labels);
	ARG_UNUSED(metric_out);
	return -ENOTSUP;
}

int spotflow_register_metric_float_with_labels(const char* name,
					       enum spotflow_agg_interval agg_interval,
					       uint16_t max_timeseries, uint8_t max_labels,
					       struct spotflow_metric_float** metric_out)
{
	ARG_UNUSED(name);
	ARG_UNUSED(agg_interval);
	ARG_UNUSED(max_timeseries);
	ARG_UNUSED(max_labels);
	ARG_UNUSED(metric_out);
	return -ENOTSUP;
}

int spotflow_report_metric_int(struct spotflow_metric_int* handle, int64_t value)
{
	struct minimal_metric* metric = metric_from_handle(handle, SPOTFLOW_METRIC_TYPE_INT);
	if (metric == NULL || metric->label_key != NULL) {
		return -EINVAL;
	}
	return report_value(metric, value, 0.0f, NULL);
}

int spotflow_report_metric_float(struct spotflow_metric_float* handle, float value)
{
	struct minimal_metric* metric = metric_from_handle(handle, SPOTFLOW_METRIC_TYPE_FLOAT);
	if (metric == NULL || metric->label_key != NULL) {
		return -EINVAL;
	}
	return report_value(metric, 0, value, NULL);
}

int spotflow_report_event(struct spotflow_metric_int* metric)
{
	return spotflow_report_metric_int(metric, 1);
}

int spotflow_report_metric_int_with_labels(struct spotflow_metric_int* metric, int64_t value,
					   const struct spotflow_label* labels, uint8_t label_count)
{
	ARG_UNUSED(metric);
	ARG_UNUSED(value);
	ARG_UNUSED(labels);
	ARG_UNUSED(label_count);
	return -ENOTSUP;
}

int spotflow_report_metric_float_with_labels(struct spotflow_metric_float* metric, float value,
					     const struct spotflow_label* labels,
					     uint8_t label_count)
{
	ARG_UNUSED(metric);
	ARG_UNUSED(value);
	ARG_UNUSED(labels);
	ARG_UNUSED(label_count);
	return -ENOTSUP;
}

int spotflow_report_event_with_labels(struct spotflow_metric_int* metric,
				      const struct spotflow_label* labels, uint8_t label_count)
{
	ARG_UNUSED(metric);
	ARG_UNUSED(labels);
	ARG_UNUSED(label_count);
	return -ENOTSUP;
}

int spotflow_minimal_register_static_label_int(const char* name,
					       enum spotflow_agg_interval agg_interval,
					       const char* label_key, uint16_t max_series,
					       struct spotflow_metric_int** metric_out)
{
	if (label_key == NULL || metric_out == NULL) {
		return -EINVAL;
	}
	struct minimal_metric* metric;
	int rc = register_metric(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, label_key,
				 max_series, &metric);
	if (rc == 0) {
		*metric_out = (struct spotflow_metric_int*)metric;
	}
	return rc;
}

int spotflow_minimal_register_static_label_float(const char* name,
						 enum spotflow_agg_interval agg_interval,
						 const char* label_key, uint16_t max_series,
						 struct spotflow_metric_float** metric_out)
{
	if (label_key == NULL || metric_out == NULL) {
		return -EINVAL;
	}
	struct minimal_metric* metric;
	int rc = register_metric(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, label_key,
				 max_series, &metric);
	if (rc == 0) {
		*metric_out = (struct spotflow_metric_float*)metric;
	}
	return rc;
}

int spotflow_minimal_report_static_label_int(struct spotflow_metric_int* handle, int64_t value,
					     const char* label_value)
{
	struct minimal_metric* metric = metric_from_handle(handle, SPOTFLOW_METRIC_TYPE_INT);
	return metric == NULL || metric->label_key == NULL
		? -EINVAL
		: report_value(metric, value, 0.0f, label_value);
}

int spotflow_minimal_report_static_label_float(struct spotflow_metric_float* handle, float value,
					       const char* label_value)
{
	struct minimal_metric* metric = metric_from_handle(handle, SPOTFLOW_METRIC_TYPE_FLOAT);
	return metric == NULL || metric->label_key == NULL
		? -EINVAL
		: report_value(metric, 0, value, label_value);
}
