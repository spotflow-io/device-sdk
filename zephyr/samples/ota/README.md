# Spotflow OTA sample

This sample connects to Spotflow over MQTT and demonstrates the public OTA API in a minimal
application. Main firmware updates are handled automatically by the device module; the
sample wires up the required callbacks and performs a **demo-only** image confirmation on
boot.

**Full SDK documentation:**
[Over-the-air updates with Zephyr](https://docs.spotflow.io/guides/zephyr/ota-zephyr)

**Create a test deployment:**
[Deploy Over-the-Air (OTA) Updates](https://docs.spotflow.io/guides/ota)

## What this sample implements

| `main.c` symbol | Behavior |
|---|---|
| `confirm_unconfirmed_main_firmware()` | On boot, confirms immediately if phase is `UNCONFIRMED` |
| `spotflow_on_main_firmware_update_progressed()` | Logs phase, pause flag, and result |
| `spotflow_on_handle_firmware_update()` | Stub for delegated firmware — logs and returns `FAILED` |
| `spotflow_on_update_canceled()` | Logs cloud cancellation |

`prj.conf` enables `CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE=y` so the device module
downloads main firmware, requests an MCUboot test upgrade, and reboots without custom code
in the sample.

> **Production note:** This sample confirms the new image as soon as it boots into
> `UNCONFIRMED`. Replace `confirm_unconfirmed_main_firmware()` with your own validation
> logic before calling `spotflow_confirm_main_firmware_image()`.

## Requirements

- Board with [partitions needed for MCUboot](https://docs.mcuboot.com/readme-zephyr.html). Tested with FRDM-RW612 and FRDM-MCXN947 from NXP.
- MCUboot in a **rollback-capable** mode (swap using offset/move/scratch, or direct XIP / RAM load with revert).
- Network connectivity (Wi-Fi by default; set `CONFIG_SPOTFLOW_USE_ETH=y` for Ethernet).

## Set up workspace

The sample can be compiled only within a Zephyr workspace with the Spotflow module.
The easiest way to set up the workspace in a new directory is to follow the second step ([Create West Workspace](https://docs.spotflow.io/quickstart/zephyr#create-west-workspace)) of our [Zephyr quickstart guide](https://docs.spotflow.io/quickstart/zephyr).

If the virtual environment is not activated, activate it:

```bash
# .venv/Scripts/Activate.ps1 in PowerShell
source .venv/bin/activate
```

Navigate to the sample directory:

```bash
cd modules/lib/spotflow/zephyr/samples/ota
```

> **Note:** If the directory doesn't exist, use `cd spotflow/zephyr/samples/ota` instead.

### Credentials

Create a new credentials file from the template:

```bash
cp credentials-sample.conf credentials.conf
```

Edit `credentials.conf` with your Wi-Fi SSID/password and Spotflow device ID / ingest key.
This file is merged automatically by CMake and included in `.gitignore`.

## Build and flash

With the Python virtual environment activated, run the following commands in the sample directory:

```bash
west build --sysbuild --board frdm_rw612 --pristine
west flash
```

## Try an OTA update

After you flash the sample to the board, follow these steps:

1. Wait for the device to connect (watch serial logs for network and MQTT readiness).
2. Modify the sample (such as by addding `LOG_INF("Hello, OTA updates!")`) and rebuild the sample.
3. Follow the instructions in the [OTA update deployment guide](https://docs.spotflow.io/guides/ota) to upload the new firmware image to the [portal](https://app.spotflow.io/) and create a deployment targeting this device.
   Use the firmware image `build/ota/zephyr/zephyr.signed.bin`, which is signed by the default signing key (suitable for testing only).
   When creating the deployment, keep the *Main firmware* toggle selected so that the update is handled automatically by the device module.
4. Observe serial logs during download and reboot. You should see main-firmware progress
   lines such as `phase=DOWNLOADING` and `phase=PENDING_REBOOT`.
5. After reboot into the new image, the sample confirms automatically:

   ```
   Unconfirmed main firmware detected (phase=UNCONFIRMED), confirming via Spotflow OTA
   Main firmware confirmed successfully (phase=NOT_RUNNING result=1)
   ```

7. In the portal deployment view, the device should move to **Succeeded** once results
   are received.

Do not log or share artifact URLs or OTA secrets from serial output. See the
[Zephyr OTA guide](https://docs.spotflow.io/guides/zephyr/ota-zephyr#security)
for logging guidance.

## Sample files

| File | Purpose |
|---|---|
| `prj.conf` | Enables Spotflow, OTA, auto main firmware, NVS settings, networking |
| `sysbuild.conf` | Enables MCUboot (`SB_CONFIG_BOOTLOADER_MCUBOOT=y`); default swap-using-offset mode |
| `sysbuild/mcuboot.conf` | Optional MCUboot log level |
| `boards/frdm_rw612.conf` | Board-specific network buffer tuning |
| `credentials-sample.conf` | Template for `credentials.conf` |
| `src/main.c` | Application callbacks and demo confirmation |

## Kconfig set by this sample

Besides the standard Spotflow and networking options, the sample enables:

```kconfig
CONFIG_SPOTFLOW_OTA=y
CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE=y
CONFIG_NVS=y
CONFIG_SETTINGS_NVS=y
```

`CONFIG_SPOTFLOW_GENERATE_BUILD_ID` defaults to `y` on supported platforms and is required
for automatic main-firmware handling.

## Next steps

- Integrate OTA into your own application using the
  [Zephyr OTA device module guide](https://docs.spotflow.io/guides/zephyr/ota-zephyr).
- Implement `spotflow_on_handle_firmware_update()` if your manifests include non-main
  artifacts.
- SDK contributors: see
  [OTA implementation design note](../../docs/ota-design.md) for internal architecture.
