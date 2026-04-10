#include "metrics/spotflow_metrics_aggregator.h"
#include "metrics/spotflow_metrics_cbor.h"
#include "metrics/spotflow_metrics_net.h"
#include "net/spotflow_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <limits.h>
#include <float.h>
#include <errno.h>

static const char *TAG = "spotflow_aggregator";

/* Forward declarations */
static bool labels_equal(const struct metric_timeseries_state* ts,
                         const struct spotflow_label* labels, uint8_t label_count);
static void update_aggregation_int(struct metric_timeseries_state* ts, int64_t value);
static void update_aggregation_float(struct metric_timeseries_state* ts, float value);
static void reset_timeseries_state(struct spotflow_metric_base* metric,
                                   struct metric_timeseries_state* ts);
static void init_timeseries_aggregation_state(struct metric_timeseries_state* ts,
                                              enum spotflow_metric_type type);
static int flush_no_aggregation_metric(struct spotflow_metric_base* metric,
                                       const struct spotflow_label* labels, uint8_t label_count,
                                       int64_t value_int, float value_float);
static int flush_timeseries(struct spotflow_metric_base* metric,
                            struct metric_timeseries_state* ts,
                            int64_t timestamp_ms);
static int copy_labels_to_timeseries(struct metric_timeseries_state* ts,
                                     const struct spotflow_label* labels, uint8_t label_count);
static void aggregation_timer_callback(void* arg);
static uint32_t get_interval_ms(enum spotflow_agg_interval interval);
static struct metric_timeseries_state* find_or_create_timeseries(
    struct metric_aggregator_context* ctx, const struct spotflow_label* labels, uint8_t label_count);

/* Public API Implementation */

int aggregator_register_metric(struct spotflow_metric_base* metric)
{
    struct metric_aggregator_context* ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return -ENOMEM;
    }

    ctx->timeseries = calloc(metric->max_timeseries,
                             sizeof(struct metric_timeseries_state));
    if (!ctx->timeseries) {
        free(ctx);
        return -ENOMEM;
    }

    ctx->metric = metric;
    ctx->timeseries_count = 0;
    ctx->timeseries_capacity = metric->max_timeseries;
    ctx->timer_started = false;

    if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
        esp_timer_create_args_t timer_args = {
            .callback = aggregation_timer_callback,
            .arg = ctx,
            .name = "aggregation_timer"
        };
         esp_err_t err = esp_timer_create(&timer_args, &ctx->aggregation_timer);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to create aggregation timer: %d", err);
			free(ctx->timeseries);
			free(ctx);
			return -ENOMEM;
		}
    }

    metric->aggregator_context = ctx;
    ESP_LOGD(TAG, "Registered aggregator for metric '%s' (max_ts=%u)",
             metric->name, metric->max_timeseries);

    return 0;
}

int aggregator_report_value(struct spotflow_metric_base* metric,
                            const struct spotflow_label* labels, uint8_t label_count,
                            int64_t value_int, float value_float)
{
    if (!metric || !metric->aggregator_context) {
        return -EINVAL;
    }

    struct metric_aggregator_context* ctx = metric->aggregator_context;

    if (xSemaphoreTake(metric->lock, portMAX_DELAY) != pdTRUE) {
        return -EINVAL;
    }

    if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
        int rc = flush_no_aggregation_metric(metric, labels, label_count,
                                             value_int, value_float);
        xSemaphoreGive(metric->lock);
        return rc;
    }

    struct metric_timeseries_state* ts =
        find_or_create_timeseries(ctx, labels, label_count);

    if (!ts) {
        ESP_LOGW(TAG, "Time series pool full for metric '%s' (%u/%u)",
                 metric->name, ctx->timeseries_count, ctx->timeseries_capacity);
        xSemaphoreGive(metric->lock);
        return -ENOSPC;
    }

    if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
        update_aggregation_int(ts, value_int);
    } else if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        update_aggregation_float(ts, value_float);
    } else {
        xSemaphoreGive(metric->lock);
        ESP_LOGE(TAG, "Invalid metric type: %d", metric->type);
        return -EINVAL;
    }

    if (!ctx->timer_started) {
        uint32_t interval_ms = get_interval_ms(metric->agg_interval);
        if (interval_ms > 0) {
            uint32_t jitter = esp_random() % (interval_ms / 10);
            esp_timer_start_once(ctx->aggregation_timer, (interval_ms - jitter) * 1000ULL);
            ctx->timer_started = true;
            ESP_LOGD(TAG, "Started aggregation timer for metric '%s' (interval=%u ms, jitter=%u ms)",
                     metric->name, interval_ms, jitter);
        }
    }

    xSemaphoreGive(metric->lock);
    return 0;
}

/* --- Internal functions --- */

static bool labels_equal(const struct metric_timeseries_state* ts,
                         const struct spotflow_label* labels, uint8_t label_count)
{
    if (ts->label_count != label_count) return false;

    for (uint8_t i = 0; i < label_count; i++) {
        if (strcmp(ts->labels[i].key, labels[i].key) != 0 ||
            strcmp(ts->labels[i].value, labels[i].value) != 0) {
            return false;
        }
    }
    return true;
}

static int copy_labels_to_timeseries(struct metric_timeseries_state* ts,
                                     const struct spotflow_label* labels, uint8_t label_count)
{
    for (uint8_t i = 0; i < label_count; i++) {
        if (!labels[i].key || !labels[i].value) {
            ESP_LOGE(TAG, "Label key or value NULL at index %u", i);
            return -1;
        }
        strncpy(ts->labels[i].key, labels[i].key, SPOTFLOW_MAX_LABEL_KEY_LEN - 1);
        ts->labels[i].key[SPOTFLOW_MAX_LABEL_KEY_LEN - 1] = '\0';
        strncpy(ts->labels[i].value, labels[i].value, SPOTFLOW_MAX_LABEL_VALUE_LEN - 1);
        ts->labels[i].value[SPOTFLOW_MAX_LABEL_VALUE_LEN - 1] = '\0';
    }
    return 0;
}

static void update_aggregation_int(struct metric_timeseries_state* ts, int64_t value)
{
    ts->count++;
    int64_t old_sum = ts->sum_int;
    ts->sum_int += value;
    if ((value > 0 && ts->sum_int < old_sum) || (value < 0 && ts->sum_int > old_sum)) {
        ts->sum_truncated = true;
    }
    if (value < ts->min_int) ts->min_int = value;
    if (value > ts->max_int) ts->max_int = value;
}

static void update_aggregation_float(struct metric_timeseries_state* ts, float value)
{
    ts->count++;
    ts->sum_float += value;
    if (value < ts->min_float) ts->min_float = value;
    if (value > ts->max_float) ts->max_float = value;
}

static void reset_timeseries_state(struct spotflow_metric_base* metric,
                                   struct metric_timeseries_state* ts)
{
    ts->count = 0;
    ts->sum_truncated = false;

    if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
        ts->sum_int = 0;
        ts->min_int = INT64_MAX;
        ts->max_int = INT64_MIN;
    } else if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        ts->sum_float = 0.0f;
        ts->min_float = FLT_MAX;
        ts->max_float = -FLT_MAX;
    } else {
        ESP_LOGE(TAG, "Invalid metric type: %d", metric->type);
    }
}

static void init_timeseries_aggregation_state(struct metric_timeseries_state* ts,
                                              enum spotflow_metric_type type)
{
    if (type == SPOTFLOW_METRIC_TYPE_INT) {
        ts->min_int = INT64_MAX;
        ts->max_int = INT64_MIN;
    } else if (type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        ts->min_float = FLT_MAX;
        ts->max_float = -FLT_MAX;
    }
}

static uint32_t get_interval_ms(enum spotflow_agg_interval interval)
{
    switch (interval) {
    case SPOTFLOW_AGG_INTERVAL_NONE: return 0;
    case SPOTFLOW_AGG_INTERVAL_1MIN: return 60 * 1000;
    case SPOTFLOW_AGG_INTERVAL_1HOUR: return 60 * 60 * 1000;
    case SPOTFLOW_AGG_INTERVAL_1DAY: return 24 * 60 * 60 * 1000;
    default: return 60 * 1000;
    }
}

static struct metric_timeseries_state* find_or_create_timeseries(
    struct metric_aggregator_context* ctx, const struct spotflow_label* labels, uint8_t label_count)
{
    struct metric_timeseries_state* inactive_slot = NULL;
    struct metric_timeseries_state* evictable_slot = NULL;

    for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
        struct metric_timeseries_state* ts = &ctx->timeseries[i];

        if (ts->active) {
            if (labels_equal(ts, labels, label_count)) return ts;
            if (ts->count == 0 && evictable_slot == NULL) evictable_slot = ts;
        } else if (!inactive_slot) {
            inactive_slot = ts;
        }
    }

    struct metric_timeseries_state* ts = inactive_slot ? inactive_slot : evictable_slot;
    if (!ts) return NULL;

    if (!inactive_slot) {
        ESP_LOGD(TAG, "Evicting idle timeseries for metric '%s'", ctx->metric->name);
    } else {
        ctx->timeseries_count++;
    }

    memset(ts, 0, sizeof(*ts));
    ts->active = true;
    ts->label_count = label_count;
    if (copy_labels_to_timeseries(ts, labels, label_count) < 0) {
        ts->active = false;
        return NULL;
    }

    init_timeseries_aggregation_state(ts, ctx->metric->type);
    ESP_LOGD(TAG, "Initialized timeseries for metric '%s'", ctx->metric->name);
    return ts;
}


static int flush_no_aggregation_metric(struct spotflow_metric_base* metric,
                                       const struct spotflow_label* labels, uint8_t label_count,
                                       int64_t value_int, float value_float)
{
    uint8_t* cbor_data = NULL;
    size_t cbor_len = 0;
    uint64_t seq_num = metric->sequence_number++;

    int rc = spotflow_metrics_cbor_encode_no_aggregation(metric, labels, label_count,
                                                        value_int, value_float,
                                                        esp_timer_get_time() / 1000ULL,
                                                        seq_num, &cbor_data, &cbor_len);
    if (rc < 0) return rc;

    rc = spotflow_metrics_enqueue(cbor_data, cbor_len);
    if (rc < 0) free(cbor_data);
    return rc;
}

static int flush_timeseries(struct spotflow_metric_base* metric,
                            struct metric_timeseries_state* ts,
                            int64_t timestamp_ms)
{
    uint8_t* cbor_data = NULL;
    size_t cbor_len = 0;
    uint64_t seq_num = metric->sequence_number++;

    int rc = spotflow_metrics_cbor_encode_aggregated(metric, ts, timestamp_ms,
                                                     seq_num, &cbor_data, &cbor_len);
    if (rc < 0) {
        reset_timeseries_state(metric, ts);
        return rc;
    }

    rc = spotflow_metrics_enqueue(cbor_data, cbor_len);
    if (rc < 0) free(cbor_data);
    reset_timeseries_state(metric, ts);
    return rc;
}

static void aggregation_timer_callback(void* arg)
{
    struct metric_aggregator_context* ctx = (struct metric_aggregator_context*)arg;
    struct spotflow_metric_base* metric = ctx->metric;
    int64_t timestamp_ms = esp_timer_get_time() / 1000ULL;

    if (xSemaphoreTake(metric->lock, portMAX_DELAY) != pdTRUE) return;

    for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
        struct metric_timeseries_state* ts = &ctx->timeseries[i];
        if (ts->active && ts->count > 0) {
            int rc = flush_timeseries(metric, ts, timestamp_ms);
            if (rc < 0) ESP_LOGE(TAG, "Failed to flush timeseries for metric '%s': %d",
                                 metric->name, rc);
        }
    }

    uint32_t interval_ms = get_interval_ms(metric->agg_interval);
    if (interval_ms > 0) esp_timer_start_once(ctx->aggregation_timer, interval_ms * 1000ULL);

    xSemaphoreGive(metric->lock);
}