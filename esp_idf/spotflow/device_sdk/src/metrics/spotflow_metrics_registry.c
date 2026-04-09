#include "metrics/spotflow_metrics_registry.h"
#include "metrics/spotflow_metrics_aggregator.h"
#include "logging/spotflow_log_backend.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <errno.h>


/* Metric registry */
static struct spotflow_metric_base g_metric_registry[CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED];
static SemaphoreHandle_t g_registry_lock;

/* Forward declarations of static functions */
static void normalize_metric_name(const char* input, char* output, size_t output_size);
static int find_available_slot(void);
static struct spotflow_metric_base* find_metric_by_name(const char* normalized_name);
static int validate_metric_params(const char* name, uint16_t max_timeseries, uint8_t max_labels);
static int normalize_and_validate_metric_name(const char* name, char* out_normalized, size_t out_size);
static void init_metric_struct(struct spotflow_metric_base* metric, const char* normalized_name,
                               enum spotflow_metric_type type,
                               enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
                               uint8_t max_labels);
static int register_metric_common(const char* name, enum spotflow_metric_type type,
                                  enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
                                  uint8_t max_labels, struct spotflow_metric_base** metric_out);

/* Initialize registry mutex */
void spotflow_metrics_init(void)
{
    g_registry_lock = xSemaphoreCreateMutex();
    if (g_registry_lock == NULL) {
        SPOTFLOW_LOG("ERROR: Failed to create registry mutex");
    }
}

/* Public API Implementation */

int spotflow_register_metric_int(const char* name, enum spotflow_agg_interval agg_interval,
                                 struct spotflow_metric_int** metric_out)
{
    struct spotflow_metric_base* base;
    int rc;

    if (metric_out == NULL) {
        SPOTFLOW_LOG("ERROR: metric_out cannot be NULL");
        return -EINVAL;
    }

    spotflow_metrics_init();
    rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, 1, 0, &base);
    if (rc < 0) {
        return rc;
    }

    if (base->type != SPOTFLOW_METRIC_TYPE_INT) {
        SPOTFLOW_LOG("ERROR: Type mismatch: expected INT, got %d", base->type);
        return -EINVAL;
    }

    *metric_out = (struct spotflow_metric_int*)base;
    return 0;
}

int spotflow_register_metric_float(const char* name, enum spotflow_agg_interval agg_interval,
                                   struct spotflow_metric_float** metric_out)
{
    struct spotflow_metric_base* base;
    int rc;

    if (metric_out == NULL) {
        SPOTFLOW_LOG("ERROR: metric_out cannot be NULL");
        return -EINVAL;
    }

    rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, 1, 0, &base);
    if (rc < 0) {
        return rc;
    }

    if (base->type != SPOTFLOW_METRIC_TYPE_FLOAT) {
        SPOTFLOW_LOG("ERROR: Type mismatch: expected FLOAT, got %d", base->type);
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
        SPOTFLOW_LOG("ERROR: metric_out cannot be NULL");
        return -EINVAL;
    }

    if (max_labels == 0) {
        SPOTFLOW_LOG("ERROR: Labeled metric requires max_labels > 0");
        return -EINVAL;
    }

    rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_INT, agg_interval, max_timeseries,
                                max_labels, &base);
    if (rc < 0) {
        return rc;
    }

    if (base->type != SPOTFLOW_METRIC_TYPE_INT) {
        SPOTFLOW_LOG("ERROR: Type mismatch: expected INT, got %d", base->type);
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
        SPOTFLOW_LOG("ERROR: metric_out cannot be NULL");
        return -EINVAL;
    }

    if (max_labels == 0) {
        SPOTFLOW_LOG("ERROR: Labeled metric requires max_labels > 0");
        return -EINVAL;
    }

    rc = register_metric_common(name, SPOTFLOW_METRIC_TYPE_FLOAT, agg_interval, max_timeseries,
                                max_labels, &base);
    if (rc < 0) {
        return rc;
    }

    if (base->type != SPOTFLOW_METRIC_TYPE_FLOAT) {
        SPOTFLOW_LOG("ERROR: Type mismatch: expected FLOAT, got %d", base->type);
        return -EINVAL;
    }

    *metric_out = (struct spotflow_metric_float*)base;
    return 0;
}

/* Static function implementations */

static void normalize_metric_name(const char* input, char* output, size_t output_size)
{
    size_t i = 0, j = 0;
    while (input[i] != '\0' && j < (output_size - 1)) {
        char c = input[i];
        if (isalnum((unsigned char)c)) {
            output[j++] = tolower((unsigned char)c);
        } else if (c == '_' || c == '-' || c == '.' || c == ' ') {
            output[j++] = '_';
        }
        i++;
    }
    output[j] = '\0';
}

static int find_available_slot(void)
{
    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
        if (g_metric_registry[i].aggregator_context == NULL) {
            return i;
        }
    }
    return -1;
}

static struct spotflow_metric_base* find_metric_by_name(const char* normalized_name)
{
    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
        struct spotflow_metric_base* base = &g_metric_registry[i];
        if (base->aggregator_context != NULL && strcmp(base->name, normalized_name) == 0) {
            return base;
        }
    }
    return NULL;
}

static int validate_metric_params(const char* name, uint16_t max_timeseries, uint8_t max_labels)
{
    if (name == NULL) {
        SPOTFLOW_LOG("ERROR: Metric name cannot be NULL");
        return -EINVAL;
    }

    if (max_timeseries == 0 || max_timeseries > 256) {
        SPOTFLOW_LOG("ERROR: Invalid max_timeseries: %u (must be 1-256)", max_timeseries);
        return -EINVAL;
    }

    if (max_labels > CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC) {
        SPOTFLOW_LOG("ERROR: Invalid max_labels: %u (max %d)", max_labels,
                     CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC);
        return -EINVAL;
    }

    return 0;
}

static int normalize_and_validate_metric_name(const char* name, char* out_normalized, size_t out_size)
{
    normalize_metric_name(name, out_normalized, out_size);

    if (strcmp(name, out_normalized) != 0) {
        SPOTFLOW_LOG("WARN: Metric name '%s' normalized to '%s'", name, out_normalized);
    }

    if (strlen(out_normalized) == 0) {
        SPOTFLOW_LOG("ERROR: Metric name '%s' normalizes to empty string", name);
        return -EINVAL;
    }

    return 0;
}

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
    metric->lock = xSemaphoreCreateMutex();
}

static int register_metric_common(const char* name, enum spotflow_metric_type type,
                                  enum spotflow_agg_interval agg_interval, uint16_t max_timeseries,
                                  uint8_t max_labels, struct spotflow_metric_base** metric_out)
{
    int rc = validate_metric_params(name, max_timeseries, max_labels);
    if (rc < 0) return rc;

    char normalized_name[256];
    rc = normalize_and_validate_metric_name(name, normalized_name, sizeof(normalized_name));
    if (rc < 0) return rc;

    xSemaphoreTake(g_registry_lock, portMAX_DELAY);

    if (find_metric_by_name(normalized_name) != NULL) {
        SPOTFLOW_LOG("ERROR: Metric '%s' already registered", normalized_name);
        xSemaphoreGive(g_registry_lock);
        return -EEXIST;
    }

    int slot = find_available_slot();
    if (slot < 0) {
        SPOTFLOW_LOG("ERROR: Metric registry full (%d/%d)", CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED,
                     CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED);
        xSemaphoreGive(g_registry_lock);
        return -ENOSPC;
    }

    struct spotflow_metric_base* metric = &g_metric_registry[slot];
    init_metric_struct(metric, normalized_name, type, agg_interval, max_timeseries, max_labels);

    rc = aggregator_register_metric(metric);
    if (rc < 0) {
        SPOTFLOW_LOG("ERROR: Failed to initialize aggregator for metric '%s': %d",
                     normalized_name, rc);
        memset(&g_metric_registry[slot], 0, sizeof(g_metric_registry[slot]));
        xSemaphoreGive(g_registry_lock);
        return rc;
    }

    xSemaphoreGive(g_registry_lock);

    SPOTFLOW_LOG("INFO: Registered metric '%s' (type=%s, agg=%d, max_ts=%u, max_labels=%u)",
                 normalized_name,
                 (type == SPOTFLOW_METRIC_TYPE_INT) ? "int" :
                 (type == SPOTFLOW_METRIC_TYPE_FLOAT) ? "float" : "unknown",
                 metric->agg_interval, max_timeseries, max_labels);

    *metric_out = metric;
    return 0;
}