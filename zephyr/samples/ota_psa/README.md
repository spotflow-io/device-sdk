# Spotflow OTA with TI PSA FWU

This sample targets `lp_em_cc35x1/cc3551e` with the TI Zephyr 4.4 downstream and the
`is25wj032f/ota` flash profile. Spotflow downloads a signed vendor image through PSA FWU,
reboots into trial execution, and waits for application acceptance. The sample accepts
immediately for demonstration, then explicitly requests TI's confirmation reboot. Replace the
immediate acceptance with application health checks in production.

## Configure and build

Copy `credentials-sample.conf` to `credentials.conf` and enter Wi-Fi and Spotflow
credentials. CMake loads that file automatically. From the workspace root, run:

```sh
west build -b lp_em_cc35x1/cc3551e -d b-psa modules/lib/spotflow/zephyr/samples/ota_psa
```

Use a short build directory on Windows because the TI Wi-Fi sources have long object
paths. This sample uses TI's native signing pipeline and does not need MCUboot/sysbuild.
The default development signing keys must match the board provisioning; configure the
appropriate signing keys for your device.

The board must already be provisioned with the same two-slot OTA layout. An update cannot
migrate the single-slot factory layout. Verify the factory programming configuration
against `zephyr/boards/ti/lp_em_cc35x1/config/flash/is25wj032f/ota` and the generated
`b-psa/zephyr/ti_flash_map_config.c` before programming. Use the runner that passes
the selected profile's MemoryConfigurator JSON to Toolbox; `--enable_ota` alone
does not establish that all factory size defaults match. With that runner, provision
the development board once using:

```sh
west flash -d b-psa --initial-programming --full-flash-erase
```

The full erase is required when migrating layouts: old images in secondary slots
can otherwise appear as unrelated PSA candidates and block vendor updates. It also
erases saved settings, Wi-Fi profiles and keys. Factory programming replaces the
board's flash layout. Use ordinary OTA deployments
for subsequent updates. On Windows, select the installed Toolbox executable with
`--simplelink-tool` if it is not on `PATH`.

If reusing a build directory previously configured for build-only validation, clear
its cached `EXTRA_CONF_FILE=credentials-sample.conf` setting with
`west build -d b-psa -- -DEXTRA_CONF_FILE=`. Otherwise the placeholder values can
override `credentials.conf`.

The overlay describes the running application's logical XIP window and reserves the
physical areas below. TI remaps either vendor slot into that execution window, so one
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

The board configuration reserves a 96 KiB Mbed TLS heap and enables 16 KiB TLS
records for simultaneous MQTT and artifact HTTPS connections. The smaller TLS
settings used by the logs sample are not sufficient for this OTA sample.

## Deploy an update

1. Change the application and increment `CONFIG_CC35XXE_VENDOR_IMAGE_VERSION` in
   `prj.conf` (for example, `0.0.1.0` to `0.0.2.0`). TI requires a newer numeric
   container version. The executable build ID must also change; repackaging the same
   executable with an unchanged build ID is rejected. The TI provider enables
   `CONFIG_SPOTFLOW_BUILD_ID_INCLUDE_TI_VENDOR_IMAGE_VERSION` by default, so
   changing only that version also changes the ID. A cached command-line version
   override takes precedence over `prj.conf`; update or clear it when reusing a build.
   The cloud version string does not set the numeric container version.
2. Rebuild and upload `b-psa/zephyr/flash/vendor_image.sign.bin` as the main firmware
   artifact. Include its entire signed container.
3. Create a deployment through the [Spotflow OTA workflow](https://docs.spotflow.io/guides/ota).
4. Observe the installation reboot, `UNCONFIRMED`, application acceptance and the
   additional confirmation reboot. Startup then verifies the running build and queues success.

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
