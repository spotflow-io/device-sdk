.. zephyr:code-sample:: spotflow_metrics
   :name: Spotflow Metrics
   :relevant-api: spotflow_metrics

   Demonstrate Spotflow metrics collection and reporting for Zephyr-based IoT devices.

Overview
********

This sample demonstrates the Spotflow metrics collection and reporting feature for Zephyr-based IoT devices.

Features
========

This sample showcases:

- **System Metrics Auto-Collection**

  - Heap usage (free/allocated bytes)
  - Thread stack usage (free bytes, usage percentage per thread)
  - Network traffic statistics (TX/RX bytes per interface)
  - CPU utilization percentage
  - MQTT connection state changes
  - Device reset cause (reported on boot)
  - Device uptime heartbeat

- **Custom Application Metrics**

  - Label-less metrics: Simple counter and temperature sensor
  - Labeled metrics: HTTP request duration with endpoint, method, and status labels

- **Integration with Logs**

  - Demonstrates metrics and logs working together
  - All telemetry transmitted to Spotflow cloud platform

Requirements
************

Hardware
========

- Zephyr-supported board with network connectivity (Wi-Fi or Ethernet)
- Tested on: NXP FRDM-RW612 (Wi-Fi)

Software
========

- Zephyr SDK version 4.3.0 or later
- Spotflow account: Sign up at `Spotflow <https://spotflow.io>`_ to get your credentials

Building and Running
********************

Configure Credentials
=====================

Copy the sample credentials file and add your Spotflow credentials:

.. code-block:: bash

   cp credentials-sample.conf credentials.conf

Edit ``credentials.conf``:

.. code-block:: kconfig

   CONFIG_SPOTFLOW_DEVICE_ID="your-device-id"
   CONFIG_SPOTFLOW_INGEST_KEY="your-ingest-key"

Alternatively add your credentials directly in ``prj.conf``.

Build the Sample
================

For Wi-Fi boards (e.g., NXP FRDM-RW612):

.. zephyr-app-commands::
   :zephyr-app: samples/metrics
   :board: frdm_rw612
   :goals: build flash
   :compact:


Monitor Output
==============

.. code-block:: bash

   west espressif monitor  # For ESP32 boards
   # OR
   screen /dev/ttyUSB0 115200  # For other boards

Expected Behavior
*****************

The application will:

1. Initialize Wi-Fi/Ethernet and connect to network
2. Register 3 custom metrics (metrics subsystem auto-initializes on first registration):

   - ``app_counter`` (int, aggregated over 1 minute)
   - ``temperature_celsius`` (float, immediate/no aggregation)
   - ``http_request_duration_ms`` (float, labeled, aggregated over 1 minute)

3. Start reporting metrics every 2 seconds
4. System metrics automatically collected every 60 seconds (configurable)

Sample Output
=============

.. code-block:: console

   [00:00:01.234] <inf> metrics_sample: ========================================
   [00:00:01.235] <inf> metrics_sample: Spotflow Metrics Sample Application
   [00:00:01.236] <inf> metrics_sample: ========================================
   [00:00:01.237] <inf> metrics_sample:
   [00:00:01.238] <inf> metrics_sample: This sample demonstrates:
   [00:00:01.239] <inf> metrics_sample:   - System metrics auto-collection
   [00:00:01.240] <inf> metrics_sample:   - Custom dimensionless metrics
   [00:00:01.241] <inf> metrics_sample:   - Custom dimensional metrics
   [00:00:01.242] <inf> metrics_sample:   - Integration with logs
   [00:00:01.244] <inf> spotflow_metrics: Registered metric 'network_tx_bytes' (type=int, agg=1, max_ts=4, max_labels=1)
   [00:00:01.250] <inf> spotflow_metrics: Registered metric 'network_rx_bytes' (type=int, agg=1, max_ts=4, max_labels=1)
   ....
   [00:00:03.456] <inf> metrics_sample: === Iteration 0 ===
   [00:00:03.457] <inf> metrics_sample: Reported temperature: 22.34 °C

Understanding the Metrics
*************************

Label-less Metrics
==================

**app_counter** (integer, aggregated):

- Increments by 10 every iteration
- Aggregated over 1 minute (PT1M)
- Cloud receives: sum, count, min, max

**temperature_celsius** (float, immediate):

- Simulates temperature sensor reading
- No aggregation (PT0S) - each value sent immediately
- Cloud receives: individual readings as they occur

Labeled Metrics
===============

**http_request_duration_ms** (float, labeled):

- Simulates HTTP request latency
- Labels: ``endpoint``, ``method``, ``status``
- Up to 18 unique label combinations tracked (3 endpoints × 2 methods × 3 statuses)
- Aggregated over 1 minute per label combination

Example label combinations:

.. code-block:: none

   {endpoint="/api/users", method="GET", status="200"}
   {endpoint="/api/products", method="POST", status="201"}
   {endpoint="/health", method="GET", status="200"}

Each unique combination maintains separate aggregation state.

System Metrics
**************

The following system metrics are automatically collected every 60 seconds (configurable):

.. list-table::
   :header-rows: 1
   :widths: 30 10 15 45

   * - Metric Name
     - Type
     - Labels
     - Description
   * - ``heap_free_bytes``
     - int
     - \-
     - Available heap memory
   * - ``heap_allocated_bytes``
     - int
     - \-
     - Allocated heap memory
   * - ``cpu_utilization_percent``
     - float
     - \-
     - CPU usage percentage
   * - ``thread_stack_free_bytes``
     - int
     - ``thread``
     - Free stack per thread
   * - ``thread_stack_used_percent``
     - float
     - ``thread``
     - Stack usage percentage per thread
   * - ``network_tx_bytes``
     - int
     - ``interface``
     - Transmitted bytes per interface
   * - ``network_rx_bytes``
     - int
     - ``interface``
     - Received bytes per interface
   * - ``connection_mqtt_connected``
     - int
     - \-
     - Connection state (0/1), event-driven
   * - ``boot_reset``
     - int
     - ``reason``
     - Reset cause, reported once on boot
   * - ``uptime_ms``
     - int
     - \-
     - Device uptime (heartbeat, configurable interval)

Configuration Options
*********************

Key configuration options in ``prj.conf``:

.. code-block:: kconfig

   # Metrics subsystem
   CONFIG_SPOTFLOW_METRICS=y
   CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE=64              # Message queue size (default with system metrics)
   CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED=32          # Max metrics
   CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC=4    # Max labels per metric

   # System metrics
   CONFIG_SPOTFLOW_METRICS_SYSTEM=y
   CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL=60         # Collection interval (seconds)
   CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP=y              # Enable heap metrics
   CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK=y           # Enable network metrics
   CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU=y               # Enable CPU metrics
   CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK=y             # Enable thread stack metrics
   CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION=y        # Enable connection state metrics
   CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE=y       # Enable reset cause reporting

   # Heartbeat
   CONFIG_SPOTFLOW_METRICS_HEARTBEAT=y
   CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL=60      # Heartbeat interval (seconds)

   # Heap sizing (auto-calculated, adjust if needed)
   CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS=8192       # 8KB for app metrics
   CONFIG_HEAP_MEM_POOL_ADD_SIZE_SPOTFLOW_METRICS_SYSTEM=20480  # 20KB for system metrics

Viewing Metrics in Spotflow Cloud
*********************************

1. Log in to your Spotflow account at `Spotflow <https://spotflow.io>`_
2. Navigate to your device dashboard
3. View real-time and historical metrics
4. Create custom dashboards and alerts

Troubleshooting
***************

Metrics Not Appearing in Cloud
==============================

- Check network connectivity
- Verify credentials in ``credentials.conf``
- Check log output for MQTT connection status
- Increase log level: ``CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL=4``

Out of Memory Errors
====================

- Increase heap size: ``CONFIG_HEAP_MEM_POOL_SIZE``
- Reduce max registered metrics: ``CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED``
- Reduce max timeseries per dimensional metric in code

Queue Full Warnings
===================

- Increase queue size: ``CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE``
- Increase aggregation intervals (less frequent transmission)
- Check MQTT connection stability

Further Reading
***************

- `Spotflow Documentation <https://docs.spotflow.io>`_
