# Spotflow Observability for IoT Devices

Device Component for Spotflow embedded observability platform.

This Component provides a set of tools and libraries for [ESP IDF]() to
send your logs to the Spotflow observability platform.

It is integrated with ESP IDF as a component which utilizes the ESP IDF logging component to seamlessly integrate with current firmwares.

Our solution was tested on the following Espressif boards (more are coming soon):
* [ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html)
* [ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/index.html)
* [ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/index.html)

We currently support:
* ESP IDF v5.x.x
* Zephyr 4.1.x, 4.2.x, and 4.3.x
* nRF Connect SDK 3.0.x and 3.1.x

For more information, check [Spotflow](https://app.spotflow.io/) or [Spotflow documentation](https://docs.spotflow.io/).

## Getting Started

Register and get your Ingest key at [Spotflow](https://spotflow.io/signup).

Follow the Quickstart guide that is available in our portal after registration.

Alternatively, you can check example applications in the [examples]().
The Component is meant to be used as
a [ESP IDF component](https://docs.espressif.com/projects/idf-component-manager/en/latest/).
You can add it to your ESP IDF project by running the following command:

```code
idf.py add-dependency "spotflow/device_sdk"
```

Include the following header file from your main file:

```C
#include "spotflow.h"
```

Call the following function during startup:

```C
spotflow_init();
```


## Component Size Default
### Without Coredump

| Archive File  | Total Size | DRAM | .bss | .data | IRAM | .text | .vectors | Flash Code | .text | Flash Data | .rodata |
|---------------|------------|------|------|-------|------|-------|----------|------------|-------|------------|---------|
| libspotflow.a | 7009       | 373  | 372  | 1     | 0    | 0     | 0        | 4633       | 4633  | 2003       | 2003    |

### With Coredump

| Archive File  | Total Size | DRAM | .bss | .data | IRAM | .text | .vectors | Flash Code | .text | Flash Data | .rodata |
|---------------|------------|------|------|-------|------|-------|----------|------------|-------|------------|---------|
| libspotflow.a | 8562       | 406  | 405  | 1     | 0    | 0     | 0        | 6153       | 6153  | 2003       | 2003    |

### Spotflow Main Log Flow

<!--
```mermaid
---
title: Main log flow
---
flowchart LR
    A[User Code] --> B[ESP IDF logging backend]
    B --> C[Spotflow Logging Backend]
    C -- QoS [Set in kconfig] --> D[Spotflow Mqtt Broker]
    D --> E[Spotflow Observability Platform]
```
-->
![Spotflow Main Log Flow](docs/Spotflow_main_log_flow.svg)

### Spotflow Data Flow
![Spotflow Data Flow](docs/spotflow_data_flow.svg)

#### Configuration

Most properties of the Component are currently configured in build time using Kconfig - see the help text of each option for more information.

The Component also provides a way to dynamically configure certain properties from the cloud using the Spotflow portal.
Currently, there is only one such property:

- **Minimal severity of sent log messages** (the *level* of sent log messages)

The configuration process uses the mechanism of *desired* and *reported* values that are stored for each device in the Spotflow platform and are synchronized using the MQTT protocol.
If ESP IDF NVS storage is enabled, the Component uses it to persist the last active configuration.

### Spotflow Backend Flow
![Spotflow Backend Flow](docs/Spotflow_backend.svg)

### Coredump
It uses the default coredump component from ESP-IDF, [espcoredump](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html). If you get any not enough memory errors when saving coredumps, increase the coredump partition size in the `partition.csv` file.
If **Flash Encryption** is enabled, add the encrypted flag to the coredump partition in partitions.csv. Nothing else is required.

## Feedback
Any comments, suggestions, or issues are welcome.
Create a Github issue or contact us at hello@spotflow.io,
[LinkedIn](https://www.linkedin.com/company/spotflow/) or [Discord](https://discord.gg/yw8rAvGZBx).
