# OTA platform implementation

The firmware coordinator is independent of bootloader APIs and physical flash maps.
Kconfig and CMake select one implementation of `spotflow_ota_platform.h`; no runtime
registration or virtual-function table is needed.

## Responsibility boundaries

| Layer | Responsibility |
|---|---|
| `firmware/spotflow_ota_fw_main.c` | Controls, identity correlation, probation, installation ordering, reconciliation and result queuing |
| `platform/spotflow_ota_platform_mcuboot.c` | MCUboot writer, test installation, confirmation and reset |
| `platform/spotflow_ota_platform_psa.c` | Serialized PSA finish/install/accept/reboot calls, pending confirmation and standard state normalization |
| `platform/psa/spotflow_ota_psa_ti_cc35xx.c` | TI initialization, active/primary slots, generated size limits and global-operation ownership |
| `platform/psa/spotflow_ota_psa_ti_container.c` | TI prefix, payload bounds and trailer framing, independently of PSA writes |
| `platform/mcuboot/spotflow_ota_mcuboot_stream.c` | MCUboot header decoding and payload bounds, independently of flash layout |
| `platform/spotflow_ota_identity_stream.c` | Native-endian Zephyr descriptor parsing, independently of containers and storage |

The platform writer consumes complete sequential artifact blocks. Its identity operation
returns the completed candidate's executable build ID; its finish operation finalizes the
candidate before probation is saved. Abort cleans an incomplete or unused candidate.
After installation has been requested, the coordinator retains probation even if the
subsequent reboot returns or fails. Startup resolves the actual result.

The state query distinguishes unconfirmed, confirmed, rejected, and query errors.
Confirmation reports whether an application-requested reboot is required to make it
durable. A successful pending request does not queue success, clear probation, or reboot.
The PSA backend remembers that request so repeated confirmation is safe. The sample
intentionally invokes the PSA reboot API directly.

## Identity and containers

Only executable payload bytes reach the descriptor reader. It preserves partial magic,
headers and ID values across blocks, skips unknown padded entries, and rejects invalid
sizes or an all-zero ID. Zephyr descriptors use the executable's native endianness;
MCUboot container headers and TI metadata use little endian independently of that format.

MCUboot encrypted, compressed and split images do not expose an ordinary executable to
this reader and are rejected. RAM-load and fixed-address flags are supported. The
bootloader remains responsible for signature and full image validation. Streamed identity
provides correlation, not an authenticity check or flash readback check.

Both backends reject the running build ID before saving probation. Otherwise a reset
between saving probation and requesting installation would make the old confirmed image
indistinguishable from a completed update. The TI provider enables
`CONFIG_SPOTFLOW_BUILD_ID_INCLUDE_TI_VENDOR_IMAGE_VERSION` by default so the numeric
container version affects identity. See [build ID generation](build-id.md).

## TI CC35xx slot mapping

TI's physical vendor slots exchange running and primary roles. Factory programming into
slot 1 leads to the next update in slot 2, followed by slot 1. Pairing belongs to the TI
implementation, not to PSA component IDs generally. An unconfirmed image's primary slot
must remain available for rollback.

The validated application uses one logical XIP window starting at `0x14000000`. TI's
bootloader and XIP/OTFDE mappings expose either physical vendor slot in that window.
One signed artifact works in both slots; no separately linked executables or whole-image
RAM loading is required. This mapping is a TI hardware/bootloader feature, not a PSA
FWU guarantee.

The provider owns TI implementation fields, `psa_fwu_init()`, generated slot sizes, the
48-byte manifest, and complete-file write offsets. Global installation and acceptance
must not include another client's unfinished components. Container parsing is separate
so another TI provider can reuse framing without copying slot management.

## Extending support

Implement and test another provider before extending the hidden
`SPOTFLOW_OTA_PSA_FWU_AVAILABLE` condition. Add its Kconfig selection and
`zephyr_library_sources_ifdef()` entry. Keep partitions and provisioning in board or
sample integration. Keep the common coordinator independent of vendor states; adapt
component selection, container framing and running-state interpretation at the provider
and platform boundary. Extend the explicit contract if a new confirmation outcome is
needed, rather than adding vendor metadata to probation or the cloud protocol.

Other CC35xx targets using the same SDK service should reuse the existing provider and
generated size symbols. Local board definitions include CC3500E, CC3501E, CC3550E and
CC3551E variants. Only `lp_em_cc35x1/cc3551e` has been hardware-validated here; the sample
retains its restrictions to that board and its fixed flash layout.

PSA FWU also exists through Trusted Firmware-M, including Zephyr's
[MPS2/AN521 sample](https://docs.zephyrproject.org/latest/samples/tfm_integration/tfm_fwu/README.html)
and ST's B-U585I-IOT02A TF-M configuration. TF-M often uses MCUboot inside its secure
service. Logical components, empty start manifests and signed-image writes at offset zero
differ from TI's physical-slot components and prefix. A future TF-M provider must handle
post-boot `READY` and failed-update reporting explicitly and correlate the exact running
identity. It must not assume every `READY` state proves success. No TF-M provider is
implemented or claimed tested by this change.

## CC35XX flash prerequisites

The validated CC3551E setup requires the TI HAL clock/flash corrections described
in the [startup investigation](ota-psa-startup-investigation.md). In particular,
SYSTIM must be clocked before ITS initialization, and flash errors must restore XIP
before returning to code in external flash. These corrections live in
`modules/hal/ti`, separately from the Spotflow provider and coordinator.
