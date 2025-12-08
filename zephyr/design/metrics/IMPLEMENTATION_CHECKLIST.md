# Metrics Feature - Implementation Checklist

**Status**: Ready for Implementation (95% design complete)
**Last Updated**: 2025-12-08
**Implementer Guide**: Use this document as step-by-step implementation reference

---

## Overview

This document provides a comprehensive checklist for implementing the Spotflow SDK metrics collection and reporting feature. All architectural decisions have been finalized and documented in the design documents.

### Design Documents Reference

1. **architecture.md** - Core metrics architecture and component design
2. **api_specification.md** - Public API functions and signatures
3. **implementation_guide.md** - Detailed implementation instructions and code examples
4. **ingestion_protocol_specification.md** - CBOR wire protocol specification
5. **system_metrics_architecture.md** - System metrics auto-collection design

---

## Critical Implementation Decisions (All Resolved)

### 1. Memory Allocation Failure Recovery
**Decision**: Roll back registry slot, return NULL, log error

**Implementation**:
```c
int rc = aggregator_register_metric(metric);
if (rc < 0) {
    LOG_ERR("Failed to initialize aggregator for metric '%s': %d", normalized_name, rc);
    g_metric_registry[i].in_use = false;  // ← Rollback
    k_mutex_unlock(&g_registry_lock);
    return NULL;
}
```

**Contract**: `aggregator_register_metric()` must be atomic - either full success or full failure with no side effects.

---

### 2. Aggregation Window Timing
**Decision**: Sliding window from first report

**Example**:
```
- T=0: Metric registered
- T=0.5s: First report → timer started, window begins at T=0.5s
- T=60.5s: Timer expires, window closes, message sent
- T=60.5s: New window begins immediately
- T=120.5s: Next expiration
```

**Implementation**: Start `k_work_delayable` timer on first `spotflow_report_metric_*()` call, not on registration.

---

### 3. Boot-Time / MQTT Connection Timing
**Decision**: Queue buffering decouples metrics from MQTT connection state

**Architecture**:
```
Aggregator → CBOR Encoder → Message Queue (16 msg buffer) → Processor → MQTT
```

**Key Points**:
- Metrics subsystem can initialize before MQTT connection established
- Aggregation timers can fire before MQTT ready
- Messages enqueue to buffer and transmit when connection becomes available
- Queue full → oldest messages dropped (logged as warnings)

---

### 4. Heap Pool Sizing
**Decision**: 16KB default for system metrics, add sizing calculator

**Formula**:
```
HEAP_SIZE = 2048 + SUM(128 + 96 × max_timeseries[i]) for all i metrics

Examples:
- 10 dimensionless metrics: 2048 + 10×128 = 3.3KB
- 10 dimensional (ts=16): 2048 + 10×(128+96×16) = 17.4KB
- 32 metrics (ts=16 each): 2048 + 32×(128+96×16) = 53KB

Recommendation: Add 25% safety margin
```

**Kconfig**:
- `CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS=16384` (core metrics)
- `CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM=16384` (system metrics)

---

### 5. Hash Collision Handling
**Decision**: FNV-1a acceptable for production

**Implementation**: Use FNV-1a with full dimension comparison on hash match (already specified in implementation_guide.md:630).

---

### 6. MQTT Backpressure / Retry
**Decision**: Infinite retry loop on -EAGAIN (preserves message ordering)

**Implementation**:
```c
// Infinite retry loop for transient errors (preserves message ordering)
int rc;
do {
    rc = spotflow_mqtt_publish_ingest_cbor_msg(payload, len);
    if (rc == -EAGAIN) {
        LOG_DBG("MQTT busy, retrying...");
        k_sleep(K_MSEC(10));  // Small delay before retry
        // Continue loop - do NOT requeue (would break ordering)
    }
} while (rc == -EAGAIN);

if (rc != 0) {
    LOG_WRN("Failed to publish metric message: %d", rc);
    // Permanent error - message lost
}
k_free(payload);  // Always free after success or permanent failure
```

**Critical**: Cannot requeue because it would change message order. Retry in place infinitely.

---

### 7. Processor Thread Integration
**Decision**: Integrate in `process_config_coredumps_or_logs()` between coredumps and logs

**File**: `modules/lib/spotflow/zephyr/src/net/spotflow_processor.c`
**Function**: `process_config_coredumps_or_logs()` (lines 65-93)
**Integration Point**: Lines 77-78

**Code Modification**:
```c
static int process_config_coredumps_or_logs()
{
    int rc = spotflow_config_send_pending_message();
    if (rc < 0) return rc;

#ifdef CONFIG_SPOTFLOW_COREDUMPS
    rc = spotflow_poll_and_process_enqueued_coredump_chunks();
    if (rc < 0) return rc;
#endif

/* ===== INSERT METRICS POLLING HERE ===== */
#ifdef CONFIG_SPOTFLOW_METRICS
    rc = spotflow_poll_and_process_enqueued_metrics();
    if (rc < 0) {
        LOG_DBG("Failed to process metrics: %d", rc);
        return rc;
    }
#endif
/* ===== END OF METRICS BLOCK ===== */

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
    if (rc == 0) {  // No coredumps/metrics sent -> send logs
        rc = poll_and_process_enqueued_logs();
        if (rc < 0) return rc;
    }
#endif
    return rc;
}
```

**Additional Changes**:
1. Add header include (after line 20): `#include "metrics/spotflow_metrics_net.h"`
2. Add init call in `spotflow_mqtt_thread_entry()` after line 54: `spotflow_metrics_net_init();`

**Priority Order**: Config → Coredumps → **Metrics** → Logs

---

### 8. Per-Metric Timer Scope
**Decision**: One timer per metric, all time series expire together (correct design)

**Implementation Notes**:
- All time series of a metric share the same `k_work_delayable` timer
- When timer expires, iterate all active time series and generate messages
- Each time series gets a message with its respective count
- **Optimization**: Sparse time series (count=0) should NOT generate messages

---

### 9. Message Memory Ownership
**Decision**: Processor always frees (success or failure)

**Memory Ownership Contract**:
1. Encoder allocates and enqueues pointer
2. If enqueue fails: encoder frees immediately
3. If enqueue succeeds: ownership transfers to queue
4. Processor dequeues and ALWAYS frees (success or failure)

**Encoder Side**:
```c
uint8_t* payload = k_malloc(cbor_len);
// ... encode ...

int rc = k_msgq_put(&metrics_queue, &msg, K_NO_WAIT);
if (rc != 0) {
    k_free(payload);  // Enqueue failed - encoder frees
    return -ENOMEM;
}
// Ownership transferred - processor will free
```

**Processor Side**:
```c
k_msgq_get(&metrics_queue, &msg, K_NO_WAIT);
int rc = spotflow_mqtt_publish_ingest_cbor_msg(msg.payload, msg.len);
// ... handle -EAGAIN retry loop ...
k_free(msg.payload);  // ALWAYS free (success or failure)
```

---

### 10. Linear Search Performance
**Decision**: O(n) acceptable for max_timeseries ≤ 256

**Implementation**: Use linear search in `find_or_create_timeseries()` as specified. Hash table optimization deferred to future version.

**Performance Note**:
```c
// Performance Note: O(n) linear search is acceptable for typical use cases
// where max_timeseries ≤ 256. For larger cardinalities, consider hash table
// optimization in future version.
for (uint16_t i = 0; i < ctx->timeseries_capacity; i++) {
    if (ctx->timeseries[i].active &&
        ctx->timeseries[i].dimension_hash == dim_hash) {
        // Found match
    }
}
```

---

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1-2)

**Goal**: Implement metric registration, basic aggregation, CBOR encoding

#### 1.1 Directory Structure
```
modules/lib/spotflow/zephyr/src/
├── metrics/
│   ├── spotflow_metrics.h              (PUBLIC API)
│   ├── spotflow_metrics_backend.c      (Registration, reporting API)
│   ├── spotflow_metrics_backend.h      (Internal header)
│   ├── spotflow_metrics_aggregator.c   (Aggregation logic)
│   ├── spotflow_metrics_aggregator.h
│   ├── spotflow_metrics_cbor.c         (CBOR encoding)
│   ├── spotflow_metrics_cbor.h
│   ├── spotflow_metrics_queue.c        (Message queue)
│   ├── spotflow_metrics_queue.h
│   ├── spotflow_metrics_net.c          (Network layer integration)
│   ├── spotflow_metrics_net.h
│   └── CMakeLists.txt
```

#### 1.2 File Creation Checklist

- [ ] Create `modules/lib/spotflow/zephyr/include/spotflow/metrics/spotflow_metrics.h`
  - Public API declarations (10 functions)
  - Type definitions (`spotflow_metric_t`, `spotflow_dimension_t`, enums)
  - Error code documentation

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_backend.c`
  - Metric registration functions (4 functions)
  - Metric reporting functions (6 functions)
  - Registry management (static array)
  - Name normalization (lowercase conversion)

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_backend.h`
  - Internal structures (`struct spotflow_metric`)
  - Registry interface
  - Constants (max name length, etc.)

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_aggregator.c`
  - `aggregator_init()`
  - `aggregator_register_metric()` - allocate context, time series pool
  - `aggregator_report_value()` - find/create time series, update aggregation
  - Timer callbacks for window expiration
  - FNV-1a hashing implementation
  - Dimension comparison for collision detection

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_aggregator.h`
  - Aggregator context structure
  - Time series state structure
  - Internal function declarations

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_cbor.c`
  - `spotflow_metrics_cbor_encode()` - encode one time series to CBOR
  - CBOR key constants (0x00-0x18)
  - Handle float vs int encoding
  - Handle aggregated vs PT0S (immediate) messages

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_cbor.h`
  - CBOR encoding function signature
  - Message structure definition

#### 1.3 Key Implementation Details

**Metric Name Normalization** (backend):
```c
void normalize_metric_name(const char* name, char* normalized, size_t len) {
    for (size_t i = 0; i < len - 1 && name[i] != '\0'; i++) {
        normalized[i] = tolower((unsigned char)name[i]);
    }
    normalized[len - 1] = '\0';
}
```

**FNV-1a Hash** (aggregator):
```c
uint32_t fnv1a_hash_dimensions(const spotflow_dimension_t* dimensions, uint8_t count) {
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (uint8_t i = 0; i < count; i++) {
        // Hash key
        for (const char* p = dimensions[i].key; *p; p++) {
            hash ^= (uint32_t)*p;
            hash *= 16777619u;  // FNV prime
        }
        // Hash value
        for (const char* p = dimensions[i].value; *p; p++) {
            hash ^= (uint32_t)*p;
            hash *= 16777619u;
        }
    }
    return hash;
}
```

**Sequence Number Increment** (CBOR encoding):
```c
// sequenceNumber (per-metric, incremented for EACH message)
// NOTE: For dimensional metrics with N active time series, sequence number
// advances by N per aggregation interval (one increment per message)
succ = succ && zcbor_uint32_put(state, KEY_SEQUENCE_NUMBER);
succ = succ && zcbor_uint64_put(state, metric->sequence_number++);
```

---

### Phase 2: Message Queue and Network Integration (Week 2-3)

#### 2.1 Message Queue Implementation

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_queue.c`
  - Message queue initialization (`k_msgq_define`)
  - Enqueue function (non-blocking)
  - Dequeue function (called from processor)
  - Queue full handling (drop oldest, log warning)

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_queue.h`
  - Message structure: `{ uint8_t* payload; size_t len; }`
  - Queue size: `CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE`

**Message Structure**:
```c
struct metric_message {
    uint8_t* payload;  // CBOR-encoded data (allocated)
    size_t len;        // Payload length
};
```

**Queue Definition**:
```c
K_MSGQ_DEFINE(metrics_queue, sizeof(struct metric_message),
              CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE, 4);
```

#### 2.2 Network Layer Implementation

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_net.c`
  - `spotflow_metrics_net_init()` - initialize network layer
  - `spotflow_poll_and_process_enqueued_metrics()` - dequeue and publish
  - MQTT publish with -EAGAIN retry loop
  - Memory cleanup (always free payload)

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/spotflow_metrics_net.h`
  - Public function declarations for processor integration

**Network Layer Implementation**:
```c
int spotflow_poll_and_process_enqueued_metrics(void)
{
    struct metric_message msg;
    int rc = k_msgq_get(&metrics_queue, &msg, K_NO_WAIT);
    if (rc != 0) {
        return 0;  // Queue empty
    }

    // Infinite retry loop for transient errors (preserves message ordering)
    do {
        rc = spotflow_mqtt_publish_ingest_cbor_msg(msg.payload, msg.len);
        if (rc == -EAGAIN) {
            LOG_DBG("MQTT busy, retrying...");
            k_sleep(K_MSEC(10));
        }
    } while (rc == -EAGAIN);

    if (rc != 0) {
        LOG_WRN("Failed to publish metric message: %d", rc);
        // Permanent error - message lost
    }

    k_free(msg.payload);  // ALWAYS free (success or failure)
    return (rc == 0) ? 1 : rc;
}
```

#### 2.3 Processor Integration

- [ ] Modify `modules/lib/spotflow/zephyr/src/net/spotflow_processor.c`
  - Add include: `#include "metrics/spotflow_metrics_net.h"` (after line 20)
  - Add init call in `spotflow_mqtt_thread_entry()` after line 54
  - Insert metrics polling in `process_config_coredumps_or_logs()` lines 77-78

---

### Phase 3: Kconfig and Build System (Week 3)

#### 3.1 Kconfig Options

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/Kconfig`
```kconfig
config SPOTFLOW_METRICS
	bool "Enable metrics collection and reporting"
	default n
	help
	  Enable Spotflow metrics subsystem for collecting and reporting
	  application and system metrics to the cloud platform.

if SPOTFLOW_METRICS

config SPOTFLOW_METRICS_QUEUE_SIZE
	int "Metrics message queue size"
	default 16
	range 4 64
	help
	  Number of metric messages that can be buffered before transmission.
	  Larger values reduce message loss but increase memory usage.

config SPOTFLOW_METRICS_CBOR_BUFFER_SIZE
	int "CBOR encoding buffer size (bytes)"
	default 512
	range 128 2048
	help
	  Maximum size for CBOR-encoded metric messages.
	  Increase if using many dimensions or large metric names.

config SPOTFLOW_METRICS_MAX_REGISTERED
	int "Maximum number of registered metrics"
	default 32
	range 4 128
	help
	  Maximum number of metrics that can be registered simultaneously.
	  Each metric consumes approximately 128 bytes of RAM.

config SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL
	int "Default aggregation interval"
	default 1
	range 0 3
	help
	  Default aggregation period for metrics:
	  0 = PT0S (no aggregation, immediate reporting)
	  1 = PT1M (1 minute aggregation) - RECOMMENDED
	  2 = PT10M (10 minute aggregation)
	  3 = PT1H (1 hour aggregation)

config SPOTFLOW_METRICS_MAX_DIMENSIONS_PER_METRIC
	int "Maximum dimensions per metric"
	default 8
	range 1 16
	help
	  Maximum number of dimension key-value pairs per metric.
	  Each dimension adds approximately 160 bytes per time series.

config HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS
	int "Additional heap memory pool size for metrics (bytes)"
	default 16384
	range 4096 65536
	help
	  Heap memory required for metrics subsystem.

	  Formula: 2048 + SUM(128 + 96 × max_timeseries[i])

	  Examples:
	  - 10 dimensionless metrics: ~3.3KB
	  - 10 dimensional (ts=16): ~17.4KB
	  - 32 metrics (ts=16 each): ~53KB

	  Recommendation: Add 25% safety margin

endif # SPOTFLOW_METRICS
```

- [ ] Update `modules/lib/spotflow/zephyr/Kconfig`
  - Add: `rsource "src/metrics/Kconfig"`

#### 3.2 CMakeLists.txt

- [ ] Create `modules/lib/spotflow/zephyr/src/metrics/CMakeLists.txt`
```cmake
if(CONFIG_SPOTFLOW_METRICS)
    zephyr_library_sources(
        spotflow_metrics_backend.c
        spotflow_metrics_aggregator.c
        spotflow_metrics_cbor.c
        spotflow_metrics_queue.c
        spotflow_metrics_net.c
    )

    zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
    zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()
```

- [ ] Update `modules/lib/spotflow/zephyr/src/CMakeLists.txt`
  - Add: `add_subdirectory_ifdef(CONFIG_SPOTFLOW_METRICS metrics)`

- [ ] Update `modules/lib/spotflow/zephyr/CMakeLists.txt`
  - Ensure metrics header directory added to include path

---

### Phase 4: System Metrics (Optional, Week 4)

**Note**: System metrics are optional auto-collection feature. Can be implemented after core metrics working.

#### 4.1 System Metrics Structure
```
modules/lib/spotflow/zephyr/src/metrics/system/
├── spotflow_system_metrics.c           (Main init, periodic work queue)
├── spotflow_system_metrics.h           (Internal header)
├── spotflow_system_metrics_memory.c    (Memory/heap collectors)
├── spotflow_system_metrics_network.c   (Network stats collector)
├── spotflow_system_metrics_cpu.c       (CPU utilization collector)
├── spotflow_system_metrics_connection.c (Connection state)
├── spotflow_system_metrics_reset.c     (Reset cause - boot-time only)
├── CMakeLists.txt
└── Kconfig
```

#### 4.2 System Metrics Implementation

- [ ] Implement periodic collection with `k_work_delayable`
- [ ] Integrate with Zephyr APIs:
  - `sys_mem_stats_get()` for memory
  - `net_if_foreach()` + `stats.bytes.*` for network
  - `k_thread_runtime_stats_all_get()` for CPU (delta calculation)
  - `hwinfo_get_reset_cause()` for reset cause (boot-time only)
  - MQTT callback for connection state (event + periodic)

- [ ] Network interface iteration:
```c
STRUCT_SECTION_FOREACH(net_if, iface) {
    if (!net_if_is_up(iface)) continue;
    net_if_get_name(iface, iface_name, sizeof(iface_name));
    // Report with "interface" dimension
}
```

- [ ] CPU delta calculation:
```c
static uint64_t last_total_cycles = 0;
static uint64_t last_idle_cycles = 0;
static bool first_collection = true;

void collect_cpu_utilization(void) {
    k_thread_runtime_stats_t stats;
    k_thread_runtime_stats_all_get(&stats);

    uint64_t total = stats.execution_cycles + stats.idle_cycles;

    if (first_collection) {
        last_total_cycles = total;
        last_idle_cycles = stats.idle_cycles;
        first_collection = false;
        return;  // Skip first collection
    }

    uint64_t delta_total = total - last_total_cycles;
    uint64_t delta_idle = stats.idle_cycles - last_idle_cycles;

    double cpu_percent = 0.0;
    if (delta_total > 0) {
        cpu_percent = (1.0 - ((double)delta_idle / delta_total)) * 100.0;
    }

    spotflow_report_metric_float(cpu_metric, cpu_percent);

    last_total_cycles = total;
    last_idle_cycles = stats.idle_cycles;
}
```

#### 4.3 System Metrics Kconfig

- [ ] Add to `modules/lib/spotflow/zephyr/src/metrics/system/Kconfig`:
```kconfig
config SPOTFLOW_METRICS_SYSTEM
	bool "Enable system metrics auto-collection"
	depends on SPOTFLOW_METRICS
	default n

if SPOTFLOW_METRICS_SYSTEM

config SPOTFLOW_METRICS_SYSTEM_MEMORY
	bool "Collect memory usage metrics"
	default y

config SPOTFLOW_METRICS_SYSTEM_NETWORK
	bool "Collect network traffic metrics"
	depends on NETWORKING
	default y

config SPOTFLOW_METRICS_SYSTEM_CPU
	bool "Collect CPU utilization metrics"
	select THREAD_RUNTIME_STATS
	select THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
	default n

config SPOTFLOW_METRICS_SYSTEM_CONNECTION
	bool "Collect connection state metrics"
	default y

config SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
	bool "Report reset cause on boot"
	depends on HWINFO
	default y

config SPOTFLOW_METRICS_SYSTEM_INTERVAL
	int "System metrics collection interval (seconds)"
	default 60
	range 10 3600

config HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM
	int "Additional heap memory pool size for system metrics (bytes)"
	default 16384
	range 4096 65536

endif # SPOTFLOW_METRICS_SYSTEM
```

---

### Phase 5: Testing (Week 4-5)

#### 5.1 Unit Tests

**Test Framework**: Zephyr ztest

- [ ] Create `tests/unit/metrics/test_registry.c`
  - Test metric registration (success, duplicate, limit)
  - Test name normalization (uppercase → lowercase)
  - Test registration limits (max metrics)

- [ ] Create `tests/unit/metrics/test_aggregation.c`
  - Test dimensionless aggregation (sum, count, min, max)
  - Test dimensional aggregation (hash, time series creation)
  - Test overflow detection (sum truncation flag)
  - Test window expiration (timer callbacks)

- [ ] Create `tests/unit/metrics/test_cbor.c`
  - Test CBOR encoding (all field types)
  - Test PT0S vs aggregated messages
  - Test sequence number increment
  - Validate with CBOR decoder (Python cbor2 library)

- [ ] Create `tests/unit/metrics/test_hash.c`
  - Test FNV-1a hash function
  - Test collision handling
  - Test dimension comparison

#### 5.2 Integration Tests

- [ ] Create `tests/integration/metrics/test_e2e.c`
  - Register metric → report values → verify MQTT message
  - Test queue overflow (drop oldest)
  - Test MQTT publish failure handling
  - Test -EAGAIN retry loop

#### 5.3 System Tests

- [ ] Manual testing with real device
  - All metrics enabled, verify dashboard displays data
  - Individual metric enable/disable
  - Collection interval changes (30s, 60s, 300s)
  - Memory usage over 24 hours
  - CPU impact measurement

---

## Implementation Best Practices

### Error Handling

1. **Always check return values**:
```c
int rc = spotflow_register_metric_float("temperature");
if (rc == NULL) {
    LOG_ERR("Failed to register metric");
    return -ENOMEM;
}
```

2. **Use appropriate error codes**:
   - `-EINVAL`: Invalid parameter
   - `-ENOMEM`: Out of memory
   - `-ENOSPC`: No space (cardinality limit)
   - `-EBUSY`: Resource busy

3. **Log all errors with context**:
```c
LOG_ERR("Failed to register metric '%s': %d", name, rc);
```

### Memory Management

1. **Free on all error paths**:
```c
uint8_t* buf = k_malloc(size);
if (encode_failed) {
    k_free(buf);
    return -EINVAL;
}
```

2. **Clear ownership boundaries**:
   - Encoder allocates → queue owns → processor frees
   - Document with comments

3. **No dynamic allocation in fast path**:
   - Metric reporting should avoid malloc
   - Pre-allocate aggregation structures

### Thread Safety

1. **Use per-metric mutexes**:
```c
k_mutex_lock(&metric->lock, K_FOREVER);
// Update aggregation state
k_mutex_unlock(&metric->lock);
```

2. **Minimize lock duration**:
   - Hold lock only during aggregation update
   - Release before CBOR encoding

3. **No locks in CBOR encoding**:
   - Work on local copy of aggregation data

### Performance

1. **Batch operations where possible**:
   - Process multiple time series in one timer callback
   - Encode and enqueue in bulk

2. **Avoid string operations in hot path**:
   - Pre-normalize metric names on registration
   - Cache hash values

3. **Profile and measure**:
   - Use Zephyr timing API for measurement
   - Target <1ms for report API call

---

## Validation Checklist

Before considering implementation complete, verify:

### Functional Requirements

- [ ] All 10 public API functions implemented and tested
- [ ] Dimensionless metrics work (register, report, transmit)
- [ ] Dimensional metrics work (up to 8 dimensions)
- [ ] Aggregation works (sum, count, min, max)
- [ ] PT0S (immediate) reporting works
- [ ] PT1M, PT10M, PT1H aggregation works
- [ ] Sequence numbers increment correctly per message
- [ ] CBOR encoding matches protocol specification
- [ ] MQTT integration works (publish to correct topic)
- [ ] Queue buffering works (handles MQTT not ready)
- [ ] -EAGAIN retry loop works (preserves ordering)
- [ ] Error codes returned correctly
- [ ] Time series cardinality limit enforced (-ENOSPC)
- [ ] Overflow detection works (sum truncation flag)

### Non-Functional Requirements

- [ ] Memory usage within budget (heap pool sized correctly)
- [ ] No memory leaks (valgrind or Zephyr memory tracking)
- [ ] Thread-safe (concurrent reporting from multiple threads)
- [ ] Performance acceptable (<1ms API call latency)
- [ ] Compile-time optional (CONFIG_SPOTFLOW_METRICS=n works)
- [ ] Documentation complete (API reference, examples)

### System Metrics (if implemented)

- [ ] Memory metrics collected and reported
- [ ] Network metrics collected per interface
- [ ] CPU utilization delta calculation correct
- [ ] Connection state reported (event + periodic)
- [ ] Reset cause reported on boot
- [ ] Work queue integration correct
- [ ] Kconfig options work (individual enable/disable)

---

## Common Pitfalls to Avoid

### 1. Message Ordering
❌ **DON'T**: Requeue messages on -EAGAIN (breaks ordering)
✅ **DO**: Retry infinitely in place with `k_sleep()` delay

### 2. Memory Ownership
❌ **DON'T**: Free payload before enqueue completes
✅ **DO**: Transfer ownership to queue, processor always frees

### 3. Sequence Numbers
❌ **DON'T**: Increment on user report call
✅ **DO**: Increment during CBOR encoding (one per message)

### 4. Aggregation Window
❌ **DON'T**: Start timer on metric registration
✅ **DO**: Start timer on first report (sliding window)

### 5. Hash Collisions
❌ **DON'T**: Assume hash match means dimension match
✅ **DO**: Always do full dimension comparison on hash match

### 6. Sparse Time Series
❌ **DON'T**: Send messages for time series with count=0
✅ **DO**: Skip time series with no reports (optimization)

### 7. Metric Names
❌ **DON'T**: Allow uppercase or mixed case metric names
✅ **DO**: Normalize to lowercase on registration

### 8. CBOR Encoding
❌ **DON'T**: Include optional fields when not needed (e.g., count for PT0S)
✅ **DO**: Only encode fields required by protocol spec

---

## Quick Reference

### Key Files Locations

| Component | File Path |
|-----------|-----------|
| Public API | `include/spotflow/metrics/spotflow_metrics.h` |
| Backend | `src/metrics/spotflow_metrics_backend.c` |
| Aggregator | `src/metrics/spotflow_metrics_aggregator.c` |
| CBOR | `src/metrics/spotflow_metrics_cbor.c` |
| Queue | `src/metrics/spotflow_metrics_queue.c` |
| Network | `src/metrics/spotflow_metrics_net.c` |
| Processor Integration | `src/net/spotflow_processor.c:77-78` |
| Kconfig | `src/metrics/Kconfig` |
| CMakeLists | `src/metrics/CMakeLists.txt` |

### API Functions

```c
// Registration (4 functions)
spotflow_metric_t* spotflow_register_metric_float(const char* name);
spotflow_metric_t* spotflow_register_metric_int(const char* name);
spotflow_metric_t* spotflow_register_metric_float_with_dimensions(const char* name, uint16_t max_timeseries, uint8_t max_dimensions);
spotflow_metric_t* spotflow_register_metric_int_with_dimensions(const char* name, uint16_t max_timeseries, uint8_t max_dimensions);

// Reporting (6 functions)
int spotflow_report_metric_int(spotflow_metric_t* metric, int64_t value);
int spotflow_report_metric_float(spotflow_metric_t* metric, double value);
int spotflow_report_metric_int_with_dimensions(spotflow_metric_t* metric, int64_t value, const spotflow_dimension_t* dimensions, uint8_t dimension_count);
int spotflow_report_metric_float_with_dimensions(spotflow_metric_t* metric, double value, const spotflow_dimension_t* dimensions, uint8_t dimension_count);
int spotflow_report_event(spotflow_metric_t* metric);
int spotflow_report_event_with_dimensions(spotflow_metric_t* metric, const spotflow_dimension_t* dimensions, uint8_t dimension_count);
```

### CBOR Protocol Keys

```c
#define KEY_MESSAGE_TYPE           0x00
#define KEY_LABELS                 0x05
#define KEY_DEVICE_UPTIME_MS       0x06
#define KEY_SEQUENCE_NUMBER        0x0D
#define KEY_METRIC_NAME            0x10
#define KEY_AGGREGATION_INTERVAL   0x11
#define KEY_BATCH                  0x12  // RESERVED
#define KEY_SUM                    0x13
#define KEY_SUM_TRUNCATED          0x14
#define KEY_COUNT                  0x15
#define KEY_MIN                    0x16
#define KEY_MAX                    0x17
#define KEY_SAMPLES                0x18  // RESERVED

#define METRIC_MESSAGE_TYPE        0x05
```

---

## Support and References

### Design Documents
- See `design/metrics/*.md` for complete specifications
- IMPLEMENTATION_CHECKLIST.md (this document)

### Zephyr Documentation
- Work Queues: https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html
- Message Queues: https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html
- CBOR: https://docs.zephyrproject.org/latest/services/serialization/zcbor.html

### Testing
- Ztest Framework: https://docs.zephyrproject.org/latest/develop/test/ztest.html
- CBOR Validation: https://cbor.me/ or Python cbor2 library

---

**Last Updated**: 2025-12-08
**Status**: Ready for Implementation ✅
**Design Completeness**: 95%+
