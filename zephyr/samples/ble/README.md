# Spotflow BLE sample

This sample sends logs, metrics and crash reports through a Spotflow BLE gateway.
By default, select an ingest key in the gateway. Configure a unique
`CONFIG_SPOTFLOW_DEVICE_ID` in the selected board profile, then follow the
[BLE setup guide](https://docs.spotflow.io/guides/bluetooth-low-energy).

To use a device-provisioned key, set `CONFIG_SPOTFLOW_INGEST_KEY` in a private
configuration overlay or enable `CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC` and implement
`spotflow_on_ingest_key_requested()`. Choose **Use key from device** in the gateway.
Either provisioning method enables BLE encryption by default.
Set `CONFIG_BT_SMP=y` and configure trusted pairing for encrypted connections. An
explicit `CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION=n` allows a connected gateway to read
the key over an unencrypted link. Use this override for controlled testing.
The gateway uses the key for MQTT authentication. With the runtime callback, the
SDK starts advertising after the callback provides the key.

Use the SDK-managed BLE connection for application BLE communication. Your
application can configure pairing and expose custom GATT services on that
connection. See [BLE connection ownership](https://docs.spotflow.io/guides/zephyr/advanced-config-zephyr#ble-connection-ownership).

If you set the Device ID at runtime, have it ready before the SDK starts
advertising. See
[setting the Device ID at runtime](https://docs.spotflow.io/guides/zephyr/advanced-config-zephyr#setting-device-id-at-runtime).

## OTA on LP-EM-CC35X1

Use the TI CC35X1 manifest, host tools and compatibility patches described in
[CC35X1 setup](https://docs.spotflow.io/guides/texas-instruments/cc35x1-setup).
For a walkthrough covering both Wi-Fi and BLE, see
[OTA updates on TI CC35X1](https://docs.spotflow.io/guides/texas-instruments/ota).
The optional configuration below enables persistent settings and TI PSA FWU. Its overlay
uses the OTA factory layout, reserving 32 KiB for settings and 72 KiB for minimal
crash reports within application storage.

From the West workspace root:

```sh
west build --pristine -b lp_em_cc35x1/cc3551e -d build modules/lib/spotflow/zephyr/samples/ble -- '-DEXTRA_CONF_FILE=ota.conf;boards/lp_em_cc35x1_cc3551e-ota.conf' '-DEXTRA_DTC_OVERLAY_FILE=boards/lp_em_cc35x1_cc3551e-ota.overlay'
```

The sample's `ota.conf` sets `CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION=n` for
development. To require encryption, set `CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION=y`
and `CONFIG_BT_SMP=y` in an overlay and append it to the end of the list, for example
`'-DEXTRA_CONF_FILE=ota.conf;boards/lp_em_cc35x1_cc3551e-ota.conf;device-key.conf'`.

Quote each complete `-D` argument, including in PowerShell. Complete the board's
one-time activation if necessary. When migrating from the non-OTA sample, factory
programming requires a full erase; this clears device settings and crash reports:

```sh
west flash -d build -- --initial-programming --full-flash-erase
```

For an already-provisioned matching OTA layout, use regular `west flash -d build`.
On Windows, put Toolbox on `PATH` or pass its executable through `--simplelink-tool`.

Connect through the [Web App gateway](https://docs.spotflow.io/guides/bluetooth-low-energy#open-the-web-app-gateway).
To deploy a new version:

1. Increment `CONFIG_CC35XXE_VENDOR_IMAGE_VERSION` in the `-ota.conf` board file.
2. Rebuild and upload `build/zephyr/flash/vendor_image.sign.bin` as the main firmware
   image. Keep the matching `build/zephyr/zephyr.elf` for decoding crash reports.
3. [Create a deployment](https://docs.spotflow.io/using-spotflow/guides/ota) targeting
   the BLE device and keep the gateway page open.
4. Observe image transfer, installation reboot, application confirmation, and the TI
   confirmation reboot. Verify success in the deployment.

Both baseline and candidate must contain the same OTA layout and BLE OTA support.
The sample immediately confirms an unconfirmed image for demonstration and requests
TI's PSA confirmation reboot. Replace this with application health checks in a product.
The SDK retains the same confirmation, rollback, cancellation and delegated-artifact
behavior as other transports; see [OTA architecture](../../docs/ota.md).

## BLE OTA configuration

OTA enables `CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION` by default. The sample's
`ota.conf` disables it for development, including when the device supplies an ingest key. Enable
`CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION=y` and `CONFIG_BT_SMP=y`, and configure trusted
pairing before production use. TI signing keys must match the board's provisioning;
replace the default development keys for production.

The TI profile disables `CONFIG_MBEDTLS_PSA_P256M_DRIVER_ENABLED` so encrypted BLE
can use the built-in P-256 implementation and retain the legacy EC entry points
required by TI's radio library. Keep this setting when enabling encryption on this
TI downstream.

`CONFIG_SPOTFLOW_OTA_BLE_REQUEST_TIMEOUT_MS` limits each gateway OPEN, READ or CLOSE
exchange, including retransmissions. It defaults to 120 seconds; the complete image
transfer may take longer. Timeouts retry with backoff from the last consumed block.
See the [OTA configuration reference](https://docs.spotflow.io/guides/zephyr/ota-zephyr#kconfig-options)
for timeout, block-size and retry settings.
