# Spotflow Metrics Sample Application

This sample demonstrates the Spotflow metrics collection and reporting feature for Zephyr-based IoT devices.

## Features

This sample showcases:

1. **System Metrics Auto-Collection**
   - Memory usage (free/total bytes)
   - Heap usage (free/allocated bytes)
   - Network traffic statistics (TX/RX bytes)
   - CPU utilization percentage
   - MQTT connection state changes
   - Device reset cause (reported on boot)

2. **Custom Application Metrics**
   - **Dimensionless metrics**: Simple counter and temperature sensor
   - **Dimensional metrics**: HTTP request duration with endpoint, method, and status dimensions

3. **Integration with Logs**
   - Demonstrates metrics and logs working together
   - All telemetry transmitted to Spotflow cloud platform

## Prerequisites

1. **Hardware**: Zephyr-supported board with network connectivity (Wi-Fi or Ethernet)
   - Tested on: NXP FRDM-RW612 (Wi-Fi)

2. **Spotflow Account**: Sign up at https://spotflow.io to get your credentials

3. **Zephyr SDK**: Version 3.5.0 or later

## Building and Running

### 1. Configure Credentials

Copy the sample credentials file and add your Spotflow credentials:

```bash
cp credentials-sample.conf credentials.conf
```

Edit `credentials.conf`:
```conf
CONFIG_SPOTFLOW_DEVICE_ID="your-device-id"
CONFIG_SPOTFLOW_INGEST_KEY="your-ingest-key"
```

### 2. Build the Sample

For Wi-Fi boards (e.g., NXP FRDM-RW612):
```bash
west build -b frdm_rw612 samples/metrics
```

For Ethernet boards:
```bash
west build -b your_board samples/metrics -- -DCONFIG_SPOTFLOW_USE_ETH=y -DCONFIG_SPOTFLOW_USE_WIFI=n
```

### 3. Flash and Run

```bash
west flash
```

### 4. Monitor Output

```bash
west espressif monitor  # For ESP32 boards
# OR
screen /dev/ttyUSB0 115200  # For other boards
```

## Expected Behavior

The application will:

1. Initialize Wi-Fi/Ethernet and connect to network
2. Register 3 custom metrics (metrics subsystem auto-initializes on first registration):
   - `app_counter` (int, aggregated over 1 minute)
   - `temperature_celsius` (float, immediate/no aggregation)
   - `http_request_duration_ms` (float, dimensional, aggregated over 1 minute)
3. Start reporting metrics every 2 seconds
4. System metrics automatically collected every 60 seconds (configurable)

### Sample Console Output

```
[00:00:01.234] <inf> metrics_sample: ========================================
[00:00:01.235] <inf> metrics_sample: Spotflow Metrics Sample Application
[00:00:01.236] <inf> metrics_sample: ========================================
[00:00:01.237] <inf> metrics_sample:
[00:00:01.238] <inf> metrics_sample: This sample demonstrates:
[00:00:01.239] <inf> metrics_sample:   - System metrics auto-collection
[00:00:01.240] <inf> metrics_sample:   - Custom dimensionless metrics
[00:00:01.241] <inf> metrics_sample:   - Custom dimensional metrics
[00:00:01.242] <inf> metrics_sample:   - Integration with logs
[00:00:02.345] <inf> spotflow_metrics: Metrics subsystem initialized (auto-initialized on first use)
[00:00:02.346] <inf> metrics_sample: Registered metric: app_counter (int, PT1M)
[00:00:02.347] <inf> metrics_sample: Registered metric: temperature_celsius (float, PT0S)
[00:00:02.348] <inf> metrics_sample: Registered metric: http_request_duration_ms (float, dimensional, PT1M)
[00:00:03.456] <inf> metrics_sample: === Iteration 0 ===
[00:00:03.457] <inf> metrics_sample: Reported temperature: 22.34 °C
```

## Understanding the Metrics

### Dimensionless Metrics

**app_counter** (integer, aggregated):
- Increments by 10 every iteration
- Aggregated over 1 minute (PT1M)
- Cloud receives: sum, count, min, max

**temperature_celsius** (float, immediate):
- Simulates temperature sensor reading
- No aggregation (PT0S) - each value sent immediately
- Cloud receives: individual readings as they occur

### Dimensional Metrics

**http_request_duration_ms** (float, dimensional):
- Simulates HTTP request latency
- Dimensions: `endpoint`, `method`, `status`
- Up to 16 unique dimension combinations tracked
- Aggregated over 1 minute per dimension combination

Example dimension combinations:
```
{endpoint="/api/users", method="GET", status="200"}
{endpoint="/api/products", method="POST", status="201"}
{endpoint="/health", method="GET", status="200"}
```

Each unique combination maintains separate aggregation state.

### System Metrics (Auto-Collected)

The following system metrics are automatically collected every 60 seconds:

| Metric Name | Type | Dimensions | Description |
|-------------|------|------------|-------------|
| `system.memory.free_bytes` | int | - | Available RAM |
| `system.memory.total_bytes` | int | - | Total RAM |
| `system.heap.free_bytes` | int | - | Available heap |
| `system.heap.allocated_bytes` | int | - | Allocated heap |
| `system.network.tx_bytes` | int | `interface` | Transmitted bytes |
| `system.network.rx_bytes` | int | `interface` | Received bytes |
| `system.cpu.utilization_percent` | float | - | CPU usage |
| `system.connection.mqtt_connected` | int | - | Connection state (0/1) |
| `system.boot.reset_cause` | int | - | Reset cause code |

## Configuration Options

Key configuration options in `prj.conf`:

```conf
# Metrics subsystem
CONFIG_SPOTFLOW_METRICS=y
CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE=16              # Message queue size
CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED=32          # Max metrics
CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL=1  # PT1M default

# System metrics
CONFIG_SPOTFLOW_METRICS_SYSTEM=y
CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60         # Collection interval (seconds)
CONFIG_SPOTFLOW_METRICS_SYSTEM_MEMORY=y            # Enable memory metrics
CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y              # Enable heap metrics
CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y           # Enable network metrics
CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y               # Enable CPU metrics

# Heap sizing (adjust based on your needs)
CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS=16384
CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM=16384
```

## Viewing Metrics in Spotflow Cloud

1. Log in to your Spotflow account at https://spotflow.io
2. Navigate to your device dashboard
3. View real-time and historical metrics
4. Create custom dashboards and alerts

## Troubleshooting

### Metrics not appearing in cloud

- Check network connectivity
- Verify credentials in `credentials.conf`
- Check log output for MQTT connection status
- Increase log level: `CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL=4`

### Out of memory errors

- Increase heap size: `CONFIG_HEAP_MEM_POOL_SIZE`
- Reduce max registered metrics: `CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED`
- Reduce max timeseries per dimensional metric in code

### Queue full warnings

- Increase queue size: `CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE`
- Increase aggregation intervals (less frequent transmission)
- Check MQTT connection stability

## Further Reading

- [Metrics Architecture Documentation](../../design/metrics/architecture.md)
- [API Specification](../../design/metrics/api_specification.md)
- [System Metrics Documentation](../../design/metrics/system_metrics_architecture.md)
- [Spotflow Documentation](https://docs.spotflow.io)

## License

Copyright (c) 2024 Spotflow
SPDX-License-Identifier: Apache-2.0
