# Spotflow Observability Device SDK

Device SDK for Spotflow embedded observability platform.

This SDK provides a set of tools and libraries for [Zephyr RTOS](https://www.zephyrproject.org/) to
send your logs to the Spotflow observability platform.

Device SDK is integrated with Zephyr as a module that contains the Spotflow logging backend
that seamlessly integrates with the Zephyr logging subsystem.

Our solution was tested on the following Zephyr boards (more are coming soon):
* [NXP FRDM-RW612](https://www.nxp.com/design/microcontrollers/arm-cortex-m/rw6xx-rtos-ready-wireless-mcus:FRDM-RW612)
* [Nordic NRF7002DK](https://www.nordicsemi.com/Products/Development-hardware/nRF7002-DK)
* [ESP32-C3-DevKitC](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitc-02/index.html)

## Getting Started

Register and get your Ingest key at [Spotflow](https://spotflow.io/signup).

Follow the Quickstart guide that is available in our portal after registration.

Alternatively, you can check sample applications in the [samples](zephyr/samples).
The device SDK is meant to be used as
a [Zephyr module](https://docs.zephyrproject.org/latest/develop/modules.html).
You can add it to your Zephyr project by adding the following line to your `west.yml`:

```yaml
manifest:
    projects:
    - name: spotflow
      path: modules/lib/spotflow
      revision: main
      url: https://github.com/spotflow-io/device-sdk
```

## Documentation

### Architecture

```mermaid
---
title: Main log flow
---
flowchart LR
    A[User Code] --> B[Zephyr RTOS]
    B --> C[Spotflow Logging Backend]
    C --> D[Spotflow Mqtt Broker]
    D --> E[Spotflow Observability Platform]
```

```mermaid
---
title: Spotflow Logging Backend flow
---
flowchart LR
    A[Zephyr logging] --> B[Spotflow Logging Backend]
    B --> C[Spotflow Mqtt Broker]
    C --> D[Encode CBOR]
    D --> E[Log message Queue]
    E --> F[Processor]
    F --> G[Send to MQTT]
```

### Build ID

In order to match core dumps with symbol files, our Zephyr module provides a piece of information called build ID.
The build ID is computed as a hash of the bytes loaded to the device; therefore, it uniquely identifies the firmware image.
Our Zephyr module adds a build command that computes the build ID and patches it into the `.elf` file as the following [binary descriptor](https://docs.zephyrproject.org/latest/services/binary_descriptors/index.html):

- **ID**: `0x5f0` (`5f` resembles `sf` - **S**pot**f**low, `0` stands for our first binary descriptor ID)
- **Type**: `bytes`
- **Length**: `20`

You can retrieve the build ID from the `.elf` file using the following command:

```bash
west bindesc custom_search BYTES 0x5f0 zephyr.elf
```

Because Zephyr doesn't allow to insert a post-build command between the compilation of `zephyr.elf` and the generation of derived files such as `zephyr.hex` and `zephyr.bin`, our build command patches these files as well.
In particular, the files with the following extensions are patched:

- `.elf`
- `.hex`
- `.bin`
- `.strip`
- `.exe` when **not** targeting [native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html) (it's just a copy of the `.elf` file)

The files with the following extensions (and others that might be introduced in the future) are **not** patched, so the build IDs stored in them are filled with zeros:

- `.lst` (assembly listing)
- `.uf2`
- `.s19`
- `.exe` when targeting [native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)

## Feedback
Any comments, suggestions, or issues are welcome.
Create a Github issue or contact us at hello@spotflow.io,
[LinkedIn](https://www.linkedin.com/company/spotflow/) or [Discord](https://discord.gg/yw8rAvGZBx).
