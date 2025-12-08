# System Metrics API Specification

**Version:** 1.0
**Status:** Draft
**Author:** Spotflow SDK Architecture Team
**Date:** 2025-12-06

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Kconfig API](#2-kconfig-api)
3. [Metric Registry](#3-metric-registry)
4. [Configuration Examples](#4-configuration-examples)
5. [Troubleshooting Guide](#5-troubleshooting-guide)
6. [Migration Guide](#6-migration-guide)

---

## 1. Introduction

### 1.1 Purpose

This document specifies the user-facing API for the Spotflow System Metrics subsystem. Unlike the core metrics API which requires application code, **system metrics are configured purely through Kconfig** - no code is required.

### 1.2 Key Concepts

- **Zero Code Required:** Users only modify `prj.conf` or use menuconfig
- **Compile-Time Configuration:** Metrics are compiled in/out based on Kconfig
- **Auto-Registration:** Metrics are automatically registered on SDK initialization
- **Periodic Collection:** Metrics are collected at a configurable interval (default 60 seconds)
- **Event-Driven Connection Metrics:** MQTT connection state is reported on state changes

### 1.3 Prerequisites

- Spotflow SDK for Zephyr installed
- Core metrics subsystem enabled (`CONFIG_SPOTFLOW_METRICS=y`)
- Appropriate Zephyr features enabled for specific metrics (e.g., networking for network metrics)

---

## 2. Kconfig API

### 2.1 Master Enable Switch

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM`

**Type:** bool
**Default:** n
**Depends on:** `CONFIG_SPOTFLOW_METRICS`

**Description:**
Master enable switch for the system metrics subsystem. When enabled, system metrics are automatically collected and reported to the cloud based on the individual metric enable flags below.

**Usage:**
```kconfig
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
```

**Effect:**
- Compiles in system metrics subsystem code
- Initializes periodic work queue for collection
- Registers enabled metrics with core subsystem
- Adds ~900 bytes RAM overhead (all metrics enabled)

---

### 2.2 Individual Metric Categories

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY`

**Type:** bool
**Default:** y
**Depends on:** `CONFIG_SPOTFLOW_METRICS_SYSTEM`

**Description:**
Enable collection of system memory statistics (RAM usage).

**Metrics Enabled:**
- `system_memory_free_bytes` - Free memory in bytes
- `system_memory_total_bytes` - Total memory in bytes

**Zephyr API:**
Uses `sys_mem_stats_get()` from `<zephyr/sys/mem_stats.h>`

**Overhead:**
- RAM: ~200 bytes
- CPU: ~10-50 µs per collection cycle
- No additional Zephyr Kconfig requirements

**Usage:**
```kconfig
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
```

---

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP`

**Type:** bool
**Default:** y
**Depends on:** `CONFIG_SPOTFLOW_METRICS_SYSTEM && HEAP_MEM_POOL_SIZE > 0`

**Description:**
Enable collection of heap memory statistics.

**Metrics Enabled:**
- `system_heap_free_bytes` - Free heap memory in bytes
- `system_heap_allocated_bytes` - Allocated heap memory in bytes

**Zephyr API:**
Uses `sys_mem_stats_get()` from `<zephyr/sys/mem_stats.h>`

**Overhead:**
- RAM: ~200 bytes
- CPU: ~10-50 µs per collection cycle

**Dependencies:**
- Requires heap to be configured: `CONFIG_HEAP_MEM_POOL_SIZE > 0`

**Usage:**
```kconfig
CONFIG_HEAP_MEM_POOL_SIZE=16384
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y
```

---

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK`

**Type:** bool
**Default:** y
**Depends on:** `CONFIG_SPOTFLOW_METRICS_SYSTEM && CONFIG_NETWORKING && CONFIG_NET_STATISTICS`

**Description:**
Enable collection of network traffic statistics.

**Metrics Enabled:**
- `system_network_sent_bytes` - Total bytes sent (cumulative)
- `system_network_received_bytes` - Total bytes received (cumulative)

**Zephyr API:**
Queries default network interface statistics from `net_if_get_default()`

**Overhead:**
- RAM: ~200 bytes
- CPU: ~5-20 µs per collection cycle

**Dependencies:**
```kconfig
CONFIG_NETWORKING=y
CONFIG_NET_STATISTICS=y
```

**Usage:**
```kconfig
CONFIG_NETWORKING=y
CONFIG_NET_STATISTICS=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
```

---

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION`

**Type:** bool
**Default:** y
**Depends on:** `CONFIG_SPOTFLOW_METRICS_SYSTEM`

**Description:**
Enable reporting of MQTT connection state changes.

**Metrics Enabled:**
- `system_connection_state` - Connection state (1=connected, 0=disconnected)

**Behavior:**
- **Event-driven:** Reported immediately on connection state changes (PT0S period)
- **Non-aggregated:** Each state change generates an immediate MQTT message

**Overhead:**
- RAM: ~100 bytes
- CPU: Negligible (event-driven)

**Usage:**
```kconfig
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
```

---

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU`

**Type:** bool
**Default:** n
**Depends on:** `CONFIG_SPOTFLOW_METRICS_SYSTEM`
**Selects:** `CONFIG_THREAD_RUNTIME_STATS`, `CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS`

**Description:**
Enable collection of CPU utilization percentage.

**⚠️ WARNING:** Enabling this option adds overhead to thread scheduling. Only enable if CPU utilization monitoring is required.

**Metrics Enabled:**
- `system_cpu_utilization_percent` - CPU utilization (0.0-100.0%)

**Zephyr API:**
Uses `k_thread_runtime_stats_all_get()` from `<zephyr/kernel.h>`

**Overhead:**
- RAM: ~100 bytes
- CPU: ~100-500 µs per collection cycle
- **Thread switch overhead:** Zephyr adds timestamp collection on every thread switch

**Automatically Enables:**
```kconfig
CONFIG_THREAD_RUNTIME_STATS=y
CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS=y
```

**Usage:**
```kconfig
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y
```

**Recommendation:**
Leave disabled by default. Only enable when actively debugging CPU usage.

---

### 2.3 Collection Interval

#### `CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL`

**Type:** int
**Range:** 10-3600 seconds
**Default:** 60
**Depends on:** `CONFIG_SPOTFLOW_METRICS_SYSTEM`

**Description:**
How often to collect and report system metrics (in seconds).

**Effect:**
- Lower values (10-30s): More frequent updates, higher network/CPU overhead
- Higher values (300-600s): Less frequent updates, lower overhead
- Default (60s): Good balance for most applications

**Network Bandwidth Estimation:**

| Interval | Messages/Day | Bandwidth/Month |
|----------|--------------|------------------|
| 10s | 8,640 | ~60 MB |
| 30s | 2,880 | ~20 MB |
| 60s | 1,440 | ~10 MB |
| 300s | 288 | ~2 MB |

(Assumes all metrics enabled, ~235 bytes per collection)

**Usage:**
```kconfig
# Collect every 5 minutes
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=300

# Collect every 10 minutes (low overhead)
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=600
```

---

## 3. Metric Registry

### 3.1 Complete Metric List

| Metric Name | Type | Unit | Range | Description | Kconfig |
|-------------|------|------|-------|-------------|---------|
| `system_memory_free_bytes` | int64 | bytes | 0 - UINT64_MAX | Free system memory | `SYSTEM_MEMORY` |
| `system_memory_total_bytes` | int64 | bytes | 0 - UINT64_MAX | Total system memory | `SYSTEM_MEMORY` |
| `system_heap_free_bytes` | int64 | bytes | 0 - UINT64_MAX | Free heap memory | `SYSTEM_HEAP` |
| `system_heap_allocated_bytes` | int64 | bytes | 0 - UINT64_MAX | Allocated heap memory | `SYSTEM_HEAP` |
| `system_network_sent_bytes` | int64 | bytes | 0 - UINT64_MAX | Cumulative bytes sent | `SYSTEM_NETWORK` |
| `system_network_received_bytes` | int64 | bytes | 0 - UINT64_MAX | Cumulative bytes received | `SYSTEM_NETWORK` |
| `system_cpu_utilization_percent` | float | percentage | 0.0 - 100.0 | CPU utilization | `SYSTEM_CPU` |
| `system_connection_state` | int64 | enum | 0 or 1 | MQTT connection state | `SYSTEM_CONNECTION` |

### 3.2 Metric Properties

#### Memory Metrics

**Category:** Resource Usage
**Update Frequency:** Periodic (`CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL`)
**Aggregation:** Default (60 second windows, min/max/sum/count)

**Interpretation:**
- `system_memory_free_bytes` - Instantaneous free RAM
- `system_memory_total_bytes` - Total RAM (constant on most platforms)
- Dashboard shows min/max/average over aggregation period

**Example Values:**
```
system_memory_free_bytes: 32768 (32 KB free)
system_memory_total_bytes: 131072 (128 KB total)
```

---

#### Heap Metrics

**Category:** Resource Usage
**Update Frequency:** Periodic
**Aggregation:** Default

**Interpretation:**
- `system_heap_free_bytes` - Bytes available for `k_malloc()`
- `system_heap_allocated_bytes` - Bytes currently allocated
- Total heap = free + allocated

**Example Values:**
```
system_heap_free_bytes: 8192 (8 KB free)
system_heap_allocated_bytes: 8192 (8 KB allocated)
Total heap configured: 16384 (16 KB)
```

---

#### Network Metrics

**Category:** Traffic Statistics
**Update Frequency:** Periodic
**Aggregation:** Default

**Interpretation:**
- Cumulative counters (monotonically increasing)
- Dashboard calculates rate (bytes/sec) from deltas
- Resets on device reboot

**Example Values:**
```
system_network_sent_bytes: 1048576 (1 MB sent since boot)
system_network_received_bytes: 524288 (512 KB received since boot)
```

**Dashboard Calculations:**
```
TX Rate = (sent[t] - sent[t-1]) / interval
RX Rate = (received[t] - received[t-1]) / interval
```

---

#### CPU Metrics

**Category:** Performance
**Update Frequency:** Periodic
**Aggregation:** Default

**Interpretation:**
- Percentage of time CPU is busy (not idle)
- Calculated from thread runtime statistics
- Includes all threads except idle thread

**Formula:**
```
CPU% = 100 * (1 - idle_cycles / total_cycles)
```

**Example Values:**
```
system_cpu_utilization_percent: 23.5 (23.5% busy, 76.5% idle)
```

**Notes:**
- Accuracy depends on `CONFIG_SYS_CLOCK_TICKS_PER_SEC`
- May show 0% on very idle systems
- May show 100% under heavy load

---

#### Connection Metrics

**Category:** Connectivity
**Update Frequency:** Event-driven (PT0S period)
**Aggregation:** None (immediate reporting)

**Interpretation:**
- 1 = MQTT connection established
- 0 = MQTT connection lost
- Each state change generates an immediate message

**Example Values:**
```
system_connection_state: 1 (connected)
system_connection_state: 0 (disconnected)
```

**Use Cases:**
- Track connection uptime
- Alert on disconnections
- Correlate with network issues

---

## 4. Configuration Examples

### 4.1 Minimal Configuration

**Goal:** Enable only memory and connection metrics, low overhead

**prj.conf:**
```kconfig
# Core metrics subsystem
CONFIG_SPOTFLOW_METRICS=y

# System metrics - minimal
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=n
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=n
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=n
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60
```

**Result:**
- Metrics: memory (2), connection (1)
- RAM overhead: ~300 bytes
- CPU overhead: <1 ms per minute
- Network: ~60 bytes every 60 seconds

---

### 4.2 Standard Configuration

**Goal:** Enable all metrics except CPU (recommended for most applications)

**prj.conf:**
```kconfig
# Core metrics subsystem
CONFIG_SPOTFLOW_METRICS=y

# System metrics - standard
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=n
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60

# Required for network metrics
CONFIG_NET_STATISTICS=y
```

**Result:**
- Metrics: memory (2), heap (2), network (2), connection (1)
- RAM overhead: ~800 bytes
- CPU overhead: <2 ms per minute
- Network: ~235 bytes every 60 seconds

---

### 4.3 Full Configuration with CPU Monitoring

**Goal:** Enable all metrics including CPU (for debugging)

**prj.conf:**
```kconfig
# Core metrics subsystem
CONFIG_SPOTFLOW_METRICS=y

# System metrics - full
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60

# Required for network metrics
CONFIG_NET_STATISTICS=y

# CPU metrics automatically enables these:
# CONFIG_THREAD_RUNTIME_STATS=y
# CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS=y
```

**Result:**
- Metrics: all 8 system metrics
- RAM overhead: ~900 bytes
- CPU overhead: <3 ms per minute + thread switch overhead
- Network: ~260 bytes every 60 seconds

---

### 4.4 Low-Frequency Configuration

**Goal:** Minimize network usage, collect every 10 minutes

**prj.conf:**
```kconfig
CONFIG_SPOTFLOW_METRICS=y
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=n

# Collect every 10 minutes
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=600
```

**Result:**
- Network: ~235 bytes every 10 minutes (~2 MB/month)
- Still get event-driven connection state changes

---

### 4.5 High-Frequency Configuration

**Goal:** Frequent updates for debugging, collect every 30 seconds

**prj.conf:**
```kconfig
CONFIG_SPOTFLOW_METRICS=y
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y

# Collect every 30 seconds
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=30
```

**Result:**
- Network: ~260 bytes every 30 seconds (~20 MB/month)
- More responsive to changes
- Higher overhead

---

## 5. Troubleshooting Guide

### 5.1 Common Issues

#### Issue: System metrics not appearing in dashboard

**Symptoms:**
- Dashboard shows no system metrics data
- Logs show metrics subsystem initialized

**Possible Causes:**

1. **Metrics not enabled in Kconfig**
   - Check: `CONFIG_SPOTFLOW_METRICS_SYSTEM=y`
   - Check: Individual metric flags enabled

2. **Interval too long**
   - Data may not appear for up to `CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL` seconds
   - Reduce interval temporarily for testing: `CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=10`

3. **MQTT connection issues**
   - System metrics depend on MQTT connectivity
   - Check connection state metric (should show 1 when connected)

**Solution:**
```bash
# Enable verbose logging
CONFIG_SPOTFLOW_METRICS_LOG_LEVEL_DBG=y
CONFIG_SPOTFLOW_SYSTEM_METRICS_LOG_LEVEL_DBG=y

# Check logs for:
# - "System metrics initialized"
# - "Collected metric: system_memory_free_bytes = ..."
```

---

#### Issue: Network metrics showing zero

**Symptoms:**
- `system_network_sent_bytes` and `system_network_received_bytes` always 0

**Possible Causes:**

1. **Network statistics not enabled**
   - Missing: `CONFIG_NET_STATISTICS=y`

2. **No default network interface**
   - Device has no network interface configured
   - Check: Network configuration in `prj.conf`

**Solution:**
```kconfig
CONFIG_NETWORKING=y
CONFIG_NET_STATISTICS=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=y  # if using IPv6
```

---

#### Issue: CPU metric showing incorrect values

**Symptoms:**
- `system_cpu_utilization_percent` always 0% or 100%
- CPU metric seems inaccurate

**Possible Causes:**

1. **Runtime stats not properly configured**
   - Missing dependencies (should be auto-selected by Kconfig)

2. **Clock frequency too low**
   - `CONFIG_SYS_CLOCK_TICKS_PER_SEC` affects accuracy
   - Recommendation: >= 100 Hz

**Solution:**
```kconfig
CONFIG_SYS_CLOCK_TICKS_PER_SEC=100
CONFIG_THREAD_RUNTIME_STATS=y
CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS=y
```

---

#### Issue: High memory usage

**Symptoms:**
- System metrics using more RAM than expected

**Diagnostics:**
```c
// Add to your code temporarily:
#include <zephyr/sys/mem_stats.h>
struct sys_memory_stats stats;
sys_mem_stats_get(&stats);
printk("Free: %zu, Allocated: %zu\n", stats.free_bytes, stats.allocated_bytes);
```

**Possible Causes:**

1. **All metrics enabled**
   - Expected: ~900 bytes for all metrics
   - Solution: Disable unused metrics

2. **Core metrics aggregation**
   - Each aggregated metric uses ~100-150 bytes
   - This is normal and expected

---

#### Issue: Compilation errors

**Error:** `undefined reference to sys_mem_stats_get`

**Cause:** Missing Zephyr configuration

**Solution:**
```kconfig
# Ensure Zephyr memory management is enabled
CONFIG_SYS_MEM_BLOCKS=y
```

**Error:** `undefined reference to net_if_get_default`

**Cause:** Networking not enabled

**Solution:**
```kconfig
CONFIG_NETWORKING=y
```

---

### 5.2 Debugging Checklist

When system metrics are not working as expected:

- [ ] Verify `CONFIG_SPOTFLOW_METRICS=y`
- [ ] Verify `CONFIG_SPOTFLOW_METRICS_SYSTEM=y`
- [ ] Verify individual metric flags are enabled
- [ ] Check logs for initialization messages
- [ ] Verify MQTT connection is established
- [ ] Check dashboard for data (wait at least one interval)
- [ ] Enable debug logging if issues persist
- [ ] Verify Zephyr dependencies (networking, stats, etc.)

---

### 5.3 Performance Debugging

If system metrics are causing performance issues:

1. **Measure overhead:**
   ```c
   uint64_t start = k_uptime_get();
   // Let one collection cycle run
   k_sleep(K_SECONDS(CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL + 1));
   uint64_t end = k_uptime_get();
   printk("Time for one cycle: %llu ms\n", end - start);
   ```

2. **Disable CPU metrics first:**
   - Highest overhead component
   - `CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=n`

3. **Increase interval:**
   - `CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=300` (5 minutes)

4. **Disable unused metrics:**
   - Only enable what dashboard actually displays

---

## 6. Migration Guide

### 6.1 Migrating from Custom Metrics

If you previously implemented custom metrics for system telemetry, you can now replace them with system metrics.

**Before (custom code):**
```c
// Application code
void monitor_system(void) {
    struct sys_memory_stats stats;
    sys_mem_stats_get(&stats);

    spotflow_report_metric_int("my_memory_free", stats.free_bytes, NULL);
    spotflow_report_metric_int("my_memory_total",
                              stats.free_bytes + stats.allocated_bytes, NULL);
}

// Called from timer or thread
K_TIMER_DEFINE(monitor_timer, monitor_system, NULL);
```

**After (Kconfig only):**
```kconfig
# prj.conf
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y
```

**Result:**
- Remove ~50 lines of application code
- Automatic registration and collection
- Consistent naming with other devices

---

### 6.2 Metric Name Mapping

If you used custom metric names, update dashboard queries:

| Old Custom Name | New System Metric | Notes |
|-----------------|-------------------|-------|
| `memory_free` | `system_memory_free_bytes` | Standard naming |
| `heap_usage` | `system_heap_allocated_bytes` | More precise |
| `network_tx` | `system_network_sent_bytes` | Cumulative |
| `network_rx` | `system_network_received_bytes` | Cumulative |
| `cpu_load` | `system_cpu_utilization_percent` | Standardized |
| `mqtt_connected` | `system_connection_state` | Event-driven |

---

### 6.3 Dashboard Updates

No dashboard changes required - system metrics use the same MQTT protocol as custom metrics.

**Metric names will appear in dashboard metric selector:**
- `system_memory_free_bytes`
- `system_memory_total_bytes`
- `system_heap_free_bytes`
- ... etc.

---

## Appendix A: Quick Reference Card

```
┌─────────────────────────────────────────────────────────────┐
│  System Metrics Quick Reference                             │
├─────────────────────────────────────────────────────────────┤
│  Enable system metrics:                                     │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM=y                         │
│                                                              │
│  Memory metrics (RAM):                                      │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y                  │
│                                                              │
│  Heap metrics:                                              │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y                    │
│                                                              │
│  Network metrics (requires networking):                     │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y                 │
│    CONFIG_NET_STATISTICS=y                                  │
│                                                              │
│  Connection state:                                          │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y              │
│                                                              │
│  CPU utilization (⚠️ adds overhead):                        │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y                     │
│                                                              │
│  Collection interval:                                       │
│    CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60               │
│                                                              │
│  RAM overhead: ~900 bytes (all enabled)                     │
│  CPU overhead: <3 ms per cycle                              │
│  Network: ~235 bytes per cycle                              │
└─────────────────────────────────────────────────────────────┘
```

---

## Appendix B: Kconfig Template

Copy this template to your `prj.conf`:

```kconfig
###############################################################################
# Spotflow System Metrics Configuration
###############################################################################

# Enable core metrics subsystem
CONFIG_SPOTFLOW_METRICS=y

# Enable system metrics (auto-collection, zero code)
CONFIG_SPOTFLOW_METRICS_SYSTEM=y

# --- Individual Metric Categories ---

# Memory metrics (free/total RAM)
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y

# Heap metrics (free/allocated heap)
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y

# Network metrics (TX/RX bytes) - requires networking
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y
CONFIG_NET_STATISTICS=y

# Connection state (MQTT connected/disconnected)
CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y

# CPU utilization (WARNING: adds thread switch overhead)
# CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=n

# --- Collection Interval ---

# How often to collect metrics (10-3600 seconds, default 60)
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60

###############################################################################
```

---

**End of System Metrics API Specification**
