# Spotflow SDK Metrics API Specification

## Overview

This document defines the public API for the Spotflow SDK metrics collection feature. The API is designed for embedded C applications running on Zephyr RTOS and follows established SDK patterns for consistency and ease of use.

**Design Principles**:
- Simple and intuitive for common use cases
- Minimal performance overhead on reporting path
- Clear error reporting
- Thread-safe by default
- Consistent with Zephyr and Spotflow SDK conventions

## API Header

**File**: `spotflow_metrics.h`

**Location**: `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics.h`

**Usage**:
```c
#include "spotflow_metrics.h"  // For SDK-internal usage
// OR
#include <metrics/spotflow_metrics.h>  // For application usage (if exported)
```

## Data Types

### Metric Handle

```c
/**
 * @brief Opaque handle to a registered metric.
 *
 * This handle is returned by metric registration functions and must be
 * passed to metric reporting functions. The handle remains valid for the
 * lifetime of the application.
 *
 * @note Do not access or modify the contents of this structure directly.
 */
typedef struct spotflow_metric* spotflow_metric_t;
```

### Dimension Key-Value Pair

```c
/**
 * @brief Represents a single dimension (label) key-value pair.
 *
 * Dimensions identify specific time series within a metric. For example,
 * a "cpu_temperature" metric might have a "core" dimension to distinguish
 * between different CPU cores.
 *
 * @note Keys must be null-terminated strings and remain valid for the
 *       duration of the spotflow_report_metric_*() call.
 * @note Values must be null-terminated strings and remain valid for the
 *       duration of the spotflow_report_metric_*() call.
 */
typedef struct {
    /** Dimension key (e.g., "core", "interface", "zone") */
    const char* key;
    /** Dimension value (null-terminated string) */
    const char* value;
} spotflow_dimension_t;
```

### Aggregation Intervals

```c
/**
 * @brief Aggregation interval options.
 *
 * Determines how metric values are aggregated before transmission.
 * Longer intervals reduce network traffic but increase latency.
 */
typedef enum {
    /** No aggregation - enqueue each value immediately (non-blocking) */
    SPOTFLOW_AGG_INTERVAL_NONE = 0,   // PT0S
    /** Aggregate over 1 minute */
    SPOTFLOW_AGG_INTERVAL_1MIN = 1,    // PT1M
    /** Aggregate over 10 minutes */
    SPOTFLOW_AGG_INTERVAL_10MIN = 2,   // PT10M
    /** Aggregate over 1 hour */
    SPOTFLOW_AGG_INTERVAL_1HOUR = 3,   // PT1H
} spotflow_aggregation_interval_t;
```

## Registration Functions

### Register Float Metric (Dimensionless)

```c
/**
 * @brief Register a floating-point metric without dimensions.
 *
 * Registers a simple metric that reports floating-point values without
 * any dimensional labels. Use this for straightforward counters, gauges,
 * or measurements that don't vary by any dimension.
 *
 * @param name Metric name (null-terminated string). Must be unique across
 *             all registered metrics. Metric names are case-insensitive and
 *             normalized to lowercase. Maximum length: 64 characters.
 *
 * @return Pointer to metric handle on success, NULL on failure.
 *
 * @retval NULL if registration fails due to:
 *         - Invalid parameters (name is NULL/empty)
 *         - Out of memory
 *         - Maximum number of registered metrics exceeded
 *         - Duplicate metric name
 *
 * @note This function should be called during application initialization.
 * @note Registration is thread-safe but may block briefly.
 * @note The metric handle remains valid until device reboot. No explicit cleanup needed.
 * @note Metrics are stored in static memory - handles persist across sleep/wake cycles.
 *
 * @warning Do not register metrics in time-critical code paths.
 *
 * Example:
 * @code
 * spotflow_metric_t* uptime_metric =
 *     spotflow_register_metric_float("app_uptime_seconds");
 * if (uptime_metric == NULL) {
 *     LOG_ERR("Failed to register uptime metric");
 * }
 * @endcode
 */
spotflow_metric_t* spotflow_register_metric_float(const char* name);
```

### Register Integer Metric (Dimensionless)

```c
/**
 * @brief Register an integer metric without dimensions.
 *
 * Registers a simple metric that reports integer values without
 * any dimensional labels. Integer metrics are more efficient than
 * float metrics and should be preferred when fractional values are
 * not needed (e.g., counters, byte counts, event counts).
 *
 * @param name Metric name (null-terminated string). Must be unique across
 *             all registered metrics. Metric names are case-insensitive and
 *             normalized to lowercase. Maximum length: 64 characters.
 *
 * @return Pointer to metric handle on success, NULL on failure.
 *
 * @retval NULL if registration fails due to:
 *         - Invalid parameters (name is NULL/empty)
 *         - Out of memory
 *         - Maximum number of registered metrics exceeded
 *         - Duplicate metric name
 *
 * @note All other semantics identical to spotflow_register_metric_float().
 *
 * Example:
 * @code
 * spotflow_metric_t* requests_metric =
 *     spotflow_register_metric_int("http_requests_total");
 * @endcode
 */
spotflow_metric_t* spotflow_register_metric_int(const char* name);
```

### Register Float Metric with Dimensions

```c
/**
 * @brief Register a floating-point metric with support for dimensions.
 *
 * Registers a dimensional metric that reports floating-point values labeled
 * with dimensions (also called labels or tags). Each unique combination of
 * dimension values creates a separate time series.
 *
 * Function naming: "_with_dimensions" indicates this metric SUPPORTS dimensions
 * (vs dimensionless metrics). When reporting values, dimensions are REQUIRED.
 *
 * @param name Metric name (null-terminated string). Must be unique across
 *             all registered metrics. Metric names are case-insensitive and
 *             normalized to lowercase. Maximum length: 64 characters.
 * @param max_timeseries Maximum number of concurrent time series (unique
 *                       dimension combinations) to track. System allocates
 *                       exactly max_timeseries slots.
 *                       When exceeded, additional combinations are rejected
 *                       with error log and -ENOSPC return code.
 *                       Valid range: 1-256. Typical: 4-20.
 * @param max_dimensions Maximum number of dimensions per metric report.
 *                       Valid range: 1-16. Typical: 1-4.
 *
 * @return Pointer to metric handle on success, NULL on failure.
 *
 * @retval NULL if registration fails due to:
 *         - Invalid parameters (name is NULL/empty, limits out of range)
 *         - Out of memory
 *         - Maximum number of registered metrics exceeded
 *         - Duplicate metric name
 *
 * @note This function should be called during application initialization.
 * @note Registration is thread-safe but may block briefly.
 * @note The metric handle remains valid until device reboot. No explicit cleanup needed.
 * @note Metrics are stored in static memory - handles persist across sleep/wake cycles.
 *
 * @warning Do not register metrics in time-critical code paths.
 *
 * Example:
 * @code
 * spotflow_metric_t* temp_metric = spotflow_register_metric_float_with_dimensions(
 *     "cpu_temperature_celsius",
 *     4,      // Track up to 4 CPU cores
 *     1       // One dimension: "core"
 * );
 * if (temp_metric == NULL) {
 *     LOG_ERR("Failed to register temperature metric");
 * }
 * @endcode
 */
spotflow_metric_t* spotflow_register_metric_float_with_dimensions(
    const char* name,
    uint16_t max_timeseries,
    uint8_t max_dimensions
);
```

### Register Integer Metric with Dimensions

```c
/**
 * @brief Register an integer metric with support for dimensions.
 *
 * Similar to spotflow_register_metric_float_with_dimensions() but for integer values.
 *
 * Function naming: "_with_dimensions" indicates this metric SUPPORTS dimensions
 * (vs dimensionless metrics). When reporting values, dimensions are REQUIRED.
 *
 * Integer metrics are more efficient and should be preferred when
 * fractional values are not needed (e.g., counters, byte counts).
 *
 * @param name Metric name (null-terminated string). See
 *             spotflow_register_metric_float_with_dimensions() for details.
 * @param max_timeseries Maximum concurrent time series. See
 *                       spotflow_register_metric_float_with_dimensions() for details.
 * @param max_dimensions Maximum dimensions per report. See
 *                       spotflow_register_metric_float_with_dimensions() for details.
 *
 * @return Pointer to metric handle on success, NULL on failure.
 *
 * @note All other semantics identical to spotflow_register_metric_float_with_dimensions().
 *
 * Example:
 * @code
 * spotflow_metric_t* bytes_metric = spotflow_register_metric_int_with_dimensions(
 *     "network_bytes_total",
 *     8,      // 2 interfaces × 2 directions × 2 protocols
 *     3       // Dimensions: interface, direction, protocol
 * );
 * @endcode
 */
spotflow_metric_t* spotflow_register_metric_int_with_dimensions(
    const char* name,
    uint16_t max_timeseries,
    uint8_t max_dimensions
);
```

## Reporting Functions

### Report Integer Metric Value (Dimensionless)

```c
/**
 * @brief Report an integer value for a dimensionless metric.
 *
 * Records an integer metric measurement for metrics registered without dimensions.
 * The value is aggregated according to the metric's configuration.
 *
 * @param metric Metric handle returned by a registration function.
 *               Must not be NULL. Must be a dimensionless metric.
 * @param value Integer value to report. If the metric was registered as float,
 *              the value is converted to double. If registered as int, stored as-is.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Value successfully recorded for aggregation/transmission.
 * @retval -EINVAL Invalid parameters:
 *                 - metric is NULL
 *                 - metric is dimensional (use spotflow_report_metric_int_with_dimensions)
 * @retval -ENOMEM Metric queue is full. The metric value is dropped.
 *
 * @note This function is thread-safe and may be called from any context.
 * @note Performance: Typical execution time < 50 microseconds.
 *
 * Example:
 * @code
 * int64_t request_count = 42;
 * int rc = spotflow_report_metric_int(requests_metric, request_count);
 * if (rc < 0) {
 *     LOG_ERR("Failed to report metric: %d", rc);
 * }
 * @endcode
 */
int spotflow_report_metric_int(
    spotflow_metric_t* metric,
    int64_t value
);
```

### Report Float Metric Value (Dimensionless)

```c
/**
 * @brief Report a floating-point value for a dimensionless metric.
 *
 * Records a float metric measurement for metrics registered without dimensions.
 * The value is aggregated according to the metric's configuration.
 *
 * @param metric Metric handle returned by a registration function.
 *               Must not be NULL. Must be a dimensionless metric.
 * @param value Float value to report. Type conversion rules:
 *              - If metric registered as INT: value truncated to int64_t (e.g., 42.7 → 42)
 *              - If metric registered as FLOAT: value stored as-is (double precision)
 *              - Special values (NaN, Inf) are rejected with -EINVAL
 *              - Overflow during aggregation is detected and flagged (sumTruncated=true)
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Value successfully recorded for aggregation/transmission.
 * @retval -EINVAL Invalid parameters:
 *                 - metric is NULL
 *                 - metric is dimensional (use spotflow_report_metric_float_with_dimensions)
 *                 - value is NaN or Inf
 * @retval -ENOMEM Metric queue is full. The metric value is dropped.
 *
 * @note This function is thread-safe and may be called from any context.
 * @note Performance: Typical execution time < 50 microseconds.
 *
 * Example:
 * @code
 * double uptime_sec = k_uptime_get() / 1000.0;
 * int rc = spotflow_report_metric_float(uptime_metric, uptime_sec);
 * if (rc < 0) {
 *     LOG_ERR("Failed to report metric: %d", rc);
 * }
 * @endcode
 */
int spotflow_report_metric_float(
    spotflow_metric_t* metric,
    double value
);
```

### Report Integer Metric Value with Dimensions

```c
/**
 * @brief Report an integer value with dimensions for a dimensional metric.
 *
 * Records an integer metric measurement with dimensional labels.
 *
 * @param metric Metric handle returned by a registration function.
 *               Must not be NULL. Must be a dimensional metric.
 * @param value Integer value to report. If the metric was registered as float,
 *              the value is converted to double. If registered as int, stored as-is.
 * @param dimensions Array of dimension key-value pairs. Must not be NULL.
 * @param dimension_count Number of dimensions in the array. Must be > 0
 *                        and <= max_dimensions specified at registration.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Value successfully recorded for aggregation/transmission.
 * @retval -EINVAL Invalid parameters
 * @retval -ENOMEM Metric queue is full.
 * @retval -ENOSPC Time series pool is full.
 *
 * @note Dimension strings are hashed/copied internally.
 *
 * Example:
 * @code
 * spotflow_dimension_t dims[] = {{ .key = "core", .value = "0" }};
 * int64_t cycles = 1234567;
 * int rc = spotflow_report_metric_int_with_dimensions(cycles_metric, cycles, dims, 1);
 * @endcode
 */
int spotflow_report_metric_int_with_dimensions(
    spotflow_metric_t* metric,
    int64_t value,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count
);
```

### Report Float Metric Value with Dimensions

```c
/**
 * @brief Report a floating-point value with dimensions for a dimensional metric.
 *
 * Records a float metric measurement with dimensional labels.
 *
 * @param metric Metric handle returned by a registration function.
 *               Must not be NULL. Must be a dimensional metric.
 * @param value Float value to report. Type conversion rules:
 *              - If metric registered as INT: value truncated to int64_t (e.g., 42.7 → 42)
 *              - If metric registered as FLOAT: value stored as-is (double precision)
 *              - Special values (NaN, Inf) are rejected with -EINVAL
 *              - Overflow during aggregation is detected and flagged (sumTruncated=true)
 * @param dimensions Array of dimension key-value pairs. Must not be NULL.
 * @param dimension_count Number of dimensions in the array. Must be > 0
 *                        and <= max_dimensions specified at registration.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Value successfully recorded for aggregation/transmission.
 * @retval -EINVAL Invalid parameters (including NaN/Inf)
 * @retval -ENOMEM Metric queue is full.
 * @retval -ENOSPC Time series pool is full.
 *
 * @note Dimension strings are hashed/copied internally.
 *
 * Example:
 * @code
 * spotflow_dimension_t dims[] = {{ .key = "core", .value = "0" }};
 * double temp = read_cpu_temp(0);
 * int rc = spotflow_report_metric_float_with_dimensions(temp_metric, temp, dims, 1);
 * @endcode
 */
int spotflow_report_metric_float_with_dimensions(
    spotflow_metric_t* metric,
    double value,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count
);
```

### Report Event (Dimensionless)

```c
/**
 * @brief Report an event for a dimensionless metric.
 *
 * Events are point-in-time occurrences that are not aggregated over time.
 * This function is equivalent to calling spotflow_report_metric_int(metric, 1)
 * with PT0S aggregation interval. The value is always 1 (event occurred).
 *
 * Use events for occurrences like boot events, errors, alerts, or state
 * transitions that should be transmitted individually.
 *
 * @param metric Metric handle returned by a registration function.
 *               Must not be NULL. Must be a dimensionless metric.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Event successfully enqueued for transmission.
 * @retval -EINVAL Invalid parameters:
 *                 - metric is NULL
 *                 - metric is dimensional (use spotflow_report_event_with_dimensions)
 * @retval -ENOMEM Metric queue is full. The event is dropped.
 *
 * @note Events bypass aggregation and are enqueued immediately (non-blocking).
 * @note Events are transmitted with aggregationInterval = PT0S.
 * @note Do not use events for high-frequency occurrences (> 1 Hz sustained).
 *       Use regular metrics with aggregation instead.
 *
 * Example:
 * @code
 * // Report a simple boot event
 * int rc = spotflow_report_event(boot_event_metric);
 * if (rc < 0) {
 *     LOG_WRN("Failed to report boot event: %d", rc);
 * }
 * @endcode
 */
int spotflow_report_event(spotflow_metric_t* metric);
```

### Report Event with Dimensions

```c
/**
 * @brief Report an event with dimensions for a dimensional metric.
 *
 * Events are point-in-time occurrences that are not aggregated over time.
 * This function is equivalent to calling spotflow_report_metric_int_with_dimensions(metric, 1, ...)
 * with PT0S aggregation interval. The value is always 1 (event occurred).
 *
 * Use events for occurrences like boot events, errors, alerts, or state
 * transitions that need contextual information via dimensions.
 *
 * @param metric Metric handle returned by a registration function.
 *               Must not be NULL. Must be a dimensional metric.
 * @param dimensions Array of dimension key-value pairs. Must not be NULL.
 *                   Provides context about the event occurrence.
 * @param dimension_count Number of dimensions in the array. Must be > 0
 *                        and <= max_dimensions specified at registration.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Event successfully enqueued for transmission.
 * @retval -EINVAL Invalid parameters:
 *                 - metric is NULL
 *                 - metric is dimensionless (use spotflow_report_event)
 *                 - dimensions is NULL
 *                 - dimension_count is 0 or exceeds max_dimensions
 *                 - invalid dimension key or value (NULL, empty string)
 * @retval -ENOMEM Metric queue is full. The event is dropped.
 * @retval -ENOSPC Time series pool is full. Cannot track this dimension
 *                 combination. The event is dropped.
 *
 * @note Events bypass aggregation and are enqueued immediately (non-blocking).
 * @note Events are transmitted with aggregationInterval = PT0S.
 * @note Dimension strings are hashed/copied internally.
 * @note Do not use events for high-frequency occurrences (> 1 Hz sustained).
 *
 * Example:
 * @code
 * spotflow_dimension_t dims[] = {
 *     { .key = "reason", .value = "power_on" },
 *     { .key = "firmware_version", .value = "1.2.3" }
 * };
 *
 * int rc = spotflow_report_event_with_dimensions(
 *     boot_event_metric,
 *     dims,
 *     2  // dimension_count
 * );
 * if (rc < 0) {
 *     LOG_WRN("Failed to report boot event: %d", rc);
 * }
 * @endcode
 */
int spotflow_report_event_with_dimensions(
    spotflow_metric_t* metric,
    const spotflow_dimension_t* dimensions,
    uint8_t dimension_count
);
```

## Configuration Functions (Optional Advanced API)

### Set Metric Aggregation Interval

```c
/**
 * @brief Set or change the aggregation interval for a metric.
 *
 * By default, metrics use the aggregation interval specified by
 * CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL. This function
 * allows runtime override on a per-metric basis.
 *
 * @param metric Metric handle. Must not be NULL.
 * @param interval Desired aggregation interval.
 *
 * @return 0 on success, negative error code on failure.
 *
 * @retval 0 Aggregation interval updated successfully.
 * @retval -EINVAL Invalid metric handle or interval value.
 * @retval -EBUSY Cannot change interval while aggregation is in progress
 *                (rare - retry after a few milliseconds).
 *
 * @note Changing the interval resets the current aggregation window.
 * @note This is an advanced feature - most applications should use the
 *       default interval.
 *
 * @warning Not recommended for use in production unless you have specific
 *          requirements. Inconsistent intervals complicate cloud-side analysis.
 *
 * Example:
 * @code
 * // Set temperature metric to aggregate over 10 minutes instead of 1
 * int rc = spotflow_set_metric_aggregation_interval(
 *     temp_metric,
 *     SPOTFLOW_AGG_INTERVAL_10MIN
 * );
 * @endcode
 */
int spotflow_set_metric_aggregation_interval(
    spotflow_metric_t* metric,
    spotflow_aggregation_interval_t interval
);
```

## Usage Constraints and Best Practices

### Thread Safety

- All API functions are thread-safe and may be called from any context (ISR, thread, work queue).
- Registration functions may block briefly (< 1ms) due to mutex acquisition.
- Reporting functions use per-metric locking to minimize contention.
- Multiple threads may report to the same metric concurrently without data corruption.

### Type Compatibility

**Type conversion is based on the metric's registration type, not the reporting function used:**

- **Integer metrics** (`spotflow_register_metric_int()`):
  - `spotflow_report_metric_int(metric, int64_t)` - stores value as-is
  - `spotflow_report_metric_float(metric, double)` - truncates to int64_t (e.g., `42.7` → `42`)

- **Float metrics** (`spotflow_register_metric_float()`):
  - `spotflow_report_metric_float(metric, double)` - stores value as-is
  - `spotflow_report_metric_int(metric, int64_t)` - converts to double (e.g., `42` → `42.0`)

- **No runtime error**: Calling `_int` or `_float` reporting function on either metric type works - conversion happens based on registration type
- **Best practice**: Match the reporting function to the metric type for clarity and type safety

**Example:**
```c
spotflow_metric_t* int_metric = spotflow_register_metric_int("counter");
spotflow_metric_t* float_metric = spotflow_register_metric_float("temperature");

// Best practice - match function to type:
spotflow_report_metric_int(int_metric, 100);         // Stored as 100
spotflow_report_metric_float(float_metric, 98.6);    // Stored as 98.6

// Also valid - type conversion happens automatically:
spotflow_report_metric_float(int_metric, 42.7);      // Stored as 42 (truncated)
spotflow_report_metric_int(float_metric, 42);        // Stored as 42.0 (converted)
```

### Performance Considerations

1. **Registration**:
   - Register metrics during initialization, not in hot paths
   - Registration allocates memory and initializes data structures
   - Expected time: < 1ms per metric

2. **Reporting**:
   - Designed for high-frequency calling (typical: 1-100 Hz per metric)
   - Expected execution time: < 50 microseconds (typical), < 100 microseconds (p99)
   - No dynamic memory allocation on the reporting path (after registration)
   - Lock contention is per-metric, not global

3. **Memory Usage**:
   - Per-metric overhead: ~128 bytes (configurable)
   - Per-timeseries overhead: ~96 bytes (depends on dimension count)
   - Total per-metric: ~128 + max_timeseries * 96 bytes
   - Example: max_timeseries=16 → ~128 + 16*96 = ~1664 bytes per metric
   - Queue overhead: `CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE * sizeof(void*)`
   - CBOR buffer: `CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE` bytes

### Naming Conventions

1. **Metric Names**:
   - Use lowercase with underscores: `cpu_temperature_celsius`
   - Include unit in name: `_bytes`, `_seconds`, `_celsius`, `_percent`
   - Use suffixes for metric type:
     - `_total` for counters: `http_requests_total`
     - `_count` for counts: `error_count`
     - No suffix for gauges: `cpu_temperature_celsius`
   - Maximum length: 64 characters

2. **Dimension Keys**:
   - Use lowercase with underscores: `interface`, `error_code`
   - Keep keys short (1-2 words)
   - Use consistent keys across related metrics

3. **Dimension Values**:
   - All dimension values are strings - use descriptive values: `eth0`, `power_on`, `critical`
   - For booleans, use string values: `"true"`, `"false"`, or `"1"`, `"0"`
   - For enums/numbers, convert to strings: `snprintf(buf, sizeof(buf), "%d", value)`

### Cardinality Management

**Critical**: Dimension cardinality (number of unique combinations) directly impacts memory and performance.

**Pool Full Behavior**:
- Each metric allocates exactly `max_timeseries` slots (e.g., max_timeseries=16 allocates 16 slots)
- First `max_timeseries` unique combinations get dedicated slots
- When pool is full, additional combinations are **rejected with -ENOSPC error**
- Error log generated: "Metric '%s': time series pool full, dropping dimension combination"
- Rejected dimensions are not aggregated or transmitted (data loss)

**Guidelines**:
- Set `max_timeseries` to expected cardinality (number of unique dimension combinations)
- Add margin for safety (e.g., if expecting 10 combinations, use max_timeseries=15)
- Keep cardinality bounded (< 100 time series per metric)
- Avoid unbounded dimensions (user IDs, timestamps, session IDs)
- Prefer bounded dimensions (core number [0-7], interface name [eth0, wlan0], error code [1-100])
- Monitor application logs for "pool full" errors to detect cardinality issues

**Example of BAD cardinality**:
```c
// BAD: User ID creates unbounded time series
char user_id_str[32];
snprintf(user_id_str, sizeof(user_id_str), "%d", user_id);
spotflow_dimension_t dims[] = {
    { .key = "user_id", .value = user_id_str }  // Could be millions of users!
};
spotflow_report_metric_with_dimensions(requests_metric, 1.0, dims, 1);
```

**Example of GOOD cardinality**:
```c
// GOOD: HTTP status code creates bounded time series (5-10 common codes)
char status_str[8];
snprintf(status_str, sizeof(status_str), "%d", status_code);
spotflow_dimension_t dims[] = {
    { .key = "status_code", .value = status_str }  // Only ~10 common status codes
};
spotflow_report_metric_with_dimensions(requests_metric, 1.0, dims, 1);
```

### Error Handling Patterns

1. **Registration Errors**:
```c
spotflow_metric_t* metric = spotflow_register_metric_float(...);
if (metric == NULL) {
    LOG_ERR("Failed to register metric - check Kconfig limits");
    // Degrade gracefully - app continues without this metric
    return;
}
```

2. **Reporting Errors (Dimensionless)**:
```c
int rc = spotflow_report_metric_int(metric, value);
if (rc == -ENOMEM) {
    // Queue full - transient error, log at debug level
    LOG_DBG("Metric queue full - sample dropped");
} else if (rc == -EINVAL) {
    // Programming error - log at error level and investigate
    LOG_ERR("Invalid metric parameters: %d", rc);
} else if (rc < 0) {
    // Unexpected error
    LOG_WRN("Metric report failed: %d", rc);
}
// Continue application execution regardless
```

3. **Reporting Errors (With Dimensions)**:
```c
int rc = spotflow_report_metric_with_dimensions(metric, value, dims, dimension_count);
if (rc == -ENOSPC) {
    // Time series pool full - consider increasing max_timeseries
    LOG_ERR("Time series pool full - increase max_timeseries at registration");
} else if (rc == -ENOMEM) {
    // Queue full - transient error
    LOG_DBG("Metric queue full - sample dropped");
} else if (rc == -EINVAL) {
    // Programming error - check dimension_count and values
    LOG_ERR("Invalid metric parameters: %d", rc);
} else if (rc < 0) {
    LOG_WRN("Metric report failed: %d", rc);
}
```

### Aggregation Interval Selection

Choose aggregation interval based on metric semantics and reporting frequency:

| Interval | Use Case | Typical Frequency |
|----------|----------|-------------------|
| PT0S (none) | Events, alerts, rare occurrences | < 0.1 Hz |
| PT1M (1 min) | High-frequency metrics, performance | 0.1-10 Hz |
| PT10M (10 min) | Environmental sensors, utilization | 0.01-0.1 Hz |
| PT1H (1 hour) | Coarse trends, daily patterns | < 0.01 Hz |

**Default**: PT1M (1 minute) provides good balance for most embedded applications.

## Integration with Spotflow SDK

### Automatic Initialization

The metrics subsystem initializes automatically when `CONFIG_SPOTFLOW_METRICS=y`:

1. Metrics backend initializes during SDK startup
2. Network polling integrates with existing processor thread
3. No explicit initialization call needed in application code

### Kconfig Dependencies

To use metrics, enable in `prj.conf`:

```kconfig
CONFIG_SPOTFLOW=y
CONFIG_SPOTFLOW_METRICS=y

# Optional: Tune queue size for your application
# CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE=32

# Optional: Increase buffer for complex messages
# CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE=1024
```

### Interaction with Other SDK Features

- **Logs**: Metrics share MQTT connection with logs. Both are polled by processor thread.
- **Coredumps**: Processor prioritizes coredumps > metrics > logs for bandwidth.
- **Configuration**: Future versions may support cloud-based metric configuration.
- **Session Metadata**: Metrics are associated with current session metadata (build ID, device ID).

## Complete Examples

### Example 1: Simple Application Uptime Metric

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "spotflow_metrics.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void) {
    // Register simple uptime metric
    spotflow_metric_t* uptime_metric =
        spotflow_register_metric_float("app_uptime_seconds");

    if (uptime_metric == NULL) {
        LOG_ERR("Failed to register uptime metric");
        // Continue without metrics
    }

    while (1) {
        // Report uptime every 60 seconds
        if (uptime_metric != NULL) {
            int64_t uptime_ms = k_uptime_get();
            spotflow_report_metric_int(uptime_metric, uptime_ms);
        }

        k_sleep(K_SECONDS(60));
    }

    return 0;
}
```

### Example 2: Multi-Core Temperature Monitoring

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "spotflow_metrics.h"
#include "cpu_temp_driver.h"

LOG_MODULE_REGISTER(temp_monitor, LOG_LEVEL_INF);

#define NUM_CORES 4

static spotflow_metric_t* temp_metric;

void temp_monitor_init(void) {
    // Register temperature metric with dimension for core number
    temp_metric = spotflow_register_metric_float_with_dimensions(
        "cpu_temperature_celsius",
        NUM_CORES,  // Track all 4 cores
        1           // One dimension: core
    );

    if (temp_metric == NULL) {
        LOG_ERR("Failed to register temperature metric");
    }
}

void temp_monitor_thread(void) {
    char core_str[4];

    while (1) {
        for (int core = 0; core < NUM_CORES; core++) {
            float temp = cpu_temp_read(core);

            // Create dimension for core number
            snprintf(core_str, sizeof(core_str), "%d", core);
            spotflow_dimension_t dims[] = {
                { .key = "core", .value = core_str }
            };

            int rc = spotflow_report_metric_with_dimensions(
                temp_metric,
                temp,
                dims,
                1  // dimension_count
            );
            if (rc == -ENOMEM) {
                LOG_DBG("Metric queue full for core %d", core);
            }
        }

        // Report every 10 seconds
        k_sleep(K_SECONDS(10));
    }
}
```

### Example 3: Network Interface Statistics

```c
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>
#include "spotflow_metrics.h"

LOG_MODULE_REGISTER(net_stats, LOG_LEVEL_INF);

static spotflow_metric_t* bytes_metric;

void network_stats_init(void) {
    // Register network bytes metric with multiple dimensions
    bytes_metric = spotflow_register_metric_int_with_dimensions(
        "network_bytes_total",
        8,   // Max time series: 2 interfaces × 2 directions × 2 protocols
        3    // Dimensions: interface, direction, protocol
    );

    if (bytes_metric == NULL) {
        LOG_ERR("Failed to register network bytes metric");
    }
}

void report_interface_stats(const char* iface_name, struct net_stats* stats) {
    if (bytes_metric == NULL) return;

    // Report RX bytes for TCP
    spotflow_dimension_t rx_tcp_dims[] = {
        { .key = "interface", .value = iface_name },
        { .key = "direction", .value = "rx" },
        { .key = "protocol", .value = "tcp" }
    };
    spotflow_report_metric_with_dimensions(
        bytes_metric,
        (double)stats->tcp_rx_bytes,
        rx_tcp_dims,
        3  // dimension_count
    );

    // Report TX bytes for TCP
    spotflow_dimension_t tx_tcp_dims[] = {
        { .key = "interface", .value = iface_name },
        { .key = "direction", .value = "tx" },
        { .key = "protocol", .value = "tcp" }
    };
    spotflow_report_metric_with_dimensions(
        bytes_metric,
        (double)stats->tcp_tx_bytes,
        tx_tcp_dims,
        3  // dimension_count
    );

    // ... Similar for UDP and other protocols
}
```

### Example 4: Device Boot Event

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "spotflow_metrics.h"

LOG_MODULE_REGISTER(boot, LOG_LEVEL_INF);

static spotflow_metric_t* boot_event_metric;

void report_boot_event(const char* reason, const char* fw_version) {
    if (boot_event_metric == NULL) {
        boot_event_metric = spotflow_register_metric_int_with_dimensions(
            "device_boot_event",
            10,  // Track up to 10 different boot reason combinations
            2    // Dimensions: reason, firmware_version
        );
        if (boot_event_metric == NULL) {
            LOG_WRN("Cannot register boot event metric");
            return;
        }
    }

    // Report boot event with context
    spotflow_dimension_t dims[] = {
        { .key = "reason", .value = reason },
        { .key = "firmware_version", .value = fw_version }
    };

    int rc = spotflow_report_event_with_dimensions(
        boot_event_metric,
        dims,
        2  // dimension_count
    );
    if (rc < 0) {
        LOG_WRN("Failed to report boot event: %d", rc);
    } else {
        LOG_INF("Boot event reported: %s", reason);
    }
}

int main(void) {
    // Report boot immediately
    report_boot_event("power_on", "1.2.3");

    // ... rest of application
    return 0;
}
```

## Error Codes Reference

| Error Code | Constant | Meaning |
|------------|----------|---------|
| 0 | Success | Operation completed successfully |
| -EINVAL | Invalid argument | NULL pointer, out of range value, or invalid parameter |
| -ENOMEM | Out of memory | Metric queue full or heap allocation failed |
| -ENOTSUP | Not supported | Feature not implemented (e.g., sample collection) |
| -EBUSY | Resource busy | Transient condition, retry may succeed |
| -ENOSPC | No space | Maximum metrics registered or time series limit reached |

## Future API Extensions (Not in V1)

The following features are planned for future releases but not part of the initial API:

1. **Sample Collection**: `collect_samples=true` to store individual values
2. **Histogram Metrics**: Statistical distribution support
3. **Metric Deregistration**: Runtime metric removal
4. **Custom Aggregation Functions**: User-defined aggregation logic
5. **Metric Queries**: On-device metric value retrieval
6. **Cloud-Based Configuration**: Remote metric enable/disable, interval adjustment

## API Version

**Version**: 1.0.0

**Compatibility**: This API is subject to change during development. Once released, breaking changes will increment the major version number following semantic versioning.

## See Also

- Architecture Specification: `architecture.md`
- Ingestion Protocol: `ingestion_protocol_specification.md`
- Implementation Guide: `implementation_guide.md`
- Kconfig Options: `KConfig` in metrics subsystem
