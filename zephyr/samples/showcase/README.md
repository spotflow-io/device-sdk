# Spotflow API showcase

This MQTT-only sample combines the public Spotflow Zephyr features in one application:

- Zephyr logs at every severity and cloud-controlled minimum severity
- integer, float, labeled, label-less, event, heartbeat, and system metrics
- runtime device identity and all supported session-label value types
- coredump capture and upload after reboot
- automatic main-firmware OTA with observation, pause/resume, abort, rollback, and confirmation
- delegated firmware download with pause/resume and cancellation
- persistent Spotflow configuration and OTA state

The sample deliberately keeps destructive and mutually exclusive actions behind one hardware button.
It does not provide a shell.

## Button controls

| Input | Behavior |
|---|---|
| Short press | Confirm the running main image when it is in the `UNCONFIRMED` OTA phase. Otherwise, do nothing. |
| Long press (three seconds) | Cancel a delegated artifact download or abort an automatic main update. If an unconfirmed image is running, reboot without confirming it so MCUboot rolls it back. If no OTA is active, trigger a coredump. |

An automatic main update cannot be aborted after MCUboot's test upgrade has been committed in
`PENDING_REBOOT`. A long press in that short phase logs a warning and does not crash or reboot the
device.

## Supported boards

The sample is configured for:

- NXP FRDM-RW612, using button SW2
- NXP FRDM-MCXN947 (`frdm_mcxn947/mcxn947/cpu0`)

Both configurations provide an MCUboot secondary image slot, persistent settings storage, a
coredump partition, networking, and a `sw0` button alias. Supporting another board normally
requires equivalent flash partitions and a board devicetree overlay.

## Configure credentials

Copy the credentials template without committing the resulting file:

```bash
cp credentials-sample.conf credentials.conf
```

Set the Spotflow ingest key and any credentials required by the selected network interface in
`credentials.conf`. FRDM-RW612 uses the Wi-Fi SSID and password from the template; FRDM-MCXN947
uses Ethernet by default. The sample derives a stable device ID at runtime from Zephyr's
hardware-info API. `CONFIG_SPOTFLOW_DEVICE_ID` is only a fallback for boards that cannot provide a
hardware ID.

## Build and flash

Build with sysbuild because automatic main-firmware updates require MCUboot:

```bash
west build --sysbuild --board frdm_rw612 spotflow/zephyr/samples/showcase --pristine
west flash
```

For FRDM-MCXN947, use:

```bash
west build --sysbuild --board frdm_mcxn947/mcxn947/cpu0 \
  spotflow/zephyr/samples/showcase --pristine
west flash
```

The bundled MCUboot key and configuration are suitable for demonstration only.

## Telemetry walkthrough

After connecting, the application emits telemetry every five seconds:

- `DEBUG`, `INFO`, periodic `WARNING`, and periodic `ERROR` logs
- an aggregated integer cycle metric
- an immediate floating-point temperature metric
- a labeled, aggregated operation-duration metric
- labeled integer values and labeled and label-less events
- automatically collected heap, network, CPU, stack, connection, reset-cause, and heartbeat metrics

The main thread is registered explicitly for stack monitoring to illustrate selective stack
collection. Session metadata contains string, integer, float, and boolean application labels.

Change the device's minimum sent log severity in the Spotflow portal while the sample runs. UART
still displays locally enabled messages, while the Spotflow backend applies the cloud setting.
Because settings and NVS are enabled, the selected severity persists across reboots.

To test crash reporting while no OTA is active, hold the button for at least three seconds. Zephyr
stores a coredump and reboots; after reconnecting, the SDK uploads the coredump before ordinary
telemetry. Upload the matching ELF file from the build to Spotflow to enable symbolization.

## Automatic main-firmware OTA walkthrough

1. Build and flash this sample, then build a modified version to use as the update image.
2. Upload `build/showcase/zephyr/zephyr.signed.bin` as main firmware and target this device with a deployment.
3. When the update reaches `PENDING_DOWNLOAD`, the sample queries and logs safe artifact metadata,
   pauses for three seconds, and resumes automatically. It never logs the artifact URL or secret.
4. Long-press while the update is abortable to report it as failed.
5. Otherwise, let the device reboot into the new image. It remains unconfirmed until a short press.
6. Short-press to confirm it, or long-press to reboot without confirmation and demonstrate MCUboot rollback.

The pause is performed once per attempt to make the pause/resume APIs visible without requiring
another input control.

## Delegated firmware walkthrough

Create a non-main firmware artifact in the same or a separate deployment. The sample streams it
through `spotflow_download_artifact()`, validates block offsets, and calculates a demonstration
checksum. After 4096 bytes it pauses for three seconds and resumes automatically. A long press or
an actionable cancellation from the cloud cancels the downloader.

This is a simulated external-MCU installation: successfully receiving the artifact is reported as
success. Production code must replace the final section of `spotflow_on_handle_firmware_update()`
with idempotent verification, programming, activation, and persistent version tracking for the
actual external target.

## Source map

| File | Purpose |
|---|---|
| `src/identity.c` | Runtime device ID and typed session metadata labels |
| `src/telemetry.c` | Logs, custom metrics, events, and selective system stack metrics |
| `src/ota.c` | Automatic and delegated OTA callbacks and downloader lifecycle |
| `src/button.c` | Interrupt-driven short/long-press classification |
| `src/main.c` | Network initialization and non-ISR action dispatch |

For production integration details, see the public guides for
[Zephyr logging](https://docs.spotflow.io/guides/zephyr/logging-zephyr),
[metrics](https://docs.spotflow.io/guides/zephyr/metrics-zephyr),
[crash reports](https://docs.spotflow.io/guides/zephyr/crash-reports-zephyr),
[main OTA](https://docs.spotflow.io/guides/zephyr/ota-zephyr), and
[external-MCU OTA](https://docs.spotflow.io/guides/zephyr/ota-external-mcu-zephyr).
