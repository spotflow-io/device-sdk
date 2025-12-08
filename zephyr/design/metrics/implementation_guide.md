# Spotflow Metrics - Implementation Guide

## Overview

This document provides practical guidance for implementing the Spotflow Metrics feature in the Spotflow Device SDK. It is intended for SDK developers who will write the C code to realize the architecture and API specifications.

**Related Documents**:
- Architecture Specification: `architecture.md`
- API Specification: `api_specification.md`
- Ingestion Protocol: `ingestion_protocol_specification.md`

## Terminology

This document uses the following terminology consistently:

- **Time series**: A unique sequence of metric values identified by a metric name and dimension combination
- **Time series pool**: Pre-allocated storage for tracking multiple time series within a dimensional metric
- **Time series slot**: Individual entry in the time series pool (one slot per unique dimension combination)
- **Dimensionless metric**: Metric without dimensions (single time series)
- **Dimensional metric**: Metric with dimensions (multiple time series, one per unique dimension combination)

## Implementation Roadmap

### Phase 1: Core Infrastructure (Week 1-2)

**Goal**: Implement basic metric registration and simple (dimensionless) metrics with aggregation.

**Tasks**:
1. Create module directory structure
2. Implement metric registry
3. Implement simple metric registration API
4. Implement aggregation for simple metrics
5. Implement CBOR encoding for simple messages
6. Integrate message queue
7. Write unit tests

**Deliverables**:
- `spotflow_metrics_backend.c/h`
- `spotflow_metrics_aggregator.c/h` (simple metrics only)
- `spotflow_metrics_cbor.c/h` (simple messages only)
- Unit test suite for core components

### Phase 2: Dimensional Metrics (Week 3-4)

**Goal**: Add support for metrics with dimensions and multiple time series.

**Tasks**:
1. Implement dimension handling and hashing
2. Extend aggregator for multi-timeseries tracking
3. Implement pool full error handling (reject with -ENOSPC)
4. Add dimensional metric registration APIs
5. Write unit tests for dimensional features

**Deliverables**:
- Extended `spotflow_metrics_aggregator.c` with time series management
- Dimensional metric APIs in `spotflow_metrics_backend.c`
- Unit tests for dimensional metrics and cardinality limits

### Phase 3: Network Integration (Week 5)

**Goal**: Integrate metrics transmission with existing MQTT processor.

**Tasks**:
1. Implement network polling layer
2. Integrate with `spotflow_processor.c`
3. Add metrics to polling priority (after coredumps, before logs)
4. Handle MQTT publish and error cases
5. Integration testing with live MQTT broker

**Deliverables**:
- `spotflow_metrics_net.c/h`
- Modifications to `spotflow_processor.c` (minimal)
- Integration tests

### Phase 4: Configuration and Polish (Week 6)

**Goal**: Add Kconfig options, documentation, and sample application.

**Tasks**:
1. Create Kconfig file with all options
2. Update CMakeLists.txt
3. Write API documentation headers
4. Create sample application
5. Write user documentation
6. Code review and cleanup

**Deliverables**:
- `KConfig` for metrics subsystem
- `CMakeLists.txt` for metrics module
- Documented header files
- Sample application in `samples/metrics/`
- User guide documentation

### Phase 5: Testing and Validation (Week 7-8)

**Goal**: Integration testing and cloud platform validation.

**Tasks**:
1. Integration testing with full SDK
2. Cloud platform integration validation
3. Bug fixes and edge case handling
4. Documentation review and updates

**Deliverables**:
- Integration test suite
- Validated cloud integration
- Updated documentation
- Production-ready code

## Directory Structure

Create the following directory structure:

```
modules/lib/spotflow/zephyr/src/metrics/
├── spotflow_metrics_backend.c
├── spotflow_metrics_backend.h
├── spotflow_metrics_aggregator.c
├── spotflow_metrics_aggregator.h
├── spotflow_metrics_cbor.c
├── spotflow_metrics_cbor.h
├── spotflow_metrics_net.c
├── spotflow_metrics_net.h
├── spotflow_metrics_types.h       # Internal types
├── spotflow_metrics.h              # Public API header
├── CMakeLists.txt
└── KConfig
```

## File-by-File Implementation Guide

### 1. spotflow_metrics_types.h

**Purpose**: Internal data structures shared across metrics components.

**Key Structures**:

```c
#ifndef SPOTFLOW_METRICS_TYPES_H
#define SPOTFLOW_METRICS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

/* Configuration limits */
#define SPOTFLOW_MAX_METRIC_NAME_LEN 64
#define SPOTFLOW_MAX_DIMENSION_KEY_LEN 32
#define SPOTFLOW_MAX_DIMENSION_STRING_VAL_LEN 128
#define SPOTFLOW_MAX_DIMENSIONS_PER_METRIC 16

/* Metric type enum */
typedef enum {
    SPOTFLOW_METRIC_TYPE_INT,    // Integer metric (dimensionless or dimensional)
    SPOTFLOW_METRIC_TYPE_FLOAT,  // Float metric (dimensionless or dimensional)
} spotflow_metric_type_t;

// Note: Dimensionless vs dimensional is determined by max_dimensions == 0

/* Aggregation interval enum */
typedef enum {
    SPOTFLOW_AGG_INTERVAL_NONE = 0,   // PT0S
    SPOTFLOW_AGG_INTERVAL_1MIN = 1,   // PT1M
    SPOTFLOW_AGG_INTERVAL_10MIN = 2,  // PT10M
    SPOTFLOW_AGG_INTERVAL_1HOUR = 3,  // PT1H
} spotflow_aggregation_interval_t;

/* Dimension key-value pair (from public API) */
typedef struct {
    const char* key;     // Dimension key (e.g., "core", "interface")
    const char* value;   // Dimension value (string only)
} spotflow_dimension_t;

/* Internal metric structure */
struct spotflow_metric {
    char name[SPOTFLOW_MAX_METRIC_NAME_LEN];
    spotflow_metric_type_t type;
    uint16_t max_timeseries;
    uint8_t max_dimensions;
    uint64_t sequence_number;
    spotflow_aggregation_interval_t agg_interval;
    bool collect_samples;  // Reserved for future

    struct k_mutex lock;  // Protects aggregator_context (initialized once, never destroyed)
    void* aggregator_context;  // Pointer to aggregator state

    // Statistics
    uint64_t total_reports;
    uint64_t dropped_reports;
    uint64_t transmitted_messages;
};

/**
 * Note on Mutex Lifetime:
 * - Mutexes are initialized once during metric registration via k_mutex_init()
 * - In Zephyr, k_mutex structures do not require explicit cleanup/destruction
 * - Mutexes remain valid for the lifetime of the device (until reboot)
 * - No cleanup function needed - static memory is reclaimed on reboot
 */

/* Metric registry entry (for internal use) */
struct metric_registry_entry {
    struct spotflow_metric metric;
    bool in_use;
};

#endif /* SPOTFLOW_METRICS_TYPES_H */
```

### 2. spotflow_metrics_backend.h / .c

**Purpose**: Public API implementation and metric registry management.

**Implementation Steps**:

1. **Metric Registry Initialization**:
```c
// Global registry (static array for simplicity)
static struct metric_registry_entry g_metric_registry[CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED];
static struct k_mutex g_registry_lock;
static bool g_metrics_initialized = false;

static void metrics_backend_init(void) {
    if (g_metrics_initialized) return;

    k_mutex_init(&g_registry_lock);
    memset(g_metric_registry, 0, sizeof(g_metric_registry));
    g_metrics_initialized = true;

    LOG_INF("Metrics backend initialized");
}
```

2. **Metric Registration**:
```c
// Dimensionless float metric
spotflow_metric_t* spotflow_register_metric_float(const char* name)
{
    // Validate name
    if (!name || strlen(name) == 0 || strlen(name) >= SPOTFLOW_MAX_METRIC_NAME_LEN) {
        LOG_ERR("Invalid metric name");
        return NULL;
    }

// Dimensional float metric
spotflow_metric_t* spotflow_register_metric_float_with_dimensions(
    const char* name,
    uint16_t max_timeseries,
    uint8_t max_dimensions)
{
    // Validate parameters
    if (!name || strlen(name) == 0 || strlen(name) >= SPOTFLOW_MAX_METRIC_NAME_LEN) {
        LOG_ERR("Invalid metric name");
        return NULL;
    }

    if (max_timeseries == 0 || max_timeseries > 256) {
        LOG_ERR("Invalid max_timeseries: %u", max_timeseries);
        return NULL;
    }

    if (max_dimensions == 0 || max_dimensions > SPOTFLOW_MAX_DIMENSIONS_PER_METRIC) {
        LOG_ERR("Invalid max_dimensions: %u", max_dimensions);
        return NULL;
    }

    // Normalize name to lowercase
    char normalized_name[SPOTFLOW_MAX_METRIC_NAME_LEN];
    normalize_metric_name(name, normalized_name, sizeof(normalized_name));

    // Lock registry
    k_mutex_lock(&g_registry_lock, K_FOREVER);

    // Check for duplicate
    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
        if (g_metric_registry[i].in_use &&
            strcmp(g_metric_registry[i].metric.name, normalized_name) == 0) {
            LOG_ERR("Metric '%s' already registered", normalized_name);
            k_mutex_unlock(&g_registry_lock);
            return NULL;
        }
    }

    // Find empty slot
    struct spotflow_metric* metric = NULL;
    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED; i++) {
        if (!g_metric_registry[i].in_use) {
            metric = &g_metric_registry[i].metric;
            g_metric_registry[i].in_use = true;
            break;
        }
    }

    if (!metric) {
        LOG_ERR("Maximum metrics registered (%d)", CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED);
        k_mutex_unlock(&g_registry_lock);
        return NULL;
    }

    // Initialize metric structure
    strncpy(metric->name, normalized_name, sizeof(metric->name));
    metric->type = SPOTFLOW_METRIC_TYPE_FLOAT;
    metric->max_timeseries = max_timeseries;
    metric->max_dimensions = max_dimensions;
    metric->sequence_number = 0;
    metric->agg_interval = CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL;
    // collect_samples reserved for future (not in v1)
    metric->total_reports = 0;
    metric->dropped_reports = 0;
    metric->transmitted_messages = 0;

    k_mutex_init(&metric->lock);

    // Initialize aggregator context
    int rc = aggregator_register_metric(metric);
    if (rc < 0) {
        LOG_ERR("Failed to initialize aggregator for metric '%s': %d", normalized_name, rc);
        // Rollback: mark registry slot as available if aggregator registration fails
        g_metric_registry[i].in_use = false;
        k_mutex_unlock(&g_registry_lock);
        return NULL;  // Metric NOT registered, error code propagated
    }

    k_mutex_unlock(&g_registry_lock);

    LOG_INF("Registered metric '%s' (type=float, max_ts=%u, max_dims=%u)",
            normalized_name, max_timeseries, max_dimensions);

    return metric;
}
```

3. **Metric Reporting**:
```c
// Dimensionless integer metric reporting
int spotflow_report_metric_int(
    spotflow_metric_t* metric,
    int64_t value)
{
    // Validate parameters
    if (!metric) {
        return -EINVAL;
    }

    if (metric->max_dimensions > 0) {
        LOG_ERR("Use spotflow_report_metric_int_with_dimensions for dimensional metrics");
        return -EINVAL;
    }

// Dimensionless float metric reporting
int spotflow_report_metric_float(
    spotflow_metric_t* metric,
    double value)
{
    // Validate parameters
    if (!metric) {
        return -EINVAL;
    }

    if (metric->max_dimensions > 0) {
        LOG_ERR("Use spotflow_report_metric_float_with_dimensions for dimensional metrics");
        return -EINVAL;
    }

    // Validate value (no NaN, Inf)
    if (isnan(value) || isinf(value)) {
        LOG_ERR("Invalid metric value: %f", value);
        return -EINVAL;
    }

// Dimensional integer metric reporting
int spotflow_report_metric_int_with_dimensions(
    spotflow_metric_t* metric,
    int64_t value,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count)
{
    // Validate parameters
    if (!metric || !dimensions) {
        return -EINVAL;
    }

    if (metric->max_dimensions == 0) {
        LOG_ERR("Use spotflow_report_metric_int for dimensionless metrics");
        return -EINVAL;
    }

    if (dimension_count == 0 || dimension_count > metric->max_dimensions) {
        LOG_ERR("Invalid dimension_count: %d (max: %d)", dimension_count, metric->max_dimensions);
        return -EINVAL;
    }

// Dimensional float metric reporting
int spotflow_report_metric_float_with_dimensions(
    spotflow_metric_t* metric,
    double value,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count)
{
    // Validate parameters
    if (!metric || !dimensions) {
        return -EINVAL;
    }

    if (metric->max_dimensions == 0) {
        LOG_ERR("Use spotflow_report_metric_float for dimensionless metrics");
        return -EINVAL;
    }

    if (dimension_count == 0 || dimension_count > metric->max_dimensions) {
        LOG_ERR("Invalid dimension_count: %d (max: %d)", dimension_count, metric->max_dimensions);
        return -EINVAL;
    }

    // Validate value (no NaN, Inf)
    if (isnan(value) || isinf(value)) {
        LOG_ERR("Invalid metric value: %f", value);
        return -EINVAL;
    }

    // Dispatch to aggregator
    int rc = aggregator_report_value(metric, dimensions, value);

    // Update statistics
    metric->total_reports++;
    if (rc < 0) {
        metric->dropped_reports++;
    }

    return rc;
}

// Event reporting functions (convenience wrappers)
int spotflow_report_event(spotflow_metric_t* metric)
{
    // Validate parameters
    if (!metric) {
        return -EINVAL;
    }

    if (metric->max_dimensions > 0) {
        LOG_ERR("Use spotflow_report_event_with_dimensions for dimensional metrics");
        return -EINVAL;
    }

    // Report value of 1 (event occurred)
    return spotflow_report_metric_int(metric, 1);
}

int spotflow_report_event_with_dimensions(
    spotflow_metric_t* metric,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count)
{
    // Validate parameters
    if (!metric || !dimensions) {
        return -EINVAL;
    }

    if (metric->max_dimensions == 0) {
        LOG_ERR("Use spotflow_report_event for dimensionless metrics");
        return -EINVAL;
    }

    if (dimension_count == 0 || dimension_count > metric->max_dimensions) {
        LOG_ERR("Invalid dimension_count: %d (max: %d)", dimension_count, metric->max_dimensions);
        return -EINVAL;
    }

    // Report value of 1 (event occurred)
    return spotflow_report_metric_int_with_dimensions(metric, 1, dimensions, dimension_count);
}
```

**Note**: Event functions are thin wrappers that always report value `1`. The metric should be configured with PT0S aggregation interval to ensure immediate transmission without aggregation.

**Helper Functions**:
```c
static void normalize_metric_name(const char* input, char* output, size_t output_size) {
    size_t i;
    for (i = 0; i < output_size - 1 && input[i] != '\0'; i++) {
        output[i] = tolower(input[i]);
    }
    output[i] = '\0';
}
```

### 3. spotflow_metrics_aggregator.h / .c

**Purpose**: Manage aggregation state and compute statistical summaries.

**Key Data Structures**:

```c
/* Time series state for one dimension combination */
struct metric_timeseries_state {
    uint32_t dimension_hash;  // Hash of dimension key-value pairs

    // Aggregated values
    union {
        double sum_float;
        int64_t sum_int;
    };
    uint64_t count;
    union {
        double min_float;
        int64_t min_int;
    };
    union {
        double max_float;
        int64_t max_int;
    };
    bool sum_truncated;

    uint64_t window_start_ms;  // Start of current aggregation window

    // Stored dimensions (keys and values)
    spotflow_dimension_t dimensions[SPOTFLOW_MAX_DIMENSIONS_PER_METRIC];
    uint8_t dimension_count;

    // Dimension string storage (inline to avoid dynamic allocation)
    // Each dimension gets fixed-size buffers for key and value strings
    char dimension_key_storage[SPOTFLOW_MAX_DIMENSIONS_PER_METRIC][32];
    char dimension_value_storage[SPOTFLOW_MAX_DIMENSIONS_PER_METRIC][128];

    bool active;     // Slot in use
};

/* Aggregator context per metric */
struct metric_aggregator_context {
    struct spotflow_metric* metric;
    struct metric_timeseries_state* timeseries;
    uint16_t timeseries_count;      // Current number of active time series
    uint16_t timeseries_capacity;   // Max (from metric->max_timeseries)

    // Timer scope: ONE timer per metric (not per time series)
    // All time series of this metric share the same aggregation window
    // When timer expires, all active time series generate messages with their counts
    struct k_work_delayable aggregation_work;  // Timer for window expiration
};
```

**Implementation Steps**:

1. **Aggregator Registration**:
```c
// Contract: This function MUST be atomic - either full success (returns 0)
// or full failure (returns -ENOMEM with no side effects). Caller relies on
// this to safely rollback metric registration on failure.
int aggregator_register_metric(struct spotflow_metric* metric) {
    // Allocate aggregator context
    struct metric_aggregator_context* ctx =
        k_malloc(sizeof(struct metric_aggregator_context));
    if (!ctx) {
        return -ENOMEM;
    }

    // Allocate time series array
    ctx->timeseries = k_calloc(metric->max_timeseries,
                               sizeof(struct metric_timeseries_state));
    if (!ctx->timeseries) {
        k_free(ctx);  // Clean up - maintain atomic semantics
        return -ENOMEM;
    }

    ctx->metric = metric;
    ctx->timeseries_count = 0;
    ctx->timeseries_capacity = metric->max_timeseries;

    // Initialize aggregation timer (if not PT0S)
    if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
        k_work_init_delayable(&ctx->aggregation_work, aggregation_timer_handler);
        schedule_aggregation_timer(ctx);
    }

    metric->aggregator_context = ctx;
    return 0;
}
```

2. **Value Reporting**:
```c
int aggregator_report_value(
    struct spotflow_metric* metric,
    const spotflow_dimension_t* dimensions,
    double value)
{
    struct metric_aggregator_context* ctx = metric->aggregator_context;
    if (!ctx) {
        return -EINVAL;
    }

    k_mutex_lock(&metric->lock, K_FOREVER);

    // Find or create time series for this dimension combination
    uint32_t dim_hash = hash_dimensions(dimensions, metric->max_dimensions);
    struct metric_timeseries_state* ts = find_or_create_timeseries(ctx, dim_hash, dimensions, metric->max_dimensions);

    if (!ts) {
        // Time series pool full, reject this report
        LOG_ERR("Metric '%s': time series pool full, dropping dimension combination",
                metric->name);
        k_mutex_unlock(&metric->lock);
        return -ENOSPC;
    }

    // Update aggregation state
    if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        update_aggregation_float(ts, value);
    } else {
        update_aggregation_int(ts, (int64_t)value);
    }

    ts->count++;

    k_mutex_unlock(&metric->lock);

    // If no aggregation (PT0S), trigger immediate transmission
    if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
        return flush_timeseries_immediate(metric, ts);
    }

    return 0;
}

// Helper function to find or create time series slot
static struct metric_timeseries_state* find_or_create_timeseries(
    struct metric_aggregator_context* ctx,
    uint32_t dim_hash,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count)
{
    // Performance Note: O(n) linear search is acceptable for typical use cases
    // where max_timeseries ≤ 256. For larger cardinalities, consider hash table
    // optimization in future version.

    // First, try to find existing time series with matching hash
    for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
        if (ctx->timeseries[i].active &&
            ctx->timeseries[i].dimension_hash == dim_hash) {
            // Hash match - verify full dimension comparison to handle collisions
            if (dimensions_equal(&ctx->timeseries[i], dimensions, dimension_count)) {
                return &ctx->timeseries[i];
            }
        }
    }

    // Not found - create new time series if space available
    if (ctx->timeseries_count >= ctx->timeseries_capacity) {
        return NULL;  // Pool full - return -ENOSPC
    }

    // Find first inactive slot
    for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
        if (!ctx->timeseries[i].active) {
            struct metric_timeseries_state* ts = &ctx->timeseries[i];

            // Initialize new time series
            memset(ts, 0, sizeof(*ts));
            ts->active = true;
            ts->dimension_hash = dim_hash;
            ts->dimension_count = dimension_count;

            // Copy dimension strings into inline storage
            for (uint8_t d = 0; d < dimension_count; d++) {
                strncpy(ts->dimension_key_storage[d], dimensions[d].key, 31);
                ts->dimension_key_storage[d][31] = '\0';

                strncpy(ts->dimension_value_storage[d], dimensions[d].value, 127);
                ts->dimension_value_storage[d][127] = '\0';

                // Point dimension struct to inline storage
                ts->dimensions[d].key = ts->dimension_key_storage[d];
                ts->dimensions[d].value = ts->dimension_value_storage[d];
            }

            ctx->timeseries_count++;
            return ts;
        }
    }

    return NULL;  // Should never reach here
}

// Helper to compare dimensions for collision detection
static bool dimensions_equal(
    const struct metric_timeseries_state* ts,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count)
{
    if (ts->dimension_count != dimension_count) {
        return false;
    }

    for (uint8_t i = 0; i < dimension_count; i++) {
        if (strcmp(ts->dimensions[i].key, dimensions[i].key) != 0 ||
            strcmp(ts->dimensions[i].value, dimensions[i].value) != 0) {
            return false;
        }
    }

    return true;
}
```

3. **Aggregation Logic**:
```c
static void update_aggregation_float(struct metric_timeseries_state* ts, double value) {
    if (ts->count == 0) {
        // First value in window
        ts->sum_float = value;
        ts->min_float = value;
        ts->max_float = value;
        ts->window_start_ms = k_uptime_get();
    } else {
        // Accumulate
        double new_sum = ts->sum_float + value;

        // Check for overflow (simplified check)
        if (new_sum < ts->sum_float) {
            ts->sum_truncated = true;
        } else {
            ts->sum_float = new_sum;
        }

        if (value < ts->min_float) ts->min_float = value;
        if (value > ts->max_float) ts->max_float = value;
    }
}

static void update_aggregation_int(struct metric_timeseries_state* ts, int64_t value) {
    if (ts->count == 0) {
        ts->sum_int = value;
        ts->min_int = value;
        ts->max_int = value;
        ts->window_start_ms = k_uptime_get();
    } else {
        int64_t new_sum = ts->sum_int + value;

        // Check for overflow
        if ((value > 0 && new_sum < ts->sum_int) ||
            (value < 0 && new_sum > ts->sum_int)) {
            ts->sum_truncated = true;
        } else {
            ts->sum_int = new_sum;
        }

        if (value < ts->min_int) ts->min_int = value;
        if (value > ts->max_int) ts->max_int = value;
    }
}
```

4. **Timer Handler (Window Expiration)**:
```c
static void aggregation_timer_handler(struct k_work* work) {
    struct k_work_delayable* dwork = k_work_delayable_from_work(work);
    struct metric_aggregator_context* ctx =
        CONTAINER_OF(dwork, struct metric_aggregator_context, aggregation_work);

    struct spotflow_metric* metric = ctx->metric;

    k_mutex_lock(&metric->lock, K_FOREVER);

    // Encode and queue all active time series
    int rc = encode_and_queue_metric_message(metric, ctx);
    if (rc < 0) {
        LOG_WRN("Failed to encode metric message: %d", rc);
    }

    // Reset aggregation state
    reset_all_timeseries(ctx);

    k_mutex_unlock(&metric->lock);

    // Reschedule timer
    schedule_aggregation_timer(ctx);
}

static void schedule_aggregation_timer(struct metric_aggregator_context* ctx) {
    k_timeout_t delay;

    switch (ctx->metric->agg_interval) {
    case SPOTFLOW_AGG_INTERVAL_1MIN:
        delay = K_MINUTES(1);
        break;
    case SPOTFLOW_AGG_INTERVAL_10MIN:
        delay = K_MINUTES(10);
        break;
    case SPOTFLOW_AGG_INTERVAL_1HOUR:
        delay = K_HOURS(1);
        break;
    default:
        return;  // No timer for PT0S
    }

    k_work_schedule(&ctx->aggregation_work, delay);
}
```

5. **Dimension Hashing**:
```c
static uint32_t hash_dimensions(const spotflow_dimension_t* dimensions, uint8_t count) {
    if (!dimensions || count == 0) {
        return 0;  // Dimensionless metric
    }

    // FNV-1a hash
    uint32_t hash = 2166136261u;

    for (uint8_t i = 0; i < count; i++) {
        // Hash key
        const char* key = dimensions[i].key;
        while (*key) {
            hash ^= (uint32_t)*key++;
            hash *= 16777619u;
        }

        // Hash value (string only)
        const char* val = dimensions[i].value;
        while (*val) {
            hash ^= (uint32_t)*val++;
            hash *= 16777619u;
        }
    }

    return hash;
}
```

### 4. spotflow_metrics_cbor.h / .c

**Purpose**: Encode metric messages to CBOR format.

**Implementation Steps**:

1. **Encode Simple Metric**:
```c
int spotflow_metrics_cbor_encode_simple(
    struct spotflow_metric* metric,
    struct metric_timeseries_state* ts,
    uint8_t** cbor_data,
    size_t* cbor_len)
{
    uint8_t buffer[CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE];
    ZCBOR_STATE_E(state, 1, buffer, sizeof(buffer), 1);

    bool succ;
    int field_count = 8;  // messageType, metricName, aggInterval, uptime, seq, sum, count, min, max

    succ = zcbor_map_start_encode(state, field_count);

    // messageType
    succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
    succ = succ && zcbor_uint32_put(state, METRIC_MESSAGE_TYPE);

    // metricName
    succ = succ && zcbor_uint32_put(state, KEY_METRIC_NAME);
    succ = succ && zcbor_tstr_put_term(state, metric->name);

    // aggregationInterval
    succ = succ && zcbor_uint32_put(state, KEY_AGGREGATION_INTERVAL);
    succ = succ && zcbor_uint32_put(state, metric->agg_interval);

    // deviceUptimeMs
    succ = succ && zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
    succ = succ && zcbor_uint64_put(state, k_uptime_get());

    // sequenceNumber (per-metric, incremented for EACH message)
    // NOTE: For dimensional metrics with N active time series, sequence number
    // advances by N per aggregation interval (one increment per message)
    succ = succ && zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER);
    succ = succ && zcbor_uint64_put(state, metric->sequence_number++);

    // sum
    succ = succ && zcbor_uint32_put(state, KEY_SUM);
    if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        succ = succ && zcbor_float64_put(state, ts->sum_float);
    } else {
        succ = succ && zcbor_int64_put(state, ts->sum_int);
    }

    // sumTruncated (only if true)
    if (ts->sum_truncated) {
        succ = succ && zcbor_uint32_put(state, KEY_SUM_TRUNCATED);
        succ = succ && zcbor_bool_put(state, true);
    }

    // count (only for aggregated metrics)
    if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
        succ = succ && zcbor_uint32_put(state, KEY_COUNT);
        succ = succ && zcbor_uint64_put(state, ts->count);

        // min
        succ = succ && zcbor_uint32_put(state, KEY_MIN);
        if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
            succ = succ && zcbor_float64_put(state, ts->min_float);
        } else {
            succ = succ && zcbor_int64_put(state, ts->min_int);
        }

        // max
        succ = succ && zcbor_uint32_put(state, KEY_MAX);
        if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
            succ = succ && zcbor_float64_put(state, ts->max_float);
        } else {
            succ = succ && zcbor_int64_put(state, ts->max_int);
        }
    }

    succ = succ && zcbor_map_end_encode(state, field_count);

    if (!succ) {
        LOG_ERR("CBOR encoding failed: %d", zcbor_peek_error(state));
        return -EINVAL;
    }

    // Memory Ownership Contract:
    // 1. Encoder allocates and returns pointer to caller
    // 2. Caller enqueues pointer to message queue
    // 3. If enqueue fails: caller MUST free immediately
    // 4. If enqueue succeeds: ownership transfers to processor thread
    // 5. Processor thread ALWAYS frees (success or failure)

    // Allocate and copy
    size_t encoded_len = state->payload - buffer;
    uint8_t* data = k_malloc(encoded_len);
    if (!data) {
        return -ENOMEM;
    }

    memcpy(data, buffer, encoded_len);
    *cbor_data = data;
    *cbor_len = encoded_len;

    return 0;
}
```

2. **Encode Dimensional Metric (with labels)**:
```c
static int encode_labels(zcbor_state_t* state, const spotflow_dimension_t* dims, uint8_t count) {
    bool succ = zcbor_uint32_put(state, KEY_LABELS);
    succ = succ && zcbor_map_start_encode(state, count);

    for (uint8_t i = 0; i < count; i++) {
        succ = succ && zcbor_tstr_put_term(state, dims[i].key);
        succ = succ && zcbor_tstr_put_term(state, dims[i].value);
    }

    succ = succ && zcbor_map_end_encode(state, count);
    return succ ? 0 : -EINVAL;
}
```

### 5. spotflow_metrics_net.h / .c

**Purpose**: Network polling and MQTT transmission.

**Implementation**:

```c
// Message queue
K_MSGQ_DEFINE(g_spotflow_metrics_msgq,
              sizeof(struct spotflow_mqtt_metrics_msg*),
              CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE,
              1);

struct spotflow_mqtt_metrics_msg {
    uint8_t* payload;
    size_t len;
};

void spotflow_metrics_net_init(void) {
    LOG_DBG("Metrics network layer initialized");
}

int spotflow_poll_and_process_enqueued_metrics(void) {
    struct spotflow_mqtt_metrics_msg* msg;

    int rc = k_msgq_get(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT);
    if (rc != 0) {
        return 0;  // No message available
    }

    // Memory Ownership: Processor thread ALWAYS frees message memory
    // (success or failure) after dequeue completes

    // Infinite retry loop for transient errors (preserves message ordering)
    do {
        rc = spotflow_mqtt_publish_ingest_cbor_msg(msg->payload, msg->len);
        if (rc == -EAGAIN) {
            LOG_DBG("MQTT busy, retrying...");
            k_sleep(K_MSEC(10));  // Small delay before retry
            // Continue loop - do NOT requeue (would break ordering)
        }
    } while (rc == -EAGAIN);

    // ALWAYS free message memory (success or permanent failure)
    k_free(msg->payload);
    k_free(msg);

    if (rc < 0) {
        LOG_WRN("Failed to publish metric message: %d", rc);
        // Permanent error - message lost
        return rc;
    }

    return 1;  // Processed one message
}
```

**Integration with Processor** (modify `spotflow_processor.c`):

```c
// In process_config_coredumps_or_logs():
#ifdef CONFIG_SPOTFLOW_METRICS
    if (rc == 0) {
        // No coredumps to send -> try metrics
        rc = spotflow_poll_and_process_enqueued_metrics();
        if (rc < 0) {
            LOG_DBG("Failed to process metrics: %d", rc);
            return rc;
        }
    }
#endif
```

### 6. KConfig

**File**: `modules/lib/spotflow/zephyr/src/metrics/KConfig`

```kconfig
config SPOTFLOW_METRICS
    bool "Enable Spotflow metrics collection and reporting"
    default n
    depends on SPOTFLOW
    help
        Enable metrics collection and reporting to Spotflow cloud.
        Metrics provide time-series data for monitoring device health,
        performance, and application-specific measurements.

if SPOTFLOW_METRICS

config SPOTFLOW_METRICS_QUEUE_SIZE
    int "Size of metrics message queue"
    default 16
    range 4 64
    help
        Size of the queue used to buffer metric messages before transmission.
        Increase if metrics are dropped due to queue overflow.

config SPOTFLOW_METRICS_CBOR_BUFFER_SIZE
    int "Size of CBOR encoding buffer"
    default 512
    range 128 2048
    help
        Size of the buffer used for CBOR encoding of metric messages.
        Increase if you have metrics with many dimensions or long names.

config SPOTFLOW_METRICS_MAX_REGISTERED
    int "Maximum number of registered metrics"
    default 32
    range 4 128
    help
        Maximum number of metrics that can be registered simultaneously.
        Each registered metric consumes ~128 bytes of RAM.

config SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL
    int "Default aggregation interval"
    default 1
    range 0 3
    help
        Default aggregation interval for newly registered metrics.
        0 = PT0S (no aggregation), 1 = PT1M, 2 = PT10M, 3 = PT1H

config SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL
    int "Metrics subsystem log level"
    default SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL
    range 0 4
    help
        Log level for metrics components. Defaults to module default.

config HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS
    int "Additional heap size for metrics"
    default 8192
    help
        Additional heap memory required by metrics subsystem.
        Includes message queue, aggregation state, and CBOR buffers.

endif # SPOTFLOW_METRICS
```

### 7. CMakeLists.txt

**File**: `modules/lib/spotflow/zephyr/src/metrics/CMakeLists.txt`

```cmake
if(NOT CONFIG_SPOTFLOW_METRICS)
    return()
endif()

zephyr_library()

zephyr_library_sources(
    spotflow_metrics_backend.c
    spotflow_metrics_aggregator.c
    spotflow_metrics_cbor.c
    spotflow_metrics_net.c
)

zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
```

**Update parent CMakeLists.txt** (`modules/lib/spotflow/zephyr/CMakeLists.txt`):
```cmake
add_subdirectory(src/metrics)
```

**Update parent KConfig** (`modules/lib/spotflow/zephyr/Kconfig`):
```kconfig
rsource "src/metrics/KConfig"
```

## Testing Strategy

### Unit Tests

**Location**: `modules/lib/spotflow/zephyr/tests/metrics/`

**Test Cases**:

1. **Registry Tests**:
   - Test metric registration (valid, invalid params)
   - Test duplicate name rejection
   - Test registry limit enforcement
   - Test name normalization

2. **Aggregation Tests**:
   - Test single value aggregation
   - Test multi-value aggregation (sum, count, min, max)
   - Test sum overflow detection (sumTruncated flag)
   - Test dimension hashing
   - Test cardinality limit enforcement (pool full returns -ENOSPC)
   - Test error logging when pool full

3. **CBOR Encoding Tests**:
   - Test simple metric encoding
   - Test dimensional metric encoding
   - Test PT0S (event) encoding (no min/max)
   - Test decode and verify roundtrip

4. **Network Tests**:
   - Test message queueing
   - Test queue overflow handling
   - Test MQTT transmission (mocked)

**Test Framework**: Zephyr ztest

**Example Test**:
```c
#include <zephyr/ztest.h>
#include "spotflow_metrics.h"

ZTEST(metrics_tests, test_register_simple_metric)
{
    spotflow_metric_t* metric = spotflow_register_metric_int("test_counter");
    zassert_not_null(metric, "Metric registration failed");

    // Test reporting
    int rc = spotflow_report_metric_int(metric, 42);
    zassert_equal(rc, 0, "Metric report failed: %d", rc);
}

ZTEST(metrics_tests, test_duplicate_registration)
{
    spotflow_metric_t* m1 = spotflow_register_metric_int("duplicate");
    zassert_not_null(m1, "First registration failed");

    spotflow_metric_t* m2 = spotflow_register_metric_int("duplicate");
    zassert_is_null(m2, "Duplicate registration should fail");
}
```

### Integration Tests

**Location**: `modules/lib/spotflow/zephyr/tests/integration/metrics/`

**Test Scenarios**:

1. **End-to-End**: Register, report, aggregate, encode, queue, transmit
2. **Multi-Threaded**: Concurrent metric reports from multiple threads
3. **High Load**: Sustained high metric report rate
4. **Network Failures**: MQTT disconnection and reconnection
5. **Long Duration**: Run for 24 hours to detect leaks

## Common Implementation Pitfalls

### 1. Thread Safety

**Problem**: Race conditions in aggregation state updates.

**Solution**:
- Use per-metric mutex, not global lock
- Lock only during aggregation update, not encoding
- Use atomic operations for statistics counters

### 2. Memory Leaks

**Problem**: Forgetting to free CBOR-encoded messages after transmission.

**Solution**:
- Always pair k_malloc with k_free
- Use RAII-like patterns (init/cleanup functions)
- Test with long-duration runs

### 3. Buffer Overflows

**Problem**: CBOR encoding exceeds buffer size.

**Solution**:
- Validate buffer size before encoding
- Return error if insufficient space
- Provide clear error messages with required size

### 4. Hash Collisions

**Problem**: Different dimension combinations produce same hash.

**Solution**:
- Use full comparison in addition to hash for matching
- Log warning if collision detected
- Consider using better hash function if collisions frequent

### 5. Timer Overhead

**Problem**: Too many timers created for metrics.

**Solution**:
- Use single work queue for all timers
- Consider coalescing timers with same interval
- Profile timer overhead during testing

## Debugging Tips

### Enable Verbose Logging

```kconfig
CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL=4  # DEBUG
```

### Inspect CBOR Messages

Use `cbor.me` or Python:
```python
import cbor2
data = bytes.fromhex("A800...")  # From log
decoded = cbor2.loads(data)
print(decoded)
```

### Monitor Queue Depth

Add debug logging in queue operations:
```c
LOG_DBG("Metrics queue: %u/%u used", k_msgq_num_used(&g_spotflow_metrics_msgq),
        CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE);
```

### Track Memory Usage

```c
// Periodically log heap stats
struct sys_memory_stats stats;
sys_heap_runtime_stats_get(&_system_heap, &stats);
LOG_INF("Heap: %zu bytes allocated, %zu free",
        stats.allocated_bytes, stats.free_bytes);
```

## Code Review Checklist

Before submitting for review, verify:

- [ ] All public APIs have documentation comments
- [ ] Error codes are consistent with Zephyr conventions
- [ ] No memory leaks (verified with long tests)
- [ ] Thread safety verified (no data races)
- [ ] All error paths tested
- [ ] Kconfig help text is clear and complete
- [ ] CMakeLists.txt integrates correctly
- [ ] Code follows Spotflow SDK style (.clang-format applied)
- [ ] Unit tests pass with 100% coverage
- [ ] Integration tests pass
- [ ] Performance benchmarks meet targets
- [ ] No compiler warnings
- [ ] Documentation is complete and accurate

## Deployment and Rollout

### Alpha Release (Internal Testing)

- Deploy to test devices
- Monitor cloud ingestion
- Validate message format
- Collect performance metrics
- Fix critical bugs

### Beta Release (Early Adopters)

- Release to selected customers
- Gather feedback on API usability
- Monitor production metrics
- Iterate on documentation
- Performance tuning

### General Availability

- Announce feature in release notes
- Update SDK documentation
- Provide migration guide (N/A for new feature)
- Monitor adoption metrics
- Plan future enhancements

## Future Enhancements

Ideas for future iterations:

1. **Sample Collection**: Store individual values for histograms
2. **Metric Filtering**: Enable/disable metrics from cloud
3. **Custom Aggregation Functions**: User-defined aggregation logic
4. **Prometheus Export**: Support Prometheus exposition format
5. **Local Querying**: On-device metric value retrieval
6. **Persistent Metrics**: Survive device reboots
7. **Metric Compression**: Further reduce bandwidth

## Conclusion

This implementation guide provides a roadmap for building the Spotflow metrics feature. Follow the phased approach, implement comprehensive tests, and adhere to the architecture and API specifications. The result will be a robust, efficient metrics system that enhances the Spotflow observability platform for embedded devices.

For questions or clarifications during implementation, refer to:
- Architecture Specification: `architecture.md`
- API Specification: `api_specification.md`
- Ingestion Protocol: `ingestion_protocol_specification.md`
- PO Requirements: `specification/api.md` and `specification/ingestion_protocol.md`

Good luck with the implementation!
