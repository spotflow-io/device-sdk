# Spotflow SDK Metrics Collection and Reporting - Architecture Specification

## 1. Requirements

### 1.1 Functional Requirements

#### FR-1: Metric Registration (MUST - Source: PO API Spec)
- FR-1.1: System shall support registration of float-type metrics with labels
- FR-1.2: System shall support registration of integer-type metrics with labels
- FR-1.3: System shall support registration of simple (non-labeled) metrics
- FR-1.4: Each metric registration shall specify a unique metric name
- FR-1.5: Labeled metrics shall support configurable maximum concurrent time series count
- FR-1.6: Labeled metrics shall support configurable maximum labels count (1-8)
- FR-1.7: System shall optionally support sample collection (future enhancement)
- FR-1.8: System shall enforce strict type matching between registration and reporting

**Verification**: Unit tests for registration API, validation of metric name uniqueness, label limits enforcement

#### FR-2: Metric Reporting (MUST - Source: PO API Spec)
- FR-2.1: System shall support reporting metric values with optional label key-value pairs
- FR-2.2: Label keys shall be string type (max 16 characters)
- FR-2.3: Label values shall be string type (max 32 characters)
- FR-2.4: System shall return error codes indicating success or failure of metric reporting
- FR-2.5: System shall support reporting events (special case of metrics)
- FR-2.6: System shall enforce type-specific reporting (int metrics use int functions, float metrics use float functions)

**Verification**: Integration tests validating metric data flow to cloud platform

#### FR-3: Metric Aggregation (MUST - Source: PO Ingestion Protocol)
- FR-3.1: System shall support aggregation intervals: PT0S (immediate), PT1M, PT10M, PT1H
- FR-3.2: For PT0S metrics, system shall report immediately via message queue (no aggregation)
- FR-3.3: For aggregated metrics, system shall compute sum, count, min, max
- FR-3.4: System shall detect and flag sum truncation when overflow occurs
- FR-3.5: System shall maintain per-metric sequence numbers (incremented on each message transmission)

**Verification**: Mathematical validation of aggregation algorithms, overflow detection tests

#### FR-3A: Time Series Cardinality Control (MUST)
- FR-3A.1: System shall preallocate storage for exactly max_timeseries slots
- FR-3A.2: When time series pool is full, new label combinations shall be rejected with `-ENOSPC` error code
- FR-3A.3: System shall log error when rejecting labels due to full pool
- FR-3A.4: Rejected label combinations shall not be aggregated or transmitted

**Verification**: Unit tests for cardinality limit enforcement, error logging verification

#### FR-4: CBOR Encoding (MUST - Source: SDK Pattern)
- FR-4.1: System shall encode metrics in CBOR format following Spotflow protocol
- FR-4.2: Encoding shall include messageType, metricName, labels, aggregation data
- FR-4.3: System shall use optimized integer keys for CBOR properties
- FR-4.4: Encoding shall include deviceUptimeMs and sequenceNumber
- FR-4.5: Each message shall encode one time series (no batching)

**Verification**: CBOR decode verification, protocol conformance tests

#### FR-5: Transport and Delivery (MUST - Source: SDK Pattern)
- FR-5.1: System shall queue encoded metrics for asynchronous transmission
- FR-5.2: System shall transmit metrics via existing MQTT connection
- FR-5.3: System shall use QoS 0 for metric transmission (consistent with logs)
- FR-5.4: System shall integrate with existing processor thread and connection management
- FR-5.5: System shall rely on SESSION_METADATA sent by connection manager (not metric-specific)

**Verification**: Network traffic analysis, delivery confirmation tests

**Note on SESSION_METADATA**: The SDK's connection manager sends SESSION_METADATA once per MQTT connection establishment. This metadata (device info, firmware version, etc.) applies to ALL message types (logs, coredumps, metrics) for the entire session. Metrics do not send separate SESSION_METADATA.

### 1.2 Non-Functional Requirements

#### NFR-1: Performance (SHOULD - deferred to future iterations)
- NFR-1.1: Metric reporting API should be non-blocking and complete quickly
- NFR-1.2: CBOR encoding should minimize dynamic memory allocation
- NFR-1.3: Queue operations should use minimal locking
- NFR-1.4: Aggregation computation should use O(1) time complexity

**Verification**: Code review for algorithmic complexity (performance benchmarking deferred)

#### NFR-2: Memory Footprint (MUST)
- NFR-2.1: Per-metric storage overhead shall not exceed 128 bytes (configurable)
- NFR-2.2: Message queue shall support configurable size (default 16 messages)
- NFR-2.3: CBOR encoding buffer shall be configurable (default 512 bytes)
- NFR-2.4: Total heap allocation for metrics subsystem shall be < 8KB (default config)

**Verification**: Memory profiling, static analysis of allocations

#### NFR-3: Reliability (MUST)
- NFR-3.1: Metric reporting failures shall not crash or block application
- NFR-3.2: Queue overflow shall drop oldest metrics with statistics tracking
- NFR-3.3: Invalid metric registrations shall fail safely with error codes
- NFR-3.4: Network failures shall not cause metric data corruption

**Verification**: Fault injection testing, stress testing

#### NFR-4: Scalability (SHOULD)
- NFR-4.1: System shall support 10-50 registered metrics (typical use case)
- NFR-4.2: System shall support 100-1000 metric reports per minute (typical load)
- NFR-4.3: Labeled metrics shall support 1-8 labels per metric
- NFR-4.4: System shall support 10-100 concurrent time series per labeled metric

**Verification**: Load testing, scalability benchmarks

### 1.3 Integration Requirements

#### IR-1: SDK Integration (MUST)
- IR-1.1: Metrics subsystem shall follow SDK module structure (backend, cbor, net)
- IR-1.2: Metrics shall use existing MQTT connection and processor thread
- IR-1.3: Metrics shall integrate with Spotflow configuration subsystem
- IR-1.4: Metrics shall use existing CBOR encoding patterns

**Verification**: Code review, integration testing

#### IR-2: Zephyr Integration (MUST)
- IR-2.1: Metrics shall provide Kconfig options following SDK patterns
- IR-2.2: Metrics shall integrate with Zephyr kernel APIs (msgq, malloc, mutex)
- IR-2.3: Metrics shall use Zephyr logging for internal diagnostics
- IR-2.4: Metrics CMakeLists.txt shall follow SDK conventions

**Verification**: Build system validation, Kconfig testing

### 1.4 Constraints and Assumptions

#### Constraints:
1. **Embedded Environment**: Limited RAM (typically 512KB-2MB), limited CPU
2. **Real-time Constraints**: Metric reporting must not block time-critical operations
3. **CBOR Protocol**: Must use CBOR encoding for network efficiency
4. **MQTT Transport**: Must use existing MQTT QoS 0 connection
5. **No Dynamic Memory on Fast Path**: Metric reporting must avoid malloc when possible
6. **Zephyr RTOS**: Must use Zephyr 4.1.x, 4.2.x, 4.3.x APIs

#### Assumptions:
1. Application will register metrics during initialization phase
2. Network connectivity is managed by existing SDK components
3. MQTT connection is established before metrics transmission
4. Device has monotonic clock available (k_uptime_get())
5. Application will not exceed configured limits for concurrent time series
6. Metric names are known at compile time or early initialization

### 1.5 Out of Scope

The following items are explicitly OUT OF SCOPE for this design:

1. **Real-time Streaming**: High-frequency metric streaming (> 1 Hz per metric) not supported in v1
2. **Histogram/Percentile Metrics**: Only basic aggregations (sum, count, min, max) supported
3. **Metric Pull Model**: Only push model supported (device initiates transmission)
4. **Complex Query Interface**: No on-device metric querying or analysis
5. **Persistent Storage**: Metrics not persisted across reboots (volatile buffering only)
6. **Dynamic Metric Registration**: Runtime metric addition after initialization phase discouraged
7. **Cross-Device Aggregation**: Aggregation scope limited to single device
8. **Sample Collection**: Deferred to future release (per PO spec comment)

## 2. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Application Code                                   │
│  ┌──────────────────┐         ┌──────────────────┐                         │
│  │ Metric           │         │ Metric           │                         │
│  │ Registration     │         │ Reporting        │                         │
│  └────────┬─────────┘         └─────────┬────────┘                         │
└───────────┼───────────────────────────────┼──────────────────────────────────┘
            │                               │
            │ spotflow_metrics.h API        │
            │                               │
┌───────────▼───────────────────────────────▼──────────────────────────────────┐
│                    Spotflow Metrics Backend                                  │
│  ┌────────────────────────────────────────────────────────────────┐         │
│  │  Metric Registry                                                │         │
│  │  - Stores metric metadata (name, type, labels config)          │         │
│  │  - Manages metric handles                                       │         │
│  │  - Validates registration parameters                            │         │
│  └────────────────────────────────────────────────────────────────┘         │
│                                                                               │
│  ┌────────────────────────────────────────────────────────────────┐         │
│  │  Metric Aggregator                                              │         │
│  │  - Maintains per-timeseries aggregation state                   │         │
│  │  - Computes sum, count, min, max                                │         │
│  │  - Manages aggregation windows (1m, 10m, 1h)                    │         │
│  │  - Handles label-based time series tracking                     │         │
│  └────────────────────────────────────────────────────────────────┘         │
│                               │                                               │
│                               ▼                                               │
│  ┌────────────────────────────────────────────────────────────────┐         │
│  │  CBOR Encoder (spotflow_metrics_cbor.c)                         │         │
│  │  - Encodes metric messages to CBOR format                       │         │
│  │  - Uses zcbor library                                            │         │
│  │  - Optimized integer keys for wire efficiency                   │         │
│  └────────────────────┬───────────────────────────────────────────┘         │
└────────────────────────┼─────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                    Message Queue (k_msgq)                                   │
│  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ... ┌────┐                                   │
│  │Msg1│ │Msg2│ │Msg3│ │Msg4│     │MsgN│  (CONFIG_SPOTFLOW_METRICS_QUEUE)  │
│  └────┘ └────┘ └────┘ └────┘     └────┘                                   │
└────────────────────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────────────────────┐
│             Spotflow Network Processor (spotflow_processor.c)               │
│  ┌────────────────────────────────────────────────────────────────┐        │
│  │  Metrics Polling (spotflow_metrics_net.c)                       │        │
│  │  - Dequeues metric messages from queue                          │        │
│  │  - Publishes to MQTT broker via existing connection             │        │
│  │  - Handles backpressure and flow control                        │        │
│  └────────────────────────────────────────────────────────────────┘        │
│                               │                                              │
│  Priority: Config > Coredumps > Metrics > Logs                              │
└───────────────────────────────┼──────────────────────────────────────────────┘
                                │
                                ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                      MQTT Client (spotflow_mqtt.c)                          │
│  Topic: ingestion/{provisioning_token}/{device_id}                          │
│  QoS: 0 (at most once delivery)                                             │
└───────────────────────────────┼──────────────────────────────────────────────┘
                                │
                                ▼
                       Spotflow Cloud Platform
```

### Data Flow for Aggregated Metrics

```
Time: T0                    T1 (1 min)              T2 (aggregation trigger)
  │                           │                           │
  ▼                           ▼                           ▼
App: report_metric(temp=20)  report_metric(temp=25)    [timer expires]
  │                           │                           │
  ▼                           ▼                           ▼
Backend:                                              Aggregator:
  Aggregator updates:         Aggregator updates:       - sum=45, count=2
  - sum=20, count=1           - sum=45, count=2         - min=20, max=25
  - min=20, max=20            - min=20, max=25          - seq_num++
                                                          │
                                                          ▼
                                                      CBOR Encoder:
                                                      Creates message
                                                          │
                                                          ▼
                                                      Queue message
                                                          │
                                                          ▼
                                                      Network transmit
```

## 3. Components and Responsibilities

### 3.1 Metrics Backend (spotflow_metrics_backend.c/h)

**Purpose**: Public API layer and metric registration management

**Key Responsibilities**:
- Provide public API for metric registration and reporting
- Manage metric registry (hash table or array of registered metrics)
- Validate API parameters and enforce limits
- Dispatch metric reports to appropriate aggregator or direct encoding
- Handle synchronization for thread-safe access
- Provide statistics (dropped metrics, queue depth)

**Dependencies**:
- Zephyr kernel (k_mutex, k_malloc, k_uptime_get)
- Metrics aggregator (internal component)
- CBOR encoder (for encoding)
- Message queue (for transmission)

**Provided Interfaces**:
```c
// Public API (see API specification for full signatures)
spotflow_metric_t* spotflow_register_metric_float(const char* name);
spotflow_metric_t* spotflow_register_metric_int(const char* name);
spotflow_metric_t* spotflow_register_metric_float_with_labels(const char* name, uint16_t max_timeseries, uint8_t max_labels);
spotflow_metric_t* spotflow_register_metric_int_with_labels(const char* name, uint16_t max_timeseries, uint8_t max_labels);
int spotflow_report_metric_int(spotflow_metric_t* metric, int64_t value);
int spotflow_report_metric_float(spotflow_metric_t* metric, double value);
int spotflow_report_metric_int_with_labels(spotflow_metric_t* metric, int64_t value, const spotflow_label_t* labels, uint8_t label_count);
int spotflow_report_metric_float_with_labels(spotflow_metric_t* metric, double value, const spotflow_label_t* labels, uint8_t label_count);
int spotflow_report_event(spotflow_metric_t* metric);
int spotflow_report_event_with_labels(spotflow_metric_t* metric, const spotflow_label_t* labels, uint8_t label_count);
```

**Threading Model**:
- API calls from application threads (any context)
- Uses mutex for registry access
- Lock-free or minimal locking on metric report path

**Error Handling**:
- Returns negative error codes on failure (Zephyr errno style)
- Logs warnings for dropped metrics
- Never crashes on invalid input (defensive programming)

### 3.2 Metrics Aggregator (spotflow_metrics_aggregator.c/h)

**Purpose**: Maintains aggregation state and computes statistical summaries

**Key Responsibilities**:
- Maintain per-timeseries aggregation state (sum, count, min, max)
- Track label combinations (time series) within configured limits
- Implement aggregation window management (1min, 10min, 1hour timers)
- Trigger metric message generation on window expiration
- Reject new label combinations with error log when pool is full
- Manage sequence numbers per metric
- Support PT0S (immediate reporting) by bypassing aggregation and enqueuing directly

**Dependencies**:
- Zephyr kernel timers (k_timer or k_work_delayable)
- CBOR encoder (for message generation)
- Message queue (for output)

**Provided Interfaces**:
```c
// Internal API (not exposed to applications)
int aggregator_init(void);
int aggregator_register_metric(struct spotflow_metric* metric);
int aggregator_report_value(struct spotflow_metric* metric,
                            const spotflow_label_t* labels,
                            double value);
int aggregator_flush(struct spotflow_metric* metric); // Force aggregation window close
```

**Notes**:
- `aggregator_report_value()` accepts `double value` for both int and float metrics
  - For int metrics: value is cast to `int64_t` inside the function
  - For float metrics: value is used as-is
- `labels` parameter may be `NULL` for simple (non-labeled) metrics
- Return value: `0` on success, `-ENOSPC` if time series pool is full, `-EINVAL` for invalid parameters

**Threading Model**:
- Called from application context (metric report)
- Timer callbacks execute in work queue context
- Uses per-metric locking for aggregation state

**Error Handling**:
- Overflow detection for sum (set truncation flag)
- Label limit enforcement (reject new combinations when pool is full)
- Timer failures logged but not fatal

### 3.3 CBOR Encoder (spotflow_metrics_cbor.c/h)

**Purpose**: Encode metric messages in CBOR format per Spotflow protocol

**Key Responsibilities**:
- Encode single time series metric messages using zcbor
- Use optimized integer keys for wire efficiency
- Allocate output buffer for encoded message
- Validate encoding success

**Dependencies**:
- zcbor library (Zephyr provided)
- Kernel malloc for buffer allocation

**Provided Interfaces**:
```c
// Internal API
int spotflow_metrics_cbor_encode(
    metric_message_t* msg,
    uint8_t** cbor_data,
    size_t* cbor_len
);
```

**Threading Model**:
- Called from aggregator timer context or application context
- Stateless (uses stack or provided buffers)

**Error Handling**:
- Return error codes on encoding failure
- Validate buffer sizes before encoding
- Handle null/invalid inputs gracefully

### 3.4 Network Layer (spotflow_metrics_net.c/h)

**Purpose**: Dequeue and transmit metric messages via MQTT

**Key Responsibilities**:
- Poll metrics message queue from processor thread
- Publish messages via MQTT using existing connection
- Handle transmission errors and backpressure
- Coordinate with logs/coredumps for bandwidth sharing
- Track transmission statistics

**Dependencies**:
- Message queue (input)
- MQTT client (spotflow_mqtt.c)
- Network processor integration

**Provided Interfaces**:
```c
// Internal API
void spotflow_metrics_net_init(void);
int spotflow_poll_and_process_enqueued_metrics(void);
```

**Threading Model**:
- Called from existing processor thread context
- Sequential polling (no dedicated thread)

**Error Handling**:
- Retry on transient MQTT errors (handled by MQTT layer)
- Drop and free message on permanent failure
- Log transmission failures

### 3.5 Message Queue (Zephyr k_msgq)

**Purpose**: Asynchronous buffering between backend and network layer

**Key Responsibilities**:
- FIFO queue for metric message pointers
- Configurable depth via Kconfig
- Handle overflow by dropping oldest messages

**Configuration**:
```c
K_MSGQ_DEFINE(g_spotflow_metrics_msgq,
              sizeof(struct spotflow_mqtt_metrics_msg*),
              CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE,
              1);
```

**Message Structure**:
```c
struct spotflow_mqtt_metrics_msg {
    uint8_t* payload;
    size_t len;
};
```

## 4. Data and Metric Models

### 4.1 Metric Types

```c
typedef enum {
    SPOTFLOW_METRIC_TYPE_FLOAT,  // Float metric (simple or labeled)
    SPOTFLOW_METRIC_TYPE_INT,    // Integer metric (simple or labeled)
} spotflow_metric_type_t;
```

**Design Note**: Simple vs labeled metrics are distinguished by `max_labels == 0` (simple) vs `max_labels > 0` (labeled), not by separate type enum values. This simplifies the type system.

### 4.2 Metric Handle (Opaque)

```c
typedef struct spotflow_metric* spotflow_metric_t;
```

**Internal Structure** (not exposed in public API):
```c
struct spotflow_metric {
    char name[SPOTFLOW_MAX_METRIC_NAME_LEN];
    spotflow_metric_type_t type;     // FLOAT or INT
    uint16_t max_timeseries;         // 0 for simple, >0 for labeled
    uint8_t max_labels;              // 0 for simple, >0 for labeled
    uint32_t sequence_number;        // Per-metric sequence
    struct k_mutex lock;             // Protects aggregation state
    void* aggregator_state;          // Pointer to aggregation context
    bool collect_samples;            // Future feature flag
};
```

**Field Semantics**:
- `type`: Value type (FLOAT or INT) - determines how values are stored/transmitted
- `max_timeseries`:
  - `0` = simple metric (single time series)
  - `>0` = labeled metric (up to max_timeseries concurrent time series)
- `max_labels`:
  - `0` = simple metric (no labels allowed)
  - `>0` = labeled metric (up to max_labels per report, max 8)
- Simple metrics have `max_timeseries == 0` AND `max_labels == 0`

### 4.3 Label Model

```c
// Label key-value pair (string-only values)
typedef struct {
    const char* key;      // Label key (e.g., "core", "interface")
    const char* value;    // Label value (string only)
} spotflow_label_t;
```

**Design Notes**:
- Label values are **string-only** for simplicity
- Numeric values should be converted to strings before reporting (e.g., `snprintf()`)
- Label keys are **case-sensitive** (not normalized)
- Both key and value must be non-NULL, non-empty strings
- Strings must remain valid for duration of `spotflow_report_metric_*()` call
- Internally hashed/copied, so caller can free/reuse after call returns

**String Length Limits**:
```c
#define SPOTFLOW_MAX_METRIC_NAME_LEN     64  // Maximum metric name length
#define SPOTFLOW_MAX_LABEL_KEY_LEN       16  // Maximum label key length
#define SPOTFLOW_MAX_LABEL_VALUE_LEN     32  // Maximum label value length
#define SPOTFLOW_MAX_LABELS_PER_METRIC    8  // Maximum labels per metric
```

### 4.4 Aggregation State (Per Time Series)

```c
// Internal structure for aggregation tracking
struct metric_timeseries_state {
    // Label hash for this time series
    uint32_t label_hash;

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

    // Timestamp tracking
    uint64_t window_start_ms;

    // Label storage (strings are copied/stored for this time series)
    char label_keys[SPOTFLOW_MAX_LABELS_PER_METRIC][SPOTFLOW_MAX_LABEL_KEY_LEN];
    char label_values[SPOTFLOW_MAX_LABELS_PER_METRIC][SPOTFLOW_MAX_LABEL_VALUE_LEN];
    uint8_t label_count;

    // Active flag
    bool is_active;
};

struct metric_aggregator_context {
    spotflow_metric_t* metric;
    uint16_t aggregation_interval;  // 0=PT0S, 1=PT1M, 2=PT10M, 3=PT1H
    struct metric_timeseries_state* timeseries;  // Array of size max_time_series
    uint16_t timeseries_count;       // Current active time series
    uint16_t timeseries_capacity;    // User-configured max (e.g., 16)
    struct k_timer aggregation_timer;
};
```

### 4.5 Wire Format (CBOR Message)

**CBOR Property Keys** (optimized integers):
```c
// CBOR Map Keys (Optimized Integer Keys)
#define KEY_MESSAGE_TYPE           0x00  // Message type discriminator
// 0x01-0x04 RESERVED (used by other message types: SESSION_METADATA, COREDUMP, etc.)
#define KEY_LABELS                 0x05  // Dimension key-value pairs (map)
#define KEY_DEVICE_UPTIME_MS       0x06  // Device uptime in milliseconds
// 0x07-0x0C RESERVED (for future protocol extensions)
#define KEY_SEQUENCE_NUMBER        0x0D  // Per-metric sequence number
// 0x0E-0x0F RESERVED
#define KEY_METRIC_NAME            0x10  // Metric identifier (string)
#define KEY_AGGREGATION_INTERVAL   0x11  // PT0S/PT1M/PT10M/PT1H (uint)
#define KEY_BATCH                  0x12  // RESERVED for future batching support
#define KEY_SUM                    0x13  // Aggregated sum (int64 or float64)
#define KEY_SUM_TRUNCATED          0x14  // Overflow flag (bool)
#define KEY_COUNT                  0x15  // Number of samples (uint64)
#define KEY_MIN                    0x16  // Minimum value (int64 or float64)
#define KEY_MAX                    0x17  // Maximum value (int64 or float64)
#define KEY_SAMPLES                0x18  // RESERVED for future sample collection

// Assigned Message Type
#define METRIC_MESSAGE_TYPE        0x05  // This message type (vs LOG=0x00, etc.)
```

**Message Structure** (CBOR Map):
```
{
    0x00: 0x05,                      // messageType = METRIC
    0x10: "cpu_temperature",         // metricName
    0x05: {                          // labels
        "core": "0",
        "zone": "thermal1"
    },
    0x11: 1,                         // aggregationInterval = PT1M
    0x06: 123456789,                 // deviceUptimeMs
    0x0D: 42,                        // sequenceNumber
    0x13: 2450.5,                    // sum
    0x14: false,                     // sumTruncated
    0x15: 60,                        // count
    0x16: 38.2,                      // min
    0x17: 43.1                       // max
}
```

**PT0S (Immediate) Message** (no aggregation):
```
{
    0x00: 0x05,                      // messageType = METRIC
    0x10: "device_restart",          // metricName
    0x05: {                          // labels
        "reason": "watchdog",
        "fw_ver": "1.2.3"
    },
    0x11: 0,                         // aggregationInterval = PT0S (immediate)
    0x06: 123456789,                 // deviceUptimeMs
    0x0D: 1,                         // sequenceNumber
    0x13: 1.0,                       // sum (single value)
    0x14: false,                     // sumTruncated
    0x15: 1                          // count (always 1 for PT0S)
    // min/max not included (optional for PT0S)
}
```

## 5. API Design

See separate document: `api_specification.md` for full API reference.

**Summary of Public API Surface**:

1. **Metric Registration**:
   - `spotflow_register_metric_float()` / `spotflow_register_metric_int()` (dimensionless)
   - `spotflow_register_metric_float_with_dimensions()` / `spotflow_register_metric_int_with_dimensions()` (with dimensions)

2. **Metric Reporting**:
   - `spotflow_report_metric_int()` / `spotflow_report_metric_float()`
   - `spotflow_report_metric_int_with_dimensions()` / `spotflow_report_metric_float_with_dimensions()`
   - `spotflow_report_event()` / `spotflow_report_event_with_dimensions()`

3. **Configuration**:
   - Kconfig options for queue size, buffer sizes, aggregation defaults

4. **Extension Mechanisms**:
   - Custom dimension types via union extension
   - Aggregation interval selection per metric

## 6. Example Usage Scenarios

### 6.1 Simple Counter Metric (No Dimensions)

```c
// During initialization
spotflow_metric_t* requests_metric =
    spotflow_register_metric_int("http_requests_total");

// During operation (in request handler)
int result = spotflow_report_metric_int(requests_metric, 1);
if (result < 0) {
    LOG_WRN("Failed to report metric: %d", result);
}
```

**Expected Behavior**:
- Each report increments the counter
- Aggregated over 1-minute window (default)
- Transmitted as: sum=N, count=N, min=1, max=1 (all values are 1)

### 6.2 Temperature Gauge with Dimensions

```c
// During initialization
spotflow_metric_t* temp_metric =
    spotflow_register_metric_float_with_dimensions(
        "cpu_temperature_celsius",
        4,      // max 4 concurrent time series (cores)
        1       // 1 dimension (core ID)
    );

// During operation (in temperature monitoring thread)
spotflow_dimension_t dims[1] = {
    { .key = "core", .value = "0" }
};

float temp = read_temperature_sensor(0);
int result = spotflow_report_metric_float_with_dimensions(temp_metric, temp, dims, 1);
```

**Expected Behavior**:
- Separate time series for each core (0-3)
- Aggregation computes avg (sum/count), min, max per core
- All time series for this metric share the same sequence number
- If > 4 cores report, additional cores are rejected with error log

### 6.3 Network Traffic Counter with Multiple Dimensions

```c
// During initialization
spotflow_metric_t* network_metric =
    spotflow_register_metric_int_with_dimensions(
        "network_bytes_total",
        8,      // max 8 time series (2 interfaces × 2 directions × 2 protocols)
        3       // 3 dimensions
    );

// During operation (in network stack)
spotflow_dimension_t dims[3] = {
    { .key = "interface", .value = "eth0" },
    { .key = "direction", .value = "rx" },
    { .key = "protocol", .value = "tcp" }
};

int64_t bytes = get_interface_rx_bytes("eth0", PROTO_TCP);
spotflow_report_metric_int_with_dimensions(network_metric, bytes, dims, 3);
```

**Expected Behavior**:
- Each unique combination of (interface, direction, protocol) creates a time series
- System tracks up to 8 combinations
- Aggregation happens per time series
- One message sent per time series per aggregation interval
- If > 8 combinations occur, additional combinations are rejected with error log

### 6.4 Event Reporting (Point-in-Time)

```c
// During initialization
spotflow_metric_t* boot_event =
    spotflow_register_metric_int_with_dimensions(
        "device_boot_event",
        10,  // Track up to 10 different boot events
        2    // Dimensions: reason, firmware_version
    );

// During boot sequence
spotflow_dimension_t dims[2] = {
    { .key = "reason", .value = "power_on" },
    { .key = "firmware_version", .value = "1.2.3" }
};

// Report event with PT0S (no aggregation) for immediate transmission
spotflow_report_event_with_dimensions(boot_event, dims, 2);
```

**Expected Behavior**:
- Event reported with aggregationInterval = PT0S (no aggregation)
- Enqueued to message queue immediately (non-blocking)
- Transmitted on next processor thread polling cycle
- Dimensions included as labels in message
- count=1, sum=value (min/max not sent - they are optional)

### 6.5 Error Handling Patterns

```c
spotflow_metric_t* metric = spotflow_register_metric_float_with_dimensions(
    "sensor_reading", 1, 1);

if (metric == NULL) {
    LOG_ERR("Failed to register metric - out of memory or limit exceeded");
    // Degrade gracefully - continue without metrics
    return;
}

spotflow_dimension_t dims[1] = { { .key = "example", .value = "test" } };
int result = spotflow_report_metric_float_with_dimensions(metric, 42.0, dims, 1);

switch (result) {
    case 0:
        // Success
        break;
    case -EINVAL:
        LOG_ERR("Invalid dimensions or metric handle");
        break;
    case -ENOMEM:
        LOG_WRN("Metric queue full - metric dropped");
        break;
    case -ENOSPC:
        LOG_ERR("Time series pool full - increase max_timeseries");
        break;
    case -ENOTSUP:
        LOG_ERR("Operation not supported for this metric type");
        break;
    default:
        LOG_ERR("Unknown error: %d", result);
}
```

### 6.6 Integration with Existing SDK Features

```c
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "spotflow_metrics.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("Starting application with Spotflow observability");

    // Spotflow SDK initializes automatically (logs, metrics, coredumps)
    // via log backend autostart mechanism

    // Register application metrics
    spotflow_metric_t* uptime_metric =
        spotflow_register_metric_float("app_uptime_seconds");

    // Application logic
    while (1) {
        // Regular logging (sent via Spotflow log backend)
        LOG_INF("Application heartbeat");

        // Metric reporting
        uint64_t uptime = k_uptime_get() / 1000; // convert to seconds
        spotflow_report_metric_int(uptime_metric, (int64_t)uptime);

        k_sleep(K_SECONDS(60));
    }

    return 0;
}
```

**Expected Behavior**:
- Logs and metrics share MQTT connection
- Processor thread handles both logs and metrics (priority: config > coredumps > metrics > logs)
- No additional configuration needed beyond enabling `CONFIG_SPOTFLOW_METRICS`

### 6.7 Event Functions

**Purpose**: Event functions are convenience wrappers for reporting point-in-time occurrences without aggregation.

**API**:
```c
int spotflow_report_event(spotflow_metric_t* metric);
int spotflow_report_event_with_dimensions(spotflow_metric_t* metric,
                                          const spotflow_dimension_t* dimensions,
                                          uint8_t dimension_count);
```

**Behavior**:
- Internally calls `spotflow_report_metric_int(metric, 1)` or `spotflow_report_metric_int_with_dimensions(metric, 1, ...)`
- Automatically uses PT0S aggregation interval (no aggregation)
- Value is always `1` (represents "event occurred")
- Transmitted immediately (no waiting for aggregation window)

**Relationship to PT0S Metrics**:
- Event functions are **equivalent** to calling `spotflow_report_metric_int(metric, 1)` with PT0S interval
- PT0S metrics bypass aggregation and go directly to message queue
- Both approaches are valid; event functions provide clearer semantics for boolean events

**Use Cases**:
- Device boot events
- Error occurrences
- State transitions
- User interactions
- Any point-in-time occurrence that doesn't need aggregation

**Example**:
```c
// These are equivalent:
spotflow_report_event(boot_metric);
// vs
spotflow_report_metric_int(boot_metric, 1);  // with PT0S interval
```

## 7. Architectural Decisions

This section documents all architectural decisions made for the metrics subsystem. All decisions are final and implementation-ready.

### 7.1 Protocol and Data Model Decisions

**AD-1: Message Type ID Assignment**
- **Decision**: METRIC messages use message type **0x05**
- **Rationale**:
  - Distinguishes from LOG (0x00), SESSION_METADATA (0x01), COREDUMP_START (0x02), COREDUMP_CHUNK (0x03)
  - 0x04 is reserved for future use
  - Coordinated with backend ingestion service
- **Impact**: Backend must recognize 0x05 as METRIC message type

**AD-2: Default Aggregation Interval**
- **Decision**: Default aggregation interval is **PT1M** (1 minute)
- **Rationale**:
  - Balances network bandwidth efficiency with data granularity
  - Appropriate for dashboard visualization (1-minute resolution)
  - Matches typical system metrics collection interval (60 seconds)
  - PT0S (no aggregation) available for event-based metrics
- **Impact**: Affects network bandwidth and cloud storage patterns

**AD-3: Dimension Key Case Sensitivity**
- **Decision**: Dimension keys are **case-sensitive**
- **Rationale**:
  - Simpler implementation without normalization overhead
  - Matches industry standard behavior (Prometheus, InfluxDB use case-sensitive labels)
  - Clear semantics: "Core" and "core" are distinct dimension keys
  - Metric names remain case-insensitive (normalized to lowercase)
- **Impact**: Users must maintain consistent casing in dimension keys
- **Documentation**: Emphasize case-sensitivity in API documentation and examples

**AD-4: Sequence Number Scope and Increment Timing**
- **Decision**: Sequence numbers are **per-metric** (not per-timeseries), incremented **on each message transmission**
- **Rationale**:
  - All dimension combinations of a single metric share the same sequence number counter
  - Matches PO specification: "Each metric has its own sequence, different from other metrics. Messages for a single metric with different dimensions share the sequence."
  - Sequence number increments when message is **enqueued for transmission** (not when user reports a value, not when aggregation window expires)
  - For dimensional metrics with multiple time series, each time series message gets a unique sequence number
- **Timing**: Increment happens during CBOR encoding, just before enqueuing to MQTT queue
- **Impact**:
  - Sequence number management per-metric, not per unique dimension combination
  - Gaps in sequence indicate dropped messages (queue full, encoding errors)
  - For a metric with 3 active time series, sequence numbers advance by 3 per aggregation interval

**Example - Dimensional Metric Sequence Number Behavior**:
```
Metric: "cpu_temp" with dimension "core" (max_timeseries=4, agg_interval=PT1M)

Time T0:
  - User reports: cpu_temp{core="0"} = 45.0
  - User reports: cpu_temp{core="1"} = 47.0
  - User reports: cpu_temp{core="2"} = 46.0
  - Sequence number: still 100 (no messages sent yet)

Time T0+60s (aggregation window expires):
  - 3 CBOR messages generated (one per active time series):
    - Message 1: cpu_temp{core="0"}, seq=100, metric->sequence_number++ → 101
    - Message 2: cpu_temp{core="1"}, seq=101, metric->sequence_number++ → 102
    - Message 3: cpu_temp{core="2"}, seq=102, metric->sequence_number++ → 103
  - All 3 messages enqueued to MQTT queue
  - Next sequence number will be 103

Time T0+120s (next window):
  - User reports: cpu_temp{core="0"} = 50.0
  - User reports: cpu_temp{core="1"} = 52.0
  - User reports: cpu_temp{core="3"} = 48.0  (new core!)
  - 3 messages generated:
    - Message 1: cpu_temp{core="0"}, seq=103
    - Message 2: cpu_temp{core="1"}, seq=104
    - Message 3: cpu_temp{core="3"}, seq=105
  - Next sequence number: 106
```

**Key Point**: The sequence number is **not** incremented when `spotflow_report_metric_float()` is called. It increments only when a CBOR message is encoded and enqueued for MQTT transmission.

### 7.2 Implementation Decisions

**AD-5: Time Series Hashing Algorithm**
- **Decision**: Use **FNV-1a** hashing algorithm for dimension combinations
- **Rationale**:
  - Good hash distribution with low collision probability
  - Low computational cost (critical for embedded systems)
  - Simple implementation (no external dependencies)
  - **Production-ready** for typical use cases (max_timeseries ≤ 256)
  - 32-bit hash space (4 billion values) sufficient for expected cardinalities
- **Collision Handling**: Full dimension comparison performed on hash match
- **Alternatives Considered**: MurmurHash3 (more complex, marginal improvement), CRC32 (lower quality distribution)

**AD-6: Aggregation Window Alignment**
- **Decision**: Use **sliding window** aggregation (from first report)
- **Rationale**:
  - Simpler device implementation
  - No clock synchronization required
  - Predictable behavior regardless of wall clock time
- **Alternatives Considered**: Wall clock alignment (rejected - requires NTP/time sync, more complex)
- **Impact**: Aggregation windows start at first metric report, not aligned to wall clock

**Concrete Timing Example**:
```
Metric with PT1M (1-minute) aggregation interval:

T=0:        Metric registered via spotflow_register_metric_float()
            (Timer NOT started yet)

T=0.5s:     First spotflow_report_metric_float() call
            → Aggregation window STARTS at T=0.5s
            → Timer scheduled for T=60.5s

T=1s-60s:   Additional reports accumulate in aggregation state
            (sum, count, min, max updated)

T=60.5s:    Timer expires
            → Aggregation window CLOSES
            → CBOR message generated with aggregated data
            → Message enqueued to MQTT queue
            → NEW window begins immediately at T=60.5s
            → Timer rescheduled for T=120.5s

T=120.5s:   Next window expiration
            → Process repeats

Key: Window duration is always exactly PT1M (60 seconds), but window
boundaries are determined by first report time, not wall clock.
```

**Boot-Time and MQTT Connection Timing**:

The metrics subsystem can initialize and start aggregation timers **BEFORE** the MQTT connection is established. This is safe because of the message queue buffering design:

1. **Aggregated metrics** are encoded to CBOR and enqueued to message queue
2. **Queue provides buffering** (default 16 messages via `CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE`)
3. **Processor thread** dequeues and publishes when MQTT connection becomes ready
4. **If queue fills** before connection ready, oldest messages are dropped (logged as warnings)

**Architecture Flow**:
```
Aggregator → CBOR Encoder → Message Queue (16 msg buffer) → Processor → MQTT
                                    ↓
                          Decouples metrics collection from
                          network availability
```

This design ensures that:
- Metrics collection can begin immediately after SDK initialization
- Aggregation timers can fire even if MQTT not connected yet
- Network unavailability doesn't block the application
- Messages are delivered when connection becomes available

**AD-7: Time Series Cardinality Limit Handling**
- **Decision**: **Reject** new dimension combinations when pool is full
- **Implementation**: Log error message and return `-ENOSPC` error code
- **Rationale**:
  - Simple, predictable memory usage
  - Clear failure mode for developers
  - No silent data corruption or undefined behavior
- **Tradeoff**: Data loss for dimensions exceeding limit (user must configure `max_timeseries` appropriately)
- **Mitigation**: Clear error messages, comprehensive documentation with cardinality best practices

**AD-8: Mutex Granularity**
- **Decision**: Use **per-metric mutex** for thread safety
- **Rationale**:
  - Isolated contention (one metric doesn't block another)
  - Acceptable memory overhead (one mutex per registered metric)
  - Simpler than lock-free atomic operations
- **Alternatives Considered**: Global metrics lock (rejected - high contention), lock-free atomics (rejected - too complex)

**AD-9: Sample Collection Storage**
- **Decision**: **Skip sample collection for v1**, design interface for future extension
- **Rationale**:
  - Not required for initial aggregation-based metrics
  - Reduces implementation complexity and memory overhead
  - API designed to be extensible (sample collection reserved for future)
- **Future Work**: Ring buffer or dynamic array if sample storage becomes required

### 7.3 Integration Decisions

**AD-10: Processor Thread Priority**
- **Decision**: Metrics **share the same priority** as logs and coredumps
- **Implementation**: Use existing `SPOTFLOW_MQTT_THREAD_PRIORITY` configuration
- **Rationale**:
  - Simplifies implementation (no additional Kconfig needed)
  - Consistent with existing SDK architecture
  - Adequate for metrics use case (not latency-critical like coredumps)
- **Priority Order**: Configuration > Coredumps > Metrics > Logs
- **Alternatives Considered**: Separate configurable priority (rejected - adds complexity without clear benefit)

**AD-11: Kconfig Option Naming**
- **Decision**: Use **`SPOTFLOW_METRICS_*`** prefix for all Kconfig options
- **Rationale**: Consistent with existing SDK naming conventions (SPOTFLOW_LOG_*, SPOTFLOW_COREDUMPS_*)
- **Examples**:
  - `CONFIG_SPOTFLOW_METRICS` - Enable metrics subsystem
  - `CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE` - Queue size configuration
  - `CONFIG_SPOTFLOW_METRICS_SYSTEM` - Enable system metrics

**AD-12: MQTT Topic Structure**
- **Decision**: Metrics use the **same MQTT topic** as logs: `ingestion/{token}/{device_id}`
- **Rationale**:
  - No backend routing changes required
  - Consistent message delivery path
  - Simplifies cloud-side ingestion (single topic per device)
  - Message type field (0x05) distinguishes metrics from logs/coredumps
- **Impact**: Single MQTT subscription handles all device telemetry (logs, coredumps, metrics)
- **Alternatives Considered**: Separate metrics topic (rejected - increases backend complexity)

**AD-13: SESSION_METADATA Application Scope**
- **Decision**: SESSION_METADATA is sent **once per MQTT connection** and applies to **entire session**
- **Rationale**:
  - SESSION_METADATA is a session-level concept, not message-type-specific
  - Contains device info, firmware version, SDK version
  - Single SESSION_METADATA message covers logs, coredumps, AND metrics
  - Metrics don't require special SESSION_METADATA handling
- **Implementation**: No changes needed - existing SESSION_METADATA logic applies to all message types
- **Impact**: Backend associates SESSION_METADATA with all subsequent messages until next connection

### 7.4 Performance Considerations (Deferred to Future Iterations)

Performance benchmarking is not part of the initial implementation phase but should be considered for future optimization:

1. **CBOR Encoding Time**: Measure encoding time for various message sizes
2. **Aggregation Update Latency**: Profile time to update aggregation state
3. **Queue Contention**: Analyze impact of concurrent metric reports
4. **Memory Fragmentation**: Monitor long-term heap fragmentation from queue operations
5. **Network Bandwidth**: Characterize typical bytes/second for various metric reporting rates

**Note**: These are monitoring and optimization tasks, not blocking implementation decisions.

## 8. Risk Analysis and Mitigation

### 8.1 Technical Risks

**RISK-1: Memory Exhaustion**
- **Description**: Dimensional metrics with many unique dimension combinations could exhaust heap
- **Probability**: Medium (depends on application usage)
- **Impact**: High (SDK failure, application crash)
- **Mitigation**:
  - Enforce strict limits on concurrent time series (configurable)
  - LRU eviction when limits exceeded
  - Provide Kconfig guard rails with sane defaults
  - Document best practices for dimension cardinality

**RISK-2: Aggregation Timer Overhead**
- **Description**: Many metrics with different aggregation intervals could create timer overhead
- **Probability**: Low (typical apps have 10-50 metrics)
- **Impact**: Medium (increased CPU usage)
- **Mitigation**:
  - Use Zephyr work queue for timer callbacks (shared thread pool)
  - Batch timer expirations when possible
  - Measure and document timer overhead in performance tests

**RISK-3: Queue Overflow Under Load**
- **Description**: High metric reporting rate could fill queue, dropping metrics
- **Probability**: Medium (depends on network conditions)
- **Impact**: Medium (metric data loss, but non-fatal)
- **Mitigation**:
  - Configurable queue size (default 16, allow up to 64)
  - Statistics tracking for dropped metrics
  - Application-level backpressure via error codes
  - Documentation on proper metric reporting rates

**RISK-4: CBOR Encoding Failures**
- **Description**: Complex messages might exceed buffer sizes
- **Probability**: Low (with proper configuration)
- **Impact**: Medium (metric message dropped)
- **Mitigation**:
  - Conservative default buffer sizes (512 bytes for typical message)
  - Validation before encoding (reject oversized dimension sets)
  - Graceful degradation (log error, continue operation)

### 8.2 Integration Risks

**RISK-5: Breaking Changes to SDK Architecture**
- **Description**: Metrics integration might require changes to processor thread or MQTT client
- **Probability**: Low (design follows existing patterns closely)
- **Impact**: High (delays release, breaks existing features)
- **Mitigation**:
  - Early review of integration points with SDK maintainers
  - Minimize changes to shared components
  - Comprehensive integration testing
  - Phased rollout with feature flag

**RISK-6: Performance Impact on Logs/Coredumps**
- **Description**: Metrics polling might delay log transmission
- **Probability**: Medium (shared processor thread)
- **Impact**: Medium (increased log latency)
- **Mitigation**:
  - Priority ordering (metrics after coredumps, before logs)
  - Limit metrics processing time per polling cycle
  - Performance testing with concurrent logs/metrics
  - Configurable metrics priority if needed

### 8.3 Operational Risks

**RISK-7: Unexpected Cloud Backend Incompatibility**
- **Description**: CBOR message format might not parse correctly on backend
- **Probability**: Low (protocol spec should be clear)
- **Impact**: High (metrics not ingested)
- **Mitigation**:
  - Early protocol validation with backend team
  - Test harness for CBOR message validation
  - Schema versioning for future changes
  - Fallback to simple format if needed

**RISK-8: User Misconfiguration**
- **Description**: Users might configure too many metrics or too high reporting rate
- **Probability**: High (embedded developers may not understand cardinality limits)
- **Impact**: Medium (poor performance, metric loss)
- **Mitigation**:
  - Comprehensive documentation with examples
  - Kconfig help text with warnings
  - Runtime warnings when approaching limits
  - Example applications demonstrating best practices

### 8.4 Migration Concerns

Not applicable - new feature, no migration from previous version.

### 8.5 Testing Challenges

**CHALLENGE-1: Long-Running Aggregation Testing**
- **Description**: Need to test 1-hour aggregation windows in reasonable time
- **Mitigation**: Accelerated time testing, or separate long-duration test suite

**CHALLENGE-2: Dimension Cardinality Edge Cases**
- **Description**: Testing all combinations of dimensions is combinatorially expensive
- **Mitigation**: Focused testing on boundary conditions, fuzz testing for random combinations

**CHALLENGE-3: Network Partition Scenarios**
- **Description**: Difficult to simulate network failures in CI environment
- **Mitigation**: Mock MQTT layer for unit tests, manual testing for integration

## 9. Implementation Phases

### Phase 1: Core Infrastructure (Week 1-2)
- Metric registry and registration API
- Basic aggregation for dimensionless metrics
- CBOR encoding for simple messages
- Message queue integration
- Unit tests for core components

### Phase 2: Dimensional Metrics (Week 3-4)
- Dimension handling and hashing
- Multi-timeseries aggregation
- Time series overflow handling
- Unit tests for dimensional features

### Phase 3: Network Integration (Week 5)
- Network polling layer
- MQTT integration
- Processor thread coordination
- Integration tests with logs

### Phase 4: Configuration and Documentation (Week 6)
- Kconfig options
- CMakeLists.txt integration
- API documentation
- Sample application
- User guide

### Phase 5: Testing and Validation (Week 7-8)
- Integration testing with full SDK
- Cloud platform integration validation
- Bug fixes and edge case handling
- Documentation review and updates

## 10. Success Criteria

The metrics feature will be considered successful if:

1. **Functional**: All functional requirements met with < 5 known bugs
2. **Reliability**: Metrics delivered successfully to cloud platform under normal network conditions
3. **Memory**: Stays within configured memory limits (default ~8KB for typical configuration)
4. **Integration**: No degradation to existing logs/coredumps functionality
5. **Usability**: Developers can integrate metrics in < 30 minutes
6. **Documentation**: Complete API docs and example applications

## Appendices

### Appendix A: Glossary

- **Metric**: A time-series measurement with a name, optional dimensions, and numeric value
- **Dimension**: A key-value pair that identifies a specific time series within a metric
- **Time Series**: A unique combination of metric name and dimension values
- **Aggregation**: Statistical summarization of metric values over a time window
- **Cardinality**: The number of unique time series for a metric
- **Backend**: The device-side SDK component that collects metrics
- **Ingestion**: The cloud-side service that receives and processes metrics

### Appendix B: References

1. Spotflow Documentation: https://docs.spotflow.io
2. PO API Specification: `design/metrics/specification/api.md`
3. PO Ingestion Protocol: `design/metrics/specification/ingestion_protocol.md`
4. Zephyr Logging Backend: https://docs.zephyrproject.org/latest/services/logging/index.html
5. CBOR RFC: https://datatracker.ietf.org/doc/html/rfc8949
6. Zephyr ZCBOR: https://docs.zephyrproject.org/latest/services/serialization/zcbor.html

### Appendix C: Configuration Defaults

Recommended Kconfig defaults:

```kconfig
CONFIG_SPOTFLOW_METRICS=n  # Opt-in feature
CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE=16
CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE=512
CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED=32
CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL=1  # PT1M (see below)
CONFIG_SPOTFLOW_METRICS_MAX_DIMENSIONS_PER_METRIC=8
CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS=16384
CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL=2  # WARNING
```

**CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL Configuration:**

This Kconfig option sets the default aggregation period for all metrics registered without explicitly specifying a period. The value is an integer mapping to ISO 8601 duration strings:

| Kconfig Value | ISO 8601 | Enum Constant | Description | Use Case |
|---------------|----------|---------------|-------------|----------|
| `0` | `PT0S` | `SPOTFLOW_AGG_INTERVAL_NONE` | No aggregation - report each value immediately | Events, state changes, one-time boot metrics |
| `1` | `PT1M` | `SPOTFLOW_AGG_INTERVAL_1MIN` | Aggregate over 1 minute (60 seconds) | **Default** - Standard metrics, system telemetry |
| `2` | `PT10M` | `SPOTFLOW_AGG_INTERVAL_10MIN` | Aggregate over 10 minutes | Low-frequency metrics, reduce bandwidth |
| `3` | `PT1H` | `SPOTFLOW_AGG_INTERVAL_1HOUR` | Aggregate over 1 hour | Very low-frequency metrics, historical trends |

**Configuration Guidelines:**

1. **Default Value (1 / PT1M)**: Recommended for most applications
   - Provides good balance between timeliness and bandwidth
   - Matches dashboard expectations for time-series data
   - Suitable for system metrics (CPU, memory, network)

2. **Event-Based Metrics (0 / PT0S)**: Use when:
   - Reporting state changes (e.g., connection state)
   - One-time boot information (e.g., reset cause)
   - Critical events that need immediate visibility
   - Note: Applications should explicitly pass `"PT0S"` to reporting functions for event metrics, rather than changing the default

3. **Longer Intervals (2-3 / PT10M-PT1H)**: Use when:
   - Network bandwidth is severely constrained
   - Metrics change slowly (e.g., ambient temperature)
   - Historical trends are more important than real-time data
   - Warning: Longer intervals delay visibility into system state

**Example Configurations:**

```kconfig
# Standard configuration (recommended)
CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL=1  # PT1M

# Low-bandwidth configuration (satellite, cellular)
CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL=2  # PT10M

# High-frequency monitoring (development, debugging)
CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL=0  # PT0S (not recommended for production)
```

**Heap Pool Sizing Calculator:**

The `CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS` value depends on the number and configuration of registered metrics. Use this formula to calculate the required heap size:

**Base overhead**: 2KB (CBOR buffers, queue structures)

**Per registered metric**:
- Aggregator context: 128 bytes
- Per time series: 96 bytes × max_timeseries
- Formula: `128 + (96 × max_timeseries)`

**Total formula**:
```
HEAP_SIZE = 2048 + SUM(128 + 96 × max_timeseries[i]) for all i metrics
```

**Example configurations**:
- 10 dimensionless metrics: `2048 + 10×128 = 3.3KB`
- 10 dimensional metrics (max_timeseries=16): `2048 + 10×(128+96×16) = 17.4KB`
- 32 metrics (max_timeseries=16 each): `2048 + 32×(128+96×16) = 53KB`

**Recommendation**: Add 25% safety margin to calculated value.

**Default value rationale** (16384 bytes / 16KB):
- Supports system metrics auto-collection (7 metrics, up to 4 time series each)
- Provides headroom for application metrics (additional 8KB available)
- If system metrics disabled or fewer application metrics used, this value can be reduced

**Note**: The default aggregation interval applies only to metrics that don't specify an explicit interval when reporting. Application code can override this on a per-report basis by passing a specific ISO 8601 duration string to the reporting functions (see API specification for details).
