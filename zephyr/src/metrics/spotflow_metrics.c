#include "metrics/spotflow_metrics.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_cbor.h"
#if IS_ENABLED(CONFIG_SPOTFLOW_SYSTEM_METRICS)
#include "metrics/spotflow_system_metrics.h"
#endif

LOG_MODULE_REGISTER(spotflow_metrics_core, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define METRIC_KIND_INT 0
#define METRIC_KIND_FLOAT 1

struct metric_dimension_storage {
	char key[CONFIG_SPOTFLOW_METRICS_LABEL_MAX_LEN];
	char value[CONFIG_SPOTFLOW_METRICS_LABEL_MAX_LEN];
};

struct metric_series {
	bool used;
	uint8_t dimension_count;
	struct metric_dimension_storage dimensions[CONFIG_SPOTFLOW_METRICS_MAX_DIMENSIONS];
	uint64_t count;
	union {
		struct spotflow_metric_stats_int ints;
		struct spotflow_metric_stats_float floats;
	} stats;
	size_t samples_count;
	union {
		int64_t ints[CONFIG_SPOTFLOW_METRICS_MAX_SAMPLES_PER_SERIES];
		double floats[CONFIG_SPOTFLOW_METRICS_MAX_SAMPLES_PER_SERIES];
	} samples;
};

struct spotflow_metric {
	bool used;
	char name[CONFIG_SPOTFLOW_METRICS_NAME_MAX_LEN];
	uint8_t kind;
	bool collect_samples;
	uint8_t max_dimensions;
	uint8_t max_timeseries;
	uint64_t sequence_number;
	struct metric_series series[CONFIG_SPOTFLOW_METRICS_MAX_TIMESERIES_PER_METRIC];
};

static struct spotflow_metric metrics[CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED];
static struct k_work_delayable metrics_flush_work;
static bool pipeline_started;

static const char* aggregation_interval_str(void)
{
#if CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS == 0
	return "PT0S";
#elif CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS == 60
	return "PT1M";
#elif CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS == 600
	return "PT10M";
#elif CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS == 3600
	return "PT1H";
#else
	return NULL;
#endif
}

static inline bool strings_equal(const char* a, const char* b)
{
	return strncmp(a, b, CONFIG_SPOTFLOW_METRICS_LABEL_MAX_LEN) == 0;
}

static bool dimensions_match(const struct metric_series* series,
			     const struct spotflow_metric_dimension* dims, size_t dims_count)
{
	if (series->dimension_count != dims_count) {
		return false;
	}

	for (size_t i = 0; i < dims_count; i++) {
		if (!strings_equal(series->dimensions[i].key, dims[i].key) ||
		    !strings_equal(series->dimensions[i].value, dims[i].value)) {
			return false;
		}
	}
	return true;
}

static int store_dimensions(struct metric_series* series,
			    const struct spotflow_metric_dimension* dims, size_t dims_count)
{
	if (dims_count > CONFIG_SPOTFLOW_METRICS_MAX_DIMENSIONS) {
		return -EINVAL;
	}

	series->dimension_count = dims_count;
	for (size_t i = 0; i < dims_count; i++) {
		strncpy(series->dimensions[i].key, dims[i].key,
			sizeof(series->dimensions[i].key) - 1);
		series->dimensions[i].key[sizeof(series->dimensions[i].key) - 1] = '\0';

		strncpy(series->dimensions[i].value, dims[i].value,
			sizeof(series->dimensions[i].value) - 1);
		series->dimensions[i].value[sizeof(series->dimensions[i].value) - 1] = '\0';
	}
	return 0;
}

static struct spotflow_metric* find_empty_metric_slot(void)
{
	for (size_t i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
		if (!metrics[i].used) {
			return &metrics[i];
		}
	}
	return NULL;
}

static struct metric_series* find_or_create_series(struct spotflow_metric* metric,
						   const struct spotflow_metric_dimension* dims,
						   size_t dims_count)
{
	for (size_t i = 0; i < metric->max_timeseries; i++) {
		if (metric->series[i].used &&
		    dimensions_match(&metric->series[i], dims, dims_count)) {
			return &metric->series[i];
		}
	}

	for (size_t i = 0; i < metric->max_timeseries; i++) {
		if (!metric->series[i].used) {
			struct metric_series* s = &metric->series[i];
			memset(s, 0, sizeof(*s));
			if (store_dimensions(s, dims, dims_count) != 0) {
				return NULL;
			}
			s->used = true;
			return s;
		}
	}
	return NULL;
}

static void reset_series(struct spotflow_metric* metric, struct metric_series* series)
{
	series->count = 0;
	series->samples_count = 0;
	if (metric->kind == METRIC_KIND_INT) {
		series->stats.ints.sum = 0;
		series->stats.ints.min = 0;
		series->stats.ints.max = 0;
	} else {
		series->stats.floats.sum = 0.0;
		series->stats.floats.min = 0.0;
		series->stats.floats.max = 0.0;
	}
}

static int add_sample_int(struct spotflow_metric* metric, struct metric_series* series,
			  int64_t value)
{
	if (series->count == 0) {
		series->stats.ints.min = value;
		series->stats.ints.max = value;
		series->stats.ints.sum = value;
	} else {
		series->stats.ints.sum += value;
		if (value < series->stats.ints.min) {
			series->stats.ints.min = value;
		}
		if (value > series->stats.ints.max) {
			series->stats.ints.max = value;
		}
	}
	series->count++;

	if (metric->collect_samples &&
	    series->samples_count < CONFIG_SPOTFLOW_METRICS_MAX_SAMPLES_PER_SERIES) {
		series->samples.ints[series->samples_count++] = value;
	}

	return 0;
}

static int add_sample_float(struct spotflow_metric* metric, struct metric_series* series,
			    double value)
{
	if (series->count == 0) {
		series->stats.floats.min = value;
		series->stats.floats.max = value;
		series->stats.floats.sum = value;
	} else {
		series->stats.floats.sum += value;
		if (value < series->stats.floats.min) {
			series->stats.floats.min = value;
		}
		if (value > series->stats.floats.max) {
			series->stats.floats.max = value;
		}
	}
	series->count++;

	if (metric->collect_samples &&
	    series->samples_count < CONFIG_SPOTFLOW_METRICS_MAX_SAMPLES_PER_SERIES) {
		series->samples.floats[series->samples_count++] = value;
	}

	return 0;
}

static int encode_and_enqueue_metric(struct spotflow_metric* metric, struct metric_series* series)
{
	struct spotflow_metric_dimension dim_view[CONFIG_SPOTFLOW_METRICS_MAX_DIMENSIONS];
	for (size_t i = 0; i < series->dimension_count; i++) {
		dim_view[i].key = series->dimensions[i].key;
		dim_view[i].value = series->dimensions[i].value;
	}

	int rc;
	struct spotflow_mqtt_metrics_msg* msg = k_malloc(sizeof(struct spotflow_mqtt_metrics_msg));
	if (!msg) {
		return -ENOMEM;
	}

	const char* agg_interval = aggregation_interval_str();
	if (metric->kind == METRIC_KIND_INT) {
		rc = spotflow_metrics_cbor_encode_int(metric->name, dim_view, series->dimension_count,
						      &series->stats.ints, series->samples.ints,
						      series->samples_count, agg_interval,
						      metric->sequence_number, &msg->payload,
						      &msg->len);
	} else {
		rc = spotflow_metrics_cbor_encode_float(
		    metric->name, dim_view, series->dimension_count, &series->stats.floats,
		    series->samples.floats, series->samples_count, agg_interval,
		    metric->sequence_number, &msg->payload, &msg->len);
	}

	if (rc < 0) {
		k_free(msg);
		return rc;
	}

	metric->sequence_number++;
	rc = spotflow_metrics_enqueue(msg);
	if (rc < 0) {
		k_free(msg->payload);
		k_free(msg);
	}
	return rc;
}

static void flush_metrics(struct k_work* work)
{
	ARG_UNUSED(work);

	for (size_t i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
		if (!metrics[i].used) {
			continue;
		}
		for (size_t s = 0; s < metrics[i].max_timeseries; s++) {
			struct metric_series* series = &metrics[i].series[s];
			if (!series->used || series->count == 0) {
				continue;
			}
			int rc = encode_and_enqueue_metric(&metrics[i], series);
			if (rc < 0) {
				LOG_DBG("Failed to enqueue metric %s: %d", metrics[i].name, rc);
			}
			reset_series(&metrics[i], series);
		}
	}

	uint32_t period = CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS;
	if (period == 0) {
		period = 1;
	}
	k_work_reschedule(&metrics_flush_work, K_SECONDS(period));
}

int spotflow_metrics_pipeline_init(void)
{
	if (pipeline_started) {
		return 0;
	}

	k_work_init_delayable(&metrics_flush_work, flush_metrics);
	uint32_t period = CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS;
	if (period == 0) {
		period = 1;
	}
	k_work_schedule(&metrics_flush_work, K_SECONDS(period));
	pipeline_started = true;
#if IS_ENABLED(CONFIG_SPOTFLOW_SYSTEM_METRICS)
	spotflow_system_metrics_start();
#endif
	return 0;
}

static t_metric* register_metric_common(const char* name, uint8_t max_ts_count,
					uint8_t max_dimensions, bool collect_samples, uint8_t kind)
{
	if (name == NULL) {
		return NULL;
	}
	if (max_ts_count == 0 || max_ts_count > CONFIG_SPOTFLOW_METRICS_MAX_TIMESERIES_PER_METRIC) {
		return NULL;
	}
	if (max_dimensions > CONFIG_SPOTFLOW_METRICS_MAX_DIMENSIONS) {
		return NULL;
	}

	struct spotflow_metric* metric = find_empty_metric_slot();
	if (!metric) {
		return NULL;
	}

	memset(metric, 0, sizeof(*metric));
	strncpy(metric->name, name, sizeof(metric->name) - 1);
	metric->kind = kind;
	metric->collect_samples = collect_samples;
	metric->max_dimensions = max_dimensions;
	metric->max_timeseries = max_ts_count;
	metric->used = true;
	return metric;
}

t_metric* register_metric_int(const char* name, uint8_t max_ts_count, uint8_t max_dimensions,
			      bool collect_samples)
{
	spotflow_metrics_pipeline_init();
	return register_metric_common(name, max_ts_count, max_dimensions, collect_samples,
				      METRIC_KIND_INT);
}

t_metric* register_metric_float(const char* name, uint8_t max_ts_count, uint8_t max_dimensions,
				bool collect_samples)
{
	spotflow_metrics_pipeline_init();
	return register_metric_common(name, max_ts_count, max_dimensions, collect_samples,
				      METRIC_KIND_FLOAT);
}

t_metric* register_metric_simple(const char* name, bool collect_samples)
{
	return register_metric_int(name, 1, 0, collect_samples);
}

static int report_metric_common(t_metric* metric, const struct spotflow_metric_dimension* dims,
				size_t dims_count, int64_t value_int, double value_float,
				bool is_float)
{
	if (metric == NULL) {
		return -EINVAL;
	}

	if (dims_count > metric->max_dimensions) {
		return -EINVAL;
	}

	struct metric_series* series = find_or_create_series(metric, dims, dims_count);
	if (!series) {
		return -ENOMEM;
	}

	if (metric->kind == METRIC_KIND_INT && !is_float) {
		int rc = add_sample_int(metric, series, value_int);
		if (rc == 0 && CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS == 0) {
			spotflow_metrics_flush_now();
		}
		return rc;
	}
	if (metric->kind == METRIC_KIND_FLOAT && is_float) {
		int rc = add_sample_float(metric, series, value_float);
		if (rc == 0 && CONFIG_SPOTFLOW_METRICS_AGGREGATION_INTERVAL_SECONDS == 0) {
			spotflow_metrics_flush_now();
		}
		return rc;
	}
	return -EINVAL;
}

int report_metric_int(t_metric* metric, const struct spotflow_metric_dimension* dimensions,
		      size_t dimensions_count, int64_t value)
{
	return report_metric_common(metric, dimensions, dimensions_count, value, 0.0, false);
}

int report_metric_float(t_metric* metric, const struct spotflow_metric_dimension* dimensions,
			size_t dimensions_count, double value)
{
	return report_metric_common(metric, dimensions, dimensions_count, 0, value, true);
}

int spotflow_metrics_flush_now(void)
{
	if (!pipeline_started) {
		return -EINVAL;
	}
	k_work_submit(&metrics_flush_work.work);
	return 0;
}
