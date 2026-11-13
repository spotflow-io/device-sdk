# Spotflow OTA with TI PSA FWU

This Wi-Fi sample targets `lp_em_cc35x1/cc3551e` with the TI Zephyr 4.4 downstream and the
`is25wj032f/ota` flash profile. Spotflow downloads a signed vendor image through PSA FWU,
reboots into trial execution, and waits for application acceptance. The sample accepts
immediately for demonstration, then explicitly requests TI's confirmation reboot. Replace the
immediate acceptance with application health checks in production.

For OTA through a BLE gateway, use the [BLE sample's TI OTA profile](../ble#ota-on-lp-em-cc35x1).
Both transports use the same signed vendor image format and PSA confirmation sequence.
The [TI OTA guide](https://docs.spotflow.io/guides/texas-instruments/ota) walks through both.

## Prepare the workspace

Follow the [OTA guide's prerequisites and workspace steps](https://docs.spotflow.io/guides/texas-instruments/ota#install-prerequisites)
to install the host tools and Toolbox 4.3.23, create the workspace and apply both
compatibility patches before building. The `west-zephyr-ti-cc35x1.yml` manifest pins
TI SimpleLink Zephyr `v4.4.0-ti-10.10.00` and the required modules.
[CC35X1 flashing setup](https://docs.spotflow.io/guides/texas-instruments/cc35x1-setup#apply-compatibility-patches-before-building)
explains how to recognize an already-patched checkout. Stop before activation and
factory programming; those steps require the sample built below.

Complete [XDS110 host setup](https://docs.spotflow.io/guides/texas-instruments/cc35x1-setup#set-up-xds110-host-access)
before activation or flashing: Windows needs TI's XDS110 USB drivers, and Linux
needs suitable `udev` access rules. The standalone XDS110 Support Utilities provide
these; an existing CCS, UniFlash or EMUPack installation with XDS110 support can
also supply them. macOS does not need an additional XDS110 kernel driver.

The [HAL patch](../../patches/ti/cc35xx-flash-startup.patch) corrects the Bluetooth
source guard and enables SYSTIM before early flash and Internal Trusted Storage
(ITS) initialization. The [Toolbox patch](../../patches/ti/cc35xx-toolbox-runner.patch)
adapts Zephyr's image-builder commands and flash runner to Toolbox 4.3.23, including
factory programming with the application's selected layout. Both patches are shared
by the CC35X1 Wi-Fi, BLE and PSA OTA samples.

Run the commands below from the workspace root.

## Configure and build

Copy `credentials-sample.conf` to `credentials.conf` in the sample directory and
enter Wi-Fi and Spotflow credentials. CMake loads that file automatically.
Build from the workspace root:

```sh
west build --pristine -b lp_em_cc35x1/cc3551e -d build modules/lib/spotflow/zephyr/samples/ota_psa
```

Use the shared workspace-root `build` directory. `--pristine` clears the previous
build outputs when switching samples; it does not change the board's flash layout.

This sample uses TI's native signing pipeline and does not need MCUboot/sysbuild.
The default development signing keys must match the board provisioning; configure the
appropriate signing keys for your device.

## Activate and provision the OTA flash layout

With the sample built, perform the flashing guide's one-time activation step if the
board is not activated yet. Then factory-program this sample using the commands
below. Subsequent firmware updates use OTA deployments.

After activation, provision the two-slot layout:

```sh
west flash -d build -- --initial-programming
```

If the board was previously programmed with a different layout, use the following
command instead. It erases saved settings, Wi-Fi profiles and keys as well as old
images that could otherwise block PSA updates:

```sh
west flash -d build -- --initial-programming --full-flash-erase
```

An OTA update cannot migrate the single-slot factory layout. Initial programming
writes the `is25wj032f/ota` profile used by this sample. Use OTA deployments for
subsequent firmware updates. On Windows, add the Toolbox installation directory to
`PATH`, or append `--simplelink-tool <path-to-executable>` to the flash command.

The overlay describes the running application's logical execute-in-place (XIP)
address window and reserves the physical areas below. TI remaps either vendor slot
into that execution window, so one
signed artifact works in both slots. The wireless slots contain TI connectivity firmware
read by the Wi-Fi driver; this sample updates only the vendor application. Both wireless
slots must remain described even though Spotflow does not update them.

The overlay reserves the following physical areas:

| Area | Base | Size |
|---|---|---|
| Wireless slot 1 | `0xce000` | `0x72000` |
| Wireless slot 2 | `0x140000` | `0x72000` |
| Wi-Fi NVS | `0x1b3000` | `0x8000` |
| Application settings | `0x1bb000` | `0x1a000` |
| ITS keys | `0x1d5000` | `0x8000` |
| Vendor slot 1 | `0x1dd000` | `0x111000` |
| Vendor slot 2 | `0x2ee000` | `0x111000` |

The signed artifact must fit the vendor allocation including TI slot overhead: its maximum
size is `0x111000 - 4076` bytes. The adapter enforces this limit while downloading.
Vendor reads use the running image's logical mapping; vendor writes use PSA FWU rather
than a static physical flash partition. Changing the flash profile requires a matching
overlay and factory layout. CMake deliberately rejects other boards, flash profiles,
and custom map/discovery JSON overrides: a profile name alone cannot prove that the
fixed Devicetree addresses match a custom map. Update the overlay, provisioning and
checks together when supporting a different layout.

## Deploy an update

1. Increment `CONFIG_CC35XXE_VENDOR_IMAGE_VERSION` in
   `prj.conf` (for example, `0.0.1.0` to `0.0.2.0`). TI requires a newer numeric
   container version.
   The sample logs the configured vendor image version at startup.
   The cloud version string does not set the numeric container version.
2. Rebuild with `west build -d build` and upload
   `build/zephyr/flash/vendor_image.sign.bin` as the main firmware artifact. Include
   its entire signed container and keep the matching `build/zephyr/zephyr.elf` for
   decoding crash reports. Do not flash the candidate locally.
3. Create a deployment through the [Spotflow OTA workflow](https://docs.spotflow.io/using-spotflow/guides/ota).
4. After the installation and confirmation reboots, verify confirmed startup and
   MQTT reconnection. Check that the cloud update attempt is `Succeeded` and the
   device reports the candidate's build ID. Immediate acceptance can reset the
   sample before deferred UART messages about `UNCONFIRMED` or acceptance appear.

## Confirmation and reboot

`spotflow_confirm_main_firmware_image()` does not reboot. On TI, a successful call
requests acceptance but leaves the update `UNCONFIRMED/PENDING` until durable confirmation is
verified on the next startup. This sample then calls `psa_fwu_request_reboot()` directly.
Do not replace that call with `sys_reboot()`: TI keeps the acceptance request in RAM
until its PSA reboot API transfers it to the bootloader.

If acceptance fails, the sample does not request a confirmation reboot. If a reset loses the pending
acceptance request and TI rolls back, startup reports failure using the saved expected
build ID. Repeated confirmation before the confirmation reboot is harmless. Non-main firmware
updates are delegated to a callback and return `FAILED` in this sample.

See [the implementation note](../../docs/ota.md#psa-firmware-update-handler) and
[TI's OTA guide](https://software-dl.ti.com/simplelink/esd/simplelink_wifi_sdk/10.10.01.08/exports/docs/simplelink_wifi_sdk/html/cc35xx/ota.html).
