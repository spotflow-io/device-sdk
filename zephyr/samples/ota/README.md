# Spotflow OTA Sample

This sample demonstrates the Spotflow OTA Device SDK on Zephyr. It connects to the
Spotflow cloud over MQTT, receives full-manifest OTA updates, automatically handles main
firmware updates through MCUboot, and shows how application code integrates the public
OTA callbacks.

The sample uses the v1 OTA MQTT protocol on `ota-cbor-c2d` (cloud to device) and
`ota-cbor-d2c` (device to cloud). The legacy single-URL `ota-cbor` PoC topic is no
longer used.

## Features

- **Automatic main firmware updates** when
  `CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE=y`:
  download, flash to the MCUboot update slot, request a test upgrade, reboot, and
  reconcile probation state after boot.
- **Startup confirmation** via `spotflow_confirm_main_firmware_image()` when the device
  boots into an unconfirmed image. Production firmware should confirm only after your
  own validation checks pass; this sample confirms immediately for demonstration.
- **Non-main firmware delegation** through `spotflow_on_handle_firmware_update()`. The
  sample logs the request and returns `FAILED`; implement your own handler for secondary
  firmware.
- **Progress observation** through `spotflow_on_main_firmware_update_progressed()`.
- **Actionable cancellation** through `spotflow_on_update_canceled()` and
  `spotflow_is_update_canceled()`.

## Requirements

### Hardware

- A Zephyr board with network connectivity (Wi-Fi or Ethernet).
- MCUboot support through sysbuild.
- Flash layout that supports image management and a secondary update slot.
- Tested on NXP FRDM-RW612 (Wi-Fi).

### Software

- Zephyr SDK and workspace with Spotflow Zephyr module enabled.
- Python environment with `west` (activate `.venv` before building).
- Spotflow account credentials for MQTT ingest.
- For main firmware auto-handling: binary descriptor build ID support
  (`CONFIG_SPOTFLOW_GENERATE_BUILD_ID`).

## Building and Running

This sample must be built with sysbuild so MCUboot is included:

```bash
source .venv/bin/activate
west build --sysbuild -b frdm_rw612 spotflow/zephyr/samples/ota --pristine
west flash
```

Replace `frdm_rw612` with your board if needed. Use a board-specific overlay under
`boards/` when one exists for your target.

### Configure Credentials

Copy the sample credentials file and fill in your network and Spotflow credentials:

```bash
cp credentials-sample.conf credentials.conf
```

Edit `credentials.conf`:

```kconfig
CONFIG_SPOTFLOW_DEVICE_ID="your-device-id"
CONFIG_SPOTFLOW_INGEST_KEY="your-ingest-key"
CONFIG_NET_WIFI_SSID="your-ssid"
CONFIG_NET_WIFI_PASSWORD="your-password"
```

The build automatically merges `credentials.conf` when present. Do not commit this file.

### Sysbuild and MCUboot

`sysbuild.conf` enables the MCUboot bootloader:

```kconfig
SB_CONFIG_BOOTLOADER_MCUBOOT=y
```

Optional MCUboot tuning lives in `sysbuild/mcuboot.conf`. The application image is built
as the MCUboot secondary slot consumer; main firmware updates stream into the update
slot and request `BOOT_UPGRADE_TEST` before reboot.

## Expected Cloud Interaction

1. The device connects to Spotflow MQTT and publishes session metadata including
   `lastUpdateAttemptId`.
2. The cloud sends `UPDATE_ARTIFACTS` on `ota-cbor-c2d` (MQTT QoS 1) with attempt ID,
   manifest, artifact URLs, OTA secrets, slugs, and versions.
3. The SDK processes artifacts in order on the OTA worker thread.
4. Main firmware artifacts are handled automatically when auto-handling is enabled.
5. The device publishes merged `UPDATE_RESULTS` on `ota-cbor-d2c` (MQTT QoS 0).
6. The cloud may send `CANCEL_UPDATE` or `REPORT_UPDATE_RESULTS` to control or re-fetch
   results.

Publish a deployment from the Spotflow portal targeting this device to exercise the full
flow.

## Application Integration

### Confirming main firmware after reboot

After a successful main firmware download and reboot, MCUboot runs the new image in test
mode. Your application must call `spotflow_confirm_main_firmware_image()` once the new
image passes your checks:

```c
struct spotflow_ota_main_firmware_state state;
if (spotflow_get_main_firmware_update_state(&state) == 0 &&
    state.phase == SPOTFLOW_OTA_PHASE_UNCONFIRMED) {
    spotflow_confirm_main_firmware_image(&state);
}
```

If you do not confirm, MCUboot may roll back to the previous image on the next reboot.
The SDK reports rollback as a failed main firmware artifact.

This sample calls confirm automatically on boot for demonstration. Replace that with
your own validation logic in production firmware.

### Pausing, resuming, or failing a main firmware update

Before reboot, you can control an in-progress main firmware update from any thread:

```c
struct spotflow_ota_main_firmware_state state;
spotflow_get_main_firmware_update_state(&state);
spotflow_pause_main_firmware_update(&state);
spotflow_resume_main_firmware_update(&state);
spotflow_fail_main_firmware_update(&state);
```

These calls update shared state and wake the OTA worker; they do not publish MQTT
directly.

### Non-main firmware

Implement `spotflow_on_handle_firmware_update()` to handle delegated artifacts. Use
`info->download_request` with `spotflow_download_artifact()` to stream data. Return a
terminal `spotflow_ota_result` when finished.

## Callback Threading

Per the Spotflow OTA public contract:

- `spotflow_on_handle_firmware_update()` runs on the **OTA worker thread**.
- `spotflow_on_main_firmware_update_progressed()` runs on the **OTA worker thread**.
- `spotflow_on_update_canceled()` runs on **Zephyr sysworkq**.

Do not perform blocking work on sysworkq. Public API calls from application threads are
safe and may wake the OTA worker.

## Actionable Cancellation

`spotflow_is_update_canceled()` returns true only when the SDK has **accepted**
cancellation and your delegated handler should stop work. If the cloud sends
`CANCEL_UPDATE` after an artifact has already succeeded, the SDK ignores it for control
purposes: the cancellation callback is not invoked and `spotflow_is_update_canceled()`
stays false.

## Logging and Sensitive Data

Do not log full artifact URLs or OTA secrets. The SDK avoids logging them in normal
operation; application code should follow the same rule. Log attempt IDs, slugs,
versions, phases, and error codes instead.

## Key Kconfig Options

```kconfig
CONFIG_SPOTFLOW_OTA=y
CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE=y
CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS=4
CONFIG_SPOTFLOW_OTA_THREAD_STACK_SIZE=4096
CONFIG_SPOTFLOW_OTA_DOWNLOAD_THREAD_STACK_SIZE=4096
CONFIG_SPOTFLOW_GENERATE_BUILD_ID=y
CONFIG_NVS=y
CONFIG_SETTINGS_NVS=y
```

See `spotflow/zephyr/src/ota/Kconfig` for the full OTA option set.

## Further Reading

- [OTA Device SDK Requirements](../../../../fota/design/ota_device_sdk_requirements.md)
- [OTA Device SDK Implementation Design](../../../../fota/design/ota_device_sdk_implementation_design.md)
- [OTA Device SDK Implementation Plan](../../../../fota/design/ota_device_sdk_implementation_plan.md)
- [OTA MQTT Protocol](../../../../fota/design/notion/OTA%20MQTT%20Protocol%20359400d453c58041a190f584dd3cc5d2.md)
