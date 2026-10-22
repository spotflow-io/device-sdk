# PSA OTA refactor validation - 2026-10-22

The connected `lp_em_cc35x1/cc3551e` development board completed updates in both physical
vendor slots and is confirmed on `0.0.8.0`, build ID
`f7161884da1a9161c52b57cb4df935a4b59d2998`. Each version's first trial boot stalled at
Wi-Fi initialization before `main`. Hardware reset caused rollback and failure reporting;
retrying the identical signed artifact succeeded. This repeats the startup symptom in
the earlier implementation's validation. Its root cause remains unresolved, and this
run does not establish unattended boot reliability.

## Automated checks

| Check | Passed |
|---|---:|
| PSA provider, lifecycle and descriptor reader | 34 |
| Shared main-firmware coordinator | 49 |
| OTA initialization and reentrant callbacks | 3 |
| Platform identity and MCUboot stream framing | 14 |
| Aggregate state | 37 |
| Main-firmware model | 6 |
| Python build-ID generation and patching | 16 |
| Existing TI Toolbox runner regression tests | 4 |
| Total | 163 |

The native suites ran with `native_sim/native/64` under the host toolchain. The real
MCUboot platform source also passed a native syntax check with `-Werror`; its flash and
bootloader operations were not exercised on MCUboot hardware. Python tests cover native
ELF descriptor headers in both byte orders; the C parser ran on a little-endian host.
No other TI board or TF-M provider was hardware-tested.

`clang-format --dry-run --Werror` passed for all 30 changed C sources and headers.
`git diff --check` passed. Only Spotflow changes were staged and committed; the existing
Zephyr runner changes remained in their original staging state.

## Build identity and packaging

| Numeric version | Embedded build ID | Signed artifact bytes |
|---|---|---:|
| `0.0.7.0` | `20e1cc3f78e65d2fa4d5f1ff52d423ed96b7f124` | 912928 |
| `0.0.8.0` | `f7161884da1a9161c52b57cb4df935a4b59d2998` | 912928 |

Both ELFs produce `1f9b8fd0b583d60fe9d0fca8c0e6cfc01318d0a8` when hashed without extra
metadata. Their executable/log contents therefore match under the generator's existing
hash rules. Including each numeric version reproduces its different embedded ID exactly.
The source no longer logs or otherwise embeds the numeric version to force this change.

The generated build configuration sets `SPOTFLOW_BUILD_ID_EXTRA_INPUT` to the numeric
TI version. Its post-build script reads that value from a UTF-8 metadata file, patches
the ELF and BIN, and then packages and signs the vendor image. The version change
regenerated `autoconf.h`, and the build reran linking, patching, packaging and signing.
No additional version-specific rebuild trigger was necessary.

Both signed artifacts fit the `1114132`-byte signed-image limit. Final RAM usage was
393808 of 520192 bytes (75.70%).

## Hardware sequence

| Update | Attempt | Target | Result |
|---|---:|---|---|
| `0.0.6.0` to `0.0.7.0` | 9 | Component 5 / vendor slot 2 | Trial startup stalled; reset rolled back and reported failure. |
| Same signed `0.0.7.0` image | 10 | Component 5 / vendor slot 2 | Confirmed; cloud reported success. |
| `0.0.7.0` to `0.0.8.0` | 11 | Component 4 / vendor slot 1 | Trial startup stalled; refactored coordinator reported rollback failure. |
| Same signed `0.0.8.0` image | 12 | Component 4 / vendor slot 1 | Confirmed; cloud reported success. |

Successful runs crossed installation and confirmation reboots. Startup correlated the
expected build ID with the confirmed PSA state before queuing success. Wi-Fi and MQTT
reconnected, and persisted OTA records and device configuration survived the resets.
The second download, installation and rollback ran through the new shared coordinator
and extracted TI container parser. Deferred logs immediately before reboot can be lost;
boot markers, startup reconciliation and final cloud results provide the evidence.

Recovery pulsed the hardware reset line through the XDS110/CMSIS-DAP adapter. An attempt
to inspect stalled CPU registers found debug access locked; debug security settings were
preserved. Investigation of the Wi-Fi startup stall is separate from this refactor.

## Local evidence

Workspace-relative evidence is under `analysis/psa-fwu/`:

- `refactor-uart.log`: both updates, rollback recovery and successful confirmations.
- `refactor-build-identities.json`: executable-only and metadata-derived hash comparisons.
- `refactor-test-counts.json` and `native-*-test.log`: native test results.
- `image-0.0.7.0/` and `image-0.0.8.0/`: signed artifacts, ELF files and build logs.
- `deployment-5Z4EjXvIEllG.json` and `deployment-0RNSEQ5TEd9f.json`: final cloud counts.

`b-psa` contains the tested `0.0.8.0` build and its cached numeric-version override.
The sample default remains `0.0.1.0`; future board updates must use a newer numeric
version than the installed `0.0.8.0`.
