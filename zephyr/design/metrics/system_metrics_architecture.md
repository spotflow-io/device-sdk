# System Metrics Architecture Specification

**Version:** 1.0
**Status:** Draft
**Author:** Spotflow SDK Architecture Team
**Date:** 2025-12-06

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-12-06 | Architecture Team | Initial version |

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Requirements](#2-requirements)
3. [Architecture Overview](#3-architecture-overview)
4. [Component Responsibilities](#4-component-responsibilities)
5. [Zephyr API Integration Points](#5-zephyr-api-integration-points)
6. [Data Models](#6-data-models)
7. [Configuration Options](#7-configuration-options)
8. [Initialization and Lifecycle](#8-initialization-and-lifecycle)
9. [Performance Considerations](#9-performance-considerations)
10. [Testing Strategy](#10-testing-strategy)
11. [References](#11-references)

---

## 1. Introduction

### 1.1 Purpose

This document specifies the architecture for the **System Metrics** subsystem of the Spotflow SDK for Zephyr RTOS. System metrics are automatically collected device telemetry data (memory, network, CPU, etc.) that require **zero application code** to enable - users simply configure them via Kconfig.

### 1.2 Scope

This architecture covers:

- Auto-collection of system-level metrics from Zephyr kernel APIs
- Integration with the core Spotflow metrics subsystem
- Kconfig-based configuration (no code required)
- Periodic collection using Zephyr work queues
- Memory, heap, network, CPU, and connection state metrics

**Out of Scope:**
- Application-defined custom metrics (handled by core metrics API)
- Cloud-side metric aggregation and dashboards
- Metrics already provided by existing SDK features (uptime, reboot reason, crash count)
- Log statistics (cloud-inferable from sequence numbers)

### 1.3 Goals

1. **Zero Application Code**: Enable system metrics purely through Kconfig
2. **Minimal Overhead**: Low memory and CPU impact when enabled
3. **Compile-Time Optional**: No overhead when disabled
4. **Standards Compliant**: Follow Zephyr RTOS patterns and best practices
5. **Dashboard Ready**: Provide metrics needed for the Spotflow dashboard

### 1.4 Non-Goals

- Custom metric definitions (use core metrics API)
- Real-time metric reporting (uses periodic sampling)
- Historical metric storage on device (send to cloud immediately)

---

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-1 | System shall collect memory usage (free/total) from Zephyr kernel | MUST |
| FR-2 | System shall collect heap usage (free/allocated) from Zephyr kernel | MUST |
| FR-3 | System shall collect network statistics (TX/RX bytes) from Zephyr network stack | MUST |
| FR-4 | System shall collect MQTT connection state changes | MUST |
| FR-5 | System shall optionally collect CPU utilization percentage | SHOULD |
| FR-6 | Each metric category shall be independently enable/disable via Kconfig | MUST |
| FR-7 | Collection interval shall be configurable via Kconfig (10-3600 seconds) | MUST |
| FR-8 | System metrics shall use the core metrics subsystem for reporting | MUST |
| FR-9 | System metrics shall auto-register on SDK initialization | MUST |
| FR-10 | System metrics shall require zero application code | MUST |

### 2.2 Non-Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-1 | Memory overhead when all metrics enabled: < 2 KB RAM | MUST |
| NFR-2 | Memory overhead when disabled: 0 bytes | MUST |
| NFR-3 | CPU overhead per collection cycle: < 10 ms | MUST |
| NFR-4 | Collection shall not block critical SDK operations | MUST |
| NFR-5 | System shall handle Zephyr API failures gracefully | MUST |
| NFR-6 | System shall be thread-safe | MUST |
| NFR-7 | Documentation shall be complete and accurate | MUST |

### 2.3 Constraints

| ID | Constraint |
|----|------------|
| C-1 | Must use only Zephyr RTOS APIs (no custom kernel modifications) |
| C-2 | Must compile on all Zephyr-supported platforms |
| C-3 | Must not require dedicated thread (use system work queue) |
| C-4 | Must follow existing SDK patterns (logs, coredumps) |
| C-5 | Must not duplicate existing SDK telemetry (uptime, reboot reason) |

---

## 3. Architecture Overview

### 3.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Application Layer                           │
│                  (No code required)                             │
└─────────────────────────────────────────────────────────────────┘
                             ▲
                             │ (Kconfig only)
                             │
┌─────────────────────────────────────────────────────────────────┐
│              System Metrics Subsystem                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │   Memory     │  │   Network    │  │     CPU      │          │
│  │  Collector   │  │  Collector   │  │  Collector   │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                 │                 │                   │
│         └─────────────────┴─────────────────┘                   │
│                           │                                     │
│                  ┌────────▼────────┐                            │
│                  │   Work Queue    │                            │
│                  │  Handler        │                            │
│                  │  (Periodic)     │                            │
│                  └────────┬────────┘                            │
│                           │                                     │
│                  ┌────────▼────────┐                            │
│                  │  Auto-Register  │                            │
│                  │   & Report      │                            │
│                  └────────┬────────┘                            │
└───────────────────────────┼─────────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────────┐
│              Core Metrics Subsystem                             │
│  ┌────────────────────────────────────────────────────────┐    │
│  │  spotflow_register_metric_*()                          │    │
│  │  spotflow_report_metric_int/float*()                   │    │
│  │  Aggregation, CBOR encoding, MQTT publishing           │    │
│  └────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
                       MQTT Broker
```

### 3.2 Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  src/metrics/system/                                        │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics.c                         │    │
│  │  - Initialization                                   │    │
│  │  - Work queue handler                               │    │
│  │  - Auto-registration                                │    │
│  │  - Periodic collection orchestration                │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics_memory.c                  │    │
│  │  - sys_mem_stats_get()                              │    │
│  │  - Report system_memory_free_bytes                  │    │
│  │  - Report system_memory_total_bytes                 │    │
│  │  - Report system_heap_free_bytes                    │    │
│  │  - Report system_heap_allocated_bytes               │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics_network.c                 │    │
│  │  - net_if_get_default()                             │    │
│  │  - net_if.stats.bytes.sent/received                 │    │
│  │  - Report system_network_sent_bytes                 │    │
│  │  - Report system_network_received_bytes             │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics_cpu.c                     │    │
│  │  - k_thread_runtime_stats_all_get()                 │    │
│  │  - Calculate CPU utilization percentage             │    │
│  │  - Report system_cpu_utilization_percent            │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics_connection.c              │    │
│  │  - MQTT connection event listener                  │    │
│  │  - Report system_connection_state                   │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics_reset.c                   │    │
│  │  - hwinfo_get_reset_cause()                         │    │
│  │  - Report system_reset_cause (once on boot)         │    │
│  │  - hwinfo_clear_reset_cause()                       │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  spotflow_system_metrics.h                         │    │
│  │  - Internal APIs                                    │    │
│  │  - Data structures                                  │    │
│  └────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 Data Flow

```
1. SDK Initialization
   └─> System Metrics Init (if CONFIG_SPOTFLOW_METRICS_SYSTEM=y)
       └─> Register metrics with core subsystem
       └─> Report reset cause once (if CONFIG_...RESET_CAUSE=y)
           └─> hwinfo_get_reset_cause()
           └─> spotflow_report_metric_int(reset_cause_metric, bitmask)
           └─> hwinfo_clear_reset_cause()
       └─> Schedule initial work queue item

2. Periodic Collection (every INTERVAL seconds)
   ┌─> Work Queue Handler Triggered
   │   └─> Collect Memory Stats (if CONFIG_...MEMORY=y)
   │       └─> sys_mem_stats_get()
   │       └─> spotflow_report_metric_int(memory_free_metric, free_bytes)
   │       └─> spotflow_report_metric_int(memory_total_metric, total_bytes)
   │   └─> Collect Network Stats (if CONFIG_...NETWORK=y)
   │       └─> net_if_foreach() with labels
   │       └─> spotflow_report_metric_int_with_labels(network_sent_metric, bytes, dims, 1)
   │   └─> Collect CPU Stats (if CONFIG_...CPU=y)
   │       └─> k_thread_runtime_stats_all_get()
   │       └─> spotflow_report_metric_float(cpu_utilization_metric, percent)
   │   └─> Reschedule work item for next interval
   └─> Core Metrics Subsystem
       └─> Aggregate (if period != PT0S)
       └─> Encode CBOR
       └─> Enqueue MQTT message

3. Connection State Changes (event-driven)
   └─> MQTT Connection Event
       └─> spotflow_report_metric_int(connection_state_metric, 0 or 1)
```

---

## 4. Component Responsibilities

### 4.1 System Metrics Core Module

**File:** `src/metrics/system/spotflow_system_metrics.c`

**Responsibilities:**
- Initialize the system metrics subsystem
- Register all enabled metrics with core subsystem
- Manage periodic work queue (k_work_delayable)
- Orchestrate collection from specialized collectors
- Handle lifecycle (init, start, stop)

**Key Functions:**
```c
// Initialize system metrics subsystem (called from SDK init)
int spotflow_system_metrics_init(void);

// Work queue handler (periodic collection)
static void system_metrics_work_handler(struct k_work *work);

// Internal registration helper
static int register_system_metrics(void);
```

### 4.2 Memory Metrics Collector

**File:** `src/metrics/system/spotflow_system_metrics_memory.c`

**Responsibilities:**
- Query Zephyr memory statistics API
- Report system memory (free/total) metrics
- Report heap memory (free/allocated) metrics
- Handle API failures gracefully

**Zephyr APIs Used:**
- `sys_mem_stats_get()`
- `sys_mem_kernel_pool_ptr_get()`

### 4.3 Network Metrics Collector

**File:** `src/metrics/system/spotflow_system_metrics_network.c`

**Responsibilities:**
- Query default network interface statistics
- Report TX/RX byte counters
- Handle missing network interfaces

**Zephyr APIs Used:**
- `net_if_get_default()`
- `net_if.stats.bytes.sent`
- `net_if.stats.bytes.received`

### 4.4 CPU Metrics Collector

**File:** `src/metrics/system/spotflow_system_metrics_cpu.c`

**Responsibilities:**
- Query thread runtime statistics
- Calculate overall CPU utilization percentage
- Report CPU utilization metric

**Zephyr APIs Used:**
- `k_thread_runtime_stats_all_get()`
- `k_cycle_stats`

**Note:** Requires `CONFIG_THREAD_RUNTIME_STATS=y`

### 4.5 Connection Metrics Collector

**File:** `src/metrics/system/spotflow_system_metrics_connection.c`

**Responsibilities:**
- Listen to MQTT connection state changes
- Report connection state metric (1=connected, 0=disconnected)
- Event-driven (not periodic)

**Integration Point:**
- MQTT connection event callback

---

## 5. Zephyr API Integration Points

### 5.1 Memory Statistics API

**Header:** `<zephyr/sys/mem_stats.h>`

**Key Functions:**
```c
int sys_mem_stats_get(struct sys_memory_stats *stats);
```

**Structure:**
```c
struct sys_memory_stats {
    size_t free_bytes;
    size_t allocated_bytes;
    size_t max_allocated_bytes;
};
```

**Usage:**
```c
struct sys_memory_stats stats;
int rc = sys_mem_stats_get(&stats);
if (rc == 0) {
    spotflow_report_metric_int(system_memory_free_metric, stats.free_bytes);
    spotflow_report_metric_int(system_memory_total_metric,
                          stats.free_bytes + stats.allocated_bytes);
}
```

**Availability:** Always available (no Kconfig dependency)

### 5.2 Network Statistics API

**Header:** `<zephyr/net/net_if.h>`

**Key Functions:**
```c
struct net_if *net_if_get_default(void);
```

**Structure:**
```c
struct net_if {
    struct net_stats stats;
    // ... other fields
};

struct net_stats {
    struct net_stats_bytes bytes;
    // ... other fields
};

struct net_stats_bytes {
    uint64_t sent;
    uint64_t received;
};
```

**Usage (single interface):**
```c
struct net_if *iface = net_if_get_default();
if (iface && CONFIG_NET_STATISTICS) {
    char iface_name[16];
    net_if_get_name(iface, iface_name, sizeof(iface_name));
    spotflow_label_t dims[] = {{ .key = "interface", .value = iface_name }};

    spotflow_report_metric_int_with_labels(system_network_sent_metric,
                                          iface->stats.bytes.sent, dims, 1);
    spotflow_report_metric_int_with_labels(system_network_received_metric,
                                          iface->stats.bytes.received, dims, 1);
}
```

**Usage (iterate all interfaces):**
```c
#if CONFIG_NET_STATISTICS
static void collect_network_stats_per_interface(void) {
    struct net_if *iface;
    char iface_name[16];
    spotflow_label_t dims[1];

    // Iterate all network interfaces
    STRUCT_SECTION_FOREACH(net_if, iface) {
        if (!net_if_is_up(iface)) {
            continue;  // Skip interfaces that are down
        }

        net_if_get_name(iface, iface_name, sizeof(iface_name));
        dims[0].key = "interface";
        dims[0].value = iface_name;

        // Report TX bytes
        spotflow_report_metric_int_with_labels(
            g_system_metrics_ctx.network_sent_metric,
            iface->stats.bytes.sent,
            dims, 1);

        // Report RX bytes
        spotflow_report_metric_int_with_labels(
            g_system_metrics_ctx.network_received_metric,
            iface->stats.bytes.received,
            dims, 1);
    }
}
#endif
```

**Dependencies:**
- `CONFIG_NET_STATISTICS=y`
- `CONFIG_NETWORKING=y`

### 5.3 Thread Runtime Statistics API

**Header:** `<zephyr/kernel.h>`

**Key Functions:**
```c
int k_thread_runtime_stats_all_get(k_thread_runtime_stats_t *stats);
```

**Structure:**
```c
typedef struct {
    uint64_t total_cycles;
    uint64_t idle_cycles;
    // ... other fields
} k_thread_runtime_stats_t;
```

**Usage (Delta Calculation for Average CPU Utilization):**

The CPU utilization metric calculates the **average CPU utilization since the last collection** using delta calculations. This provides a meaningful measurement over the collection interval (e.g., 60 seconds) rather than an instantaneous snapshot.

```c
// Context structure (declared in spotflow_system_metrics.h)
static struct {
    // ... other fields ...
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
    uint64_t last_total_cycles;
    uint64_t last_idle_cycles;
    bool cpu_first_collection;
#endif
} g_system_metrics_ctx;

#if CONFIG_THREAD_RUNTIME_STATS
static void collect_cpu_metrics(void) {
    k_thread_runtime_stats_t stats;
    int rc = k_thread_runtime_stats_all_get(&stats);
    if (rc != 0) {
        LOG_ERR("Failed to get runtime stats: %d", rc);
        return;
    }

    // Calculate total cycles (execution + idle)
    uint64_t total_cycles = stats.execution_cycles + stats.idle_cycles;
    uint64_t idle_cycles = stats.idle_cycles;

    // Skip first collection (no previous data for delta)
    if (g_system_metrics_ctx.cpu_first_collection) {
        g_system_metrics_ctx.last_total_cycles = total_cycles;
        g_system_metrics_ctx.last_idle_cycles = idle_cycles;
        g_system_metrics_ctx.cpu_first_collection = false;
        LOG_DBG("CPU metrics first collection, baseline established");
        return;
    }

    // Calculate delta since last collection
    uint64_t delta_total = total_cycles - g_system_metrics_ctx.last_total_cycles;
    uint64_t delta_idle = idle_cycles - g_system_metrics_ctx.last_idle_cycles;

    // Calculate CPU utilization percentage: (1 - idle_ratio) * 100
    double cpu_percent = 0.0;
    if (delta_total > 0) {
        double idle_ratio = (double)delta_idle / (double)delta_total;
        cpu_percent = (1.0 - idle_ratio) * 100.0;
    }

    // Report metric
    spotflow_report_metric_float(g_system_metrics_ctx.cpu_utilization_metric,
                                  cpu_percent);

    // Save for next collection
    g_system_metrics_ctx.last_total_cycles = total_cycles;
    g_system_metrics_ctx.last_idle_cycles = idle_cycles;

    LOG_DBG("CPU utilization: %.2f%% (delta_total=%llu, delta_idle=%llu)",
            cpu_percent, delta_total, delta_idle);
}
#endif
```

**Key Implementation Details:**

1. **Delta Calculation**: Computes difference in cycles between collections
   - `delta_total = current_total_cycles - last_total_cycles`
   - `delta_idle = current_idle_cycles - last_idle_cycles`

2. **First Collection Handling**: Skips reporting on first collection (no baseline)
   - Sets `cpu_first_collection = false` after establishing baseline
   - Avoids invalid data from boot-time initialization

3. **Average Measurement**: Reports average over collection interval
   - For 60-second interval, reports average CPU % over that 60 seconds
   - Smooths out short-term spikes, more representative of system load

4. **Thread Safety**: Called from system work queue (single-threaded)
   - No mutex needed for context variables
   - Zephyr runtime stats API is thread-safe

**Dependencies:**
- `CONFIG_THREAD_RUNTIME_STATS=y`
- `CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS=y`

### 5.4 Work Queue API

**Header:** `<zephyr/kernel.h>`

**Key Functions:**
```c
void k_work_init_delayable(struct k_work_delayable *dwork,
                           k_work_handler_t handler);
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
```

**Usage:**
```c
static struct k_work_delayable system_metrics_work;

// In init:
k_work_init_delayable(&system_metrics_work, system_metrics_work_handler);
k_work_schedule(&system_metrics_work, K_SECONDS(interval));

// In handler:
static void system_metrics_work_handler(struct k_work *work) {
    // Collect metrics
    collect_all_metrics();

    // Reschedule
    k_work_schedule(&system_metrics_work,
                   K_SECONDS(CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL));
}
```

**Dependencies:** None (system work queue always available)

---

## 6. Data Models

### 6.1 Metric Registry

All system metrics follow the naming convention: `system_<category>_<metric>_<unit>`

| Metric Name | Type | Unit | Source API | Update Frequency | Kconfig |
|-------------|------|------|------------|------------------|---------|
| `system_memory_free_bytes` | int64 | bytes | `sys_mem_stats_get()` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY` |
| `system_memory_total_bytes` | int64 | bytes | `sys_mem_stats_get()` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY` |
| `system_heap_free_bytes` | int64 | bytes | `sys_mem_stats_get()` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP` |
| `system_heap_allocated_bytes` | int64 | bytes | `sys_mem_stats_get()` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP` |
| `system_network_sent_bytes` | int64 | bytes | `net_if.stats.bytes.sent` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK` |
| `system_network_received_bytes` | int64 | bytes | `net_if.stats.bytes.received` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK` |
| `system_cpu_utilization_percent` | float | percentage | `k_thread_runtime_stats_all_get()` | Periodic | `CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU` |
| `system_connection_state` | int64 | enum (0/1) | MQTT connection events | Event-driven | `CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION` |
| `system_reset_cause` | int64 | bitmask | `hwinfo_get_reset_cause()` | Boot-time | `CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE` |

**Notes:**
- All metrics use the default aggregation period (60 seconds) unless overridden
- Connection state and reset_cause use PT0S (event-based, no aggregation)
- Reset cause is reported once on boot (not periodic)
- All byte counters are cumulative (monotonically increasing)
- CPU utilization is an instantaneous sample (0.0 - 100.0)
- Network metrics are labeled (include "interface" label)

### 6.2 Internal Data Structures

```c
// System metrics context
struct spotflow_system_metrics_context {
    struct k_work_delayable work;           // Periodic work item
    uint32_t interval_seconds;              // Collection interval
    bool initialized;                       // Initialization flag

    // Metric handles
    spotflow_metric_t* memory_free_metric;
    spotflow_metric_t* memory_total_metric;
    spotflow_metric_t* network_sent_metric;          // Dimensional (interface)
    spotflow_metric_t* network_received_metric;      // Dimensional (interface)
    spotflow_metric_t* cpu_utilization_metric;
    spotflow_metric_t* connection_state_metric;
    spotflow_metric_t* reset_cause_metric;

    // CPU utilization delta tracking
    uint64_t last_total_cycles;         // Last total cycles for CPU delta calculation
    uint64_t last_idle_cycles;          // Last idle cycles for CPU delta calculation
    bool cpu_first_collection;          // True until first CPU collection completes
};

// Global context (static in spotflow_system_metrics.c)
static struct spotflow_system_metrics_context g_system_metrics_ctx;
```

---

## 7. Configuration Options

### 7.1 Kconfig Hierarchy

```kconfig
config SPOTFLOW_METRICS_SYSTEM
    bool "Enable Spotflow system metrics"
    depends on SPOTFLOW_METRICS
    default n
    help
      Enable automatic collection of system-level metrics (memory, network, CPU, etc.).
      These metrics are collected periodically and reported to the cloud without
      requiring any application code.

if SPOTFLOW_METRICS_SYSTEM

config SPOTFLOW_METRICS_SYSTEM_MEMORY
    bool "Enable memory metrics"
    default y
    help
      Collect system memory statistics (free/total bytes).
      Uses Zephyr sys_mem_stats_get() API.

config SPOTFLOW_METRICS_SYSTEM_HEAP
    bool "Enable heap metrics"
    depends on HEAP_MEM_POOL_SIZE > 0
    default y
    help
      Collect heap memory statistics (free/allocated bytes).
      Requires heap memory pool to be configured.

config SPOTFLOW_METRICS_SYSTEM_NETWORK
    bool "Enable network metrics"
    depends on NETWORKING && NET_STATISTICS
    default y
    help
      Collect network traffic statistics (TX/RX bytes).
      Requires networking and network statistics to be enabled.

config SPOTFLOW_METRICS_SYSTEM_CPU
    bool "Enable CPU utilization metrics"
    select THREAD_RUNTIME_STATS
    select THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
    default n
    help
      Collect CPU utilization percentage.
      WARNING: Enabling this adds overhead to thread scheduling.
      Only enable if CPU utilization monitoring is required.

config SPOTFLOW_METRICS_SYSTEM_CONNECTION
    bool "Enable connection state metrics"
    default y
    help
      Report MQTT connection state changes (connected=1, disconnected=0).
      Event-driven metric (not periodic).

config SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
    bool "Enable reset cause metric"
    depends on HWINFO
    default y
    help
      Report device reset cause on boot (power-on, watchdog, etc.).
      Event-driven metric (reported once on boot, not periodic).
      Requires HWINFO support in the platform.

config SPOTFLOW_METRICS_SYSTEM_INTERVAL
    int "System metrics collection interval (seconds)"
    range 10 3600
    default 60
    help
      How often to collect and report system metrics (10-3600 seconds).
      Default is 60 seconds (1 minute).
      Lower values increase network/CPU overhead.

config HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM
    int "Additional heap memory pool size for system metrics (bytes)"
    default 16384
    range 4096 65536
    help
      System metrics auto-collection requires heap memory for:
      - Metric registration (7 metrics × 128 bytes context)
      - Time series storage (network metrics have 4 time series)
      - CBOR encoding buffers

      Default 16KB is sufficient for all system metrics enabled.
      Reduce if only subset of metrics enabled:
      - Memory + Connection only: 4KB
      - Memory + Network: 8KB
      - All metrics: 16KB (default)

      This is added to CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS
      which is for application metrics.

endif # SPOTFLOW_METRICS_SYSTEM
```

### 7.2 Configuration Examples

**Minimal Configuration (memory + network only):**
```kconfig
CONFIG_SPOTFLOW_METRICS=y
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=n
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=n
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60
CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM=8192  # 8KB for minimal config
```

**Full Configuration (all metrics enabled):**
```kconfig
CONFIG_SPOTFLOW_METRICS=y
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=30
CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM=16384  # 16KB (default)
```

---

## 8. Initialization and Lifecycle

### 8.1 Initialization Sequence

```c
// Called from SDK initialization (early in boot)
int spotflow_init(void) {
    // ... other SDK init

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
    int rc = spotflow_system_metrics_init();
    if (rc < 0) {
        LOG_ERR("Failed to initialize system metrics: %d", rc);
        // Non-fatal - continue SDK initialization
    }
#endif

    // ... rest of SDK init
}

// System metrics initialization
int spotflow_system_metrics_init(void) {
    // Initialize context
    memset(&g_system_metrics_ctx, 0, sizeof(g_system_metrics_ctx));
    g_system_metrics_ctx.interval_seconds = CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL;

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
    // Initialize CPU delta tracking (first collection will establish baseline)
    g_system_metrics_ctx.cpu_first_collection = true;
#endif

    // Register metrics with core subsystem
    int rc = register_system_metrics();
    if (rc < 0) {
        LOG_ERR("Failed to register system metrics: %d", rc);
        return rc;
    }

    // Initialize work queue handler
    k_work_init_delayable(&g_system_metrics_ctx.work, system_metrics_work_handler);

    // Schedule first collection
    k_work_schedule(&g_system_metrics_ctx.work,
                   K_SECONDS(g_system_metrics_ctx.interval_seconds));

    g_system_metrics_ctx.initialized = true;
    LOG_INF("System metrics initialized (interval: %d seconds)",
            g_system_metrics_ctx.interval_seconds);

    return 0;
}
```

### 8.2 Metric Registration

```c
static int register_system_metrics(void) {
    // Register simple metrics
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY
    g_system_metrics_ctx.memory_free_metric =
        spotflow_register_metric_int("system_memory_free_bytes");
    if (!g_system_metrics_ctx.memory_free_metric) {
        LOG_ERR("Failed to register memory_free metric");
        return -ENOMEM;
    }

    g_system_metrics_ctx.memory_total_metric =
        spotflow_register_metric_int("system_memory_total_bytes");
    if (!g_system_metrics_ctx.memory_total_metric) {
        LOG_ERR("Failed to register memory_total metric");
        return -ENOMEM;
    }
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
    // Network metrics are labeled (interface dimension)
    g_system_metrics_ctx.network_sent_metric =
        spotflow_register_metric_int_with_labels(
            "system_network_sent_bytes",
            4,  // Max 4 network interfaces
            1   // One label: interface
        );
    if (!g_system_metrics_ctx.network_sent_metric) {
        LOG_ERR("Failed to register network_sent metric");
        return -ENOMEM;
    }

    g_system_metrics_ctx.network_received_metric =
        spotflow_register_metric_int_with_labels(
            "system_network_received_bytes",
            4,  // Max 4 network interfaces
            1   // One label: interface
        );
    if (!g_system_metrics_ctx.network_received_metric) {
        LOG_ERR("Failed to register network_received metric");
        return -ENOMEM;
    }
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
    g_system_metrics_ctx.cpu_utilization_metric =
        spotflow_register_metric_float("system_cpu_utilization_percent");
    if (!g_system_metrics_ctx.cpu_utilization_metric) {
        LOG_ERR("Failed to register cpu_utilization metric");
        return -ENOMEM;
    }
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
    g_system_metrics_ctx.connection_state_metric =
        spotflow_register_metric_int("system_connection_state");
    if (!g_system_metrics_ctx.connection_state_metric) {
        LOG_ERR("Failed to register connection_state metric");
        return -ENOMEM;
    }
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
    g_system_metrics_ctx.reset_cause_metric =
        spotflow_register_metric_int("system_reset_cause");
    if (!g_system_metrics_ctx.reset_cause_metric) {
        LOG_ERR("Failed to register reset_cause metric");
        return -ENOMEM;
    }
#endif

    return 0;
}
```

### 8.3 Periodic Collection

```c
static void system_metrics_work_handler(struct k_work *work) {
    if (!g_system_metrics_ctx.initialized) {
        return;
    }

    // Collect all enabled metrics
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY
    collect_memory_metrics();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
    collect_heap_metrics();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
    collect_network_metrics();
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
    collect_cpu_metrics();
#endif

    // Reschedule for next interval
    k_work_schedule(&g_system_metrics_ctx.work,
                   K_SECONDS(g_system_metrics_ctx.interval_seconds));
}
```

---

## 9. Performance Considerations

### 9.1 Memory Overhead

**RAM Usage (when all metrics enabled):**

| Component | Size (bytes) | Notes |
|-----------|--------------|-------|
| Work queue item | 48 | `struct k_work_delayable` |
| Context structure | 64 | Tracking state |
| Metric registrations | ~800 | 8 metrics × ~100 bytes each |
| **Total** | **~912 bytes** | **< 1 KB** |

**When disabled:** 0 bytes (compile-time excluded)

### 9.2 CPU Overhead

**Per Collection Cycle (~60 seconds):**

| Operation | Time (µs) | Notes |
|-----------|-----------|-------|
| Memory stats query | 10-50 | Kernel call |
| Network stats query | 5-20 | Struct access |
| CPU stats query | 100-500 | Thread iteration |
| Metric reporting | 50-200 per metric | CBOR encoding |
| **Total** | **~1000-3000 µs** | **< 3 ms** |

**Work Queue Execution:** Runs in cooperative thread, will not preempt higher priority tasks.

### 9.3 Network Overhead

**Per Collection Cycle:**

| Metric | CBOR Size (bytes) | Frequency |
|--------|-------------------|-----------|
| Memory (4 metrics) | ~120 | Every INTERVAL |
| Network (2 metrics) | ~60 | Every INTERVAL |
| CPU (1 metric) | ~30 | Every INTERVAL |
| Connection (1 metric) | ~25 | Event-driven |
| **Total** | **~235 bytes** | **Every 60s** |

**Monthly bandwidth:** ~10 MB (60-second interval, all metrics enabled)

### 9.4 Optimization Strategies

1. **Disable unused metrics:** Use Kconfig to compile out unused collectors
2. **Increase interval:** Use 300s (5 min) or 600s (10 min) for low-frequency monitoring
3. **Conditional CPU metrics:** Only enable when debugging performance issues
4. **Lazy initialization:** Metrics are only registered if enabled

---

## 10. Testing Strategy

### 10.1 Unit Tests

**Test File:** `tests/unit/metrics/system/test_system_metrics.c`

**Test Cases:**
1. **Initialization Tests**
   - Verify work queue initialization
   - Verify metric registration
   - Verify interval configuration

2. **Collection Tests**
   - Mock Zephyr APIs
   - Verify correct API calls
   - Verify error handling (API failures)

3. **Configuration Tests**
   - Test with different Kconfig combinations
   - Verify compile-time exclusion when disabled

### 10.2 Integration Tests

**Test File:** `tests/integration/metrics/test_system_metrics_integration.c`

**Test Cases:**
1. **End-to-End Collection**
   - Enable all metrics
   - Verify MQTT messages generated
   - Verify CBOR encoding correctness

2. **Periodic Collection**
   - Mock k_work_schedule
   - Verify rescheduling behavior
   - Verify interval adherence

3. **Error Handling**
   - Simulate Zephyr API failures
   - Verify graceful degradation
   - Verify logging

### 10.3 System Tests

**Manual Test Scenarios:**
1. Enable all metrics, verify dashboard displays data
2. Enable/disable individual metrics, verify correct subset reported
3. Change interval, verify collection frequency changes
4. Monitor memory usage over 24 hours
5. Monitor CPU impact with/without system metrics

---

## 11. References

### 11.1 Related Documents

- [Core Metrics Architecture](./architecture.md)
- [Core Metrics API Specification](./api_specification.md)
- [Ingestion Protocol Specification](./ingestion_protocol_specification.md)
- [Dashboard Specification](./specification/dashboard_spec.md)

### 11.2 Zephyr Documentation

- [Kernel Work Queue API](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html)
- [Memory Statistics API](https://docs.zephyrproject.org/latest/kernel/memory_management/sys_mem_blocks.html)
- [Network Statistics](https://docs.zephyrproject.org/latest/connectivity/networking/api/net_stats.html)
- [Thread Runtime Statistics](https://docs.zephyrproject.org/latest/kernel/services/threads/index.html)

### 11.3 Standards

- [ISO 8601 Duration Format](https://en.wikipedia.org/wiki/ISO_8601#Durations) (for period specification)
- [CBOR RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html)
- [MQTT 3.1.1](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)

---

## Appendix A: Glossary

| Term | Definition |
|------|------------|
| System Metrics | Auto-collected device telemetry requiring zero application code |
| Work Queue | Zephyr mechanism for deferred execution in thread context |
| Kconfig | Kernel configuration system used by Zephyr RTOS |
| CBOR | Concise Binary Object Representation (binary JSON) |
| PT0S | ISO 8601 duration representing "zero seconds" (event-based metric) |

---

## Appendix B: Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2025-12-06 | Use system work queue instead of dedicated thread | Minimal overhead, standard Zephyr pattern |
| 2025-12-06 | No log statistics metrics | Cloud can infer from sequence numbers |
| 2025-12-06 | CPU metrics default disabled | High overhead, opt-in only |
| 2025-12-06 | Individual metric enable flags | Fine-grained control, minimize overhead |
| 2025-12-06 | Default 60-second interval | Balance between timeliness and overhead |

---

**End of System Metrics Architecture Specification**
