# Spotflow OTA with TI PSA FWU

This sample targets `lp_em_cc35x1/cc3551e` with the TI Zephyr 4.4 downstream and the
`is25wj032f/ota` flash profile. Spotflow downloads a signed vendor image through PSA FWU,
reboots into trial execution, and waits for application acceptance. The sample accepts
immediately for demonstration, then explicitly requests TI's confirmation reboot. Replace the
immediate acceptance with application health checks in production.

## Configure and build

Copy `credentials-sample.conf` to `credentials.conf` and enter Wi-Fi and Spotflow
credentials. CMake loads that file automatically. Apply the prerequisites in
[Provision the OTA flash layout](#provision-the-ota-flash-layout), then build from
the workspace root:

```sh
west build -b lp_em_cc35x1/cc3551e -d b-psa modules/lib/spotflow/zephyr/samples/ota_psa
```

Use a short build directory on Windows because the TI Wi-Fi sources have long object
paths. This sample uses TI's native signing pipeline and does not need MCUboot/sysbuild.
The default development signing keys must match the board provisioning; configure the
appropriate signing keys for your device.

## Provision the OTA flash layout

Before building, complete the Toolbox installation and compatibility changes in
[CC35X1 flashing setup](https://docs.spotflow.io/guides/texas-instruments/cc35x1-setup).
Stop before activation and factory programming until this sample is built. The runner
patch below expects those compatibility changes to have been applied first.

Apply the [CC35xx flash startup patch](../../patches/ti/cc35xx-flash-startup.patch)
as described in the [implementation notes](../../docs/ota.md#psa-firmware-update-handler).
Then apply the [Toolbox runner patch](../../patches/ti/cc35xx-toolbox-runner.patch)
from the workspace root so initial programming uses the selected OTA memory map:

```sh
git -C zephyr apply --check ../modules/lib/spotflow/zephyr/patches/ti/cc35xx-toolbox-runner.patch
git -C zephyr apply ../modules/lib/spotflow/zephyr/patches/ti/cc35xx-toolbox-runner.patch
```

Skip either patch if your checkout already includes its changes. Build the sample,
then follow the guide's one-time activation step if the board is not activated yet.
Use `b-psa` wherever that step uses `build`. For factory programming, use the commands
below with this sample's build rather than a previously built BLE or Wi-Fi sample.

After activation, provision the two-slot layout:

```sh
west flash -d b-psa -- --initial-programming
```

If the board was previously programmed with a different layout, use the following
command instead. It erases saved settings, Wi-Fi profiles and keys as well as old
images that could otherwise block PSA updates:

```sh
west flash -d b-psa -- --initial-programming --full-flash-erase
```

An OTA update cannot migrate the single-slot factory layout. Initial programming
writes the `is25wj032f/ota` profile used by this sample. Use OTA deployments for
subsequent firmware updates. On Windows, add the Toolbox installation directory to
`PATH`, or append `--simplelink-tool <path-to-executable>` to the flash command.

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

## Deploy an update

1. Change the application and increment `CONFIG_CC35XXE_VENDOR_IMAGE_VERSION` in
   `prj.conf` (for example, `0.0.1.0` to `0.0.2.0`). TI requires a newer numeric
   container version. The executable build ID must also change; repackaging the same
   executable with an unchanged build ID is rejected. The TI provider enables
   `CONFIG_SPOTFLOW_BUILD_ID_INCLUDE_TI_VENDOR_IMAGE_VERSION` by default, so
   changing only that version also changes the ID.
   The cloud version string does not set the numeric container version.
2. Rebuild and upload `b-psa/zephyr/flash/vendor_image.sign.bin` as the main firmware
   artifact. Include its entire signed container.
3. Create a deployment through the [Spotflow OTA workflow](https://docs.spotflow.io/using-spotflow/guides/ota).
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
