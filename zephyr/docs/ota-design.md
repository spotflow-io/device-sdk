# Zephyr OTA — implementation design note

This document summarizes important design choices in the Spotflow Zephyr OTA device module.
It is written for **SDK contributors** working on the implementation.

Integrators should use the public product documentation
([Over-the-air updates with Zephyr](https://docs.spotflow.io/guides/zephyr/ota-zephyr))
and the public headers (`spotflow/ota.h`, `spotflow/downloader.h`) instead.

## Module map

| Module | Responsibility |
|---|---|
| `spotflow_ota.c` | Public facade; delegates to internal modules; idempotent init |
| `spotflow_ota_state.c` | In-memory attempt/artifact state; mutex-protected transitions |
| `spotflow_ota_worker.c` | Dedicated worker thread; artifact sequencing and job dispatch |
| `spotflow_ota_cbor.c` | C2D decode / D2C encode; protocol limits and attempt errors |
| `spotflow_ota_net.c` | Pending D2C merge and MQTT publish wrapper |
| `spotflow_ota_persistence.c` | Zephyr Settings load/save for attempt, results, versions, probation |
| `spotflow_ota_records_cbor.c` | CBOR encoding of persisted records |
| `spotflow_ota_downloader.c` | Public downloader API; HTTP streaming, pause/resume/cancel |
| `spotflow_ota_downloader_transport_socket.c` | Socket/TLS transport for downloads |
| `spotflow_ota_platform.c` | MCUboot confirm, test upgrade, reboot, flash slot access |
| `spotflow_ota_identity.c` | Running and downloaded image build ID reads |
| `spotflow_ota_fw_main.c` | Automatic main firmware pre/post-reboot flow |
| `spotflow_ota_fw_custom.c` | Delegated firmware dispatch; weak default callbacks; cancel work item |

MQTT subscription and inbound routing live in `spotflow_mqtt.c` / `spotflow_processor.c`.
C2D payloads are decoded on the MQTT thread; all durable work is handed to the OTA worker.

## Threading and synchronization

```
MQTT thread     → decode C2D, enqueue worker jobs, poll pending D2C send
OTA worker      → state transitions, HTTP downloads, flash, user firmware callbacks
sysworkq        → spotflow_on_update_canceled()
App threads     → public API (pause/resume/fail/confirm/query)
```

`spotflow_download_artifact()` blocks its caller on the OTA worker thread. There is no
separate download thread. Automatic main-firmware downloads and delegated firmware
handlers (including typical `spotflow_download_artifact()` use) all run on the OTA worker.

**Rules:**

- `state_mutex` protects in-memory attempt state only.
- No user callbacks, persistence, network, downloader, flash, or MQTT while holding
  `state_mutex`.
- Public API calls update state under the mutex, release it, then perform I/O or wake the
  worker.
- Main-firmware progress callbacks fire from the OTA worker after state changes, not from
  the calling application thread.

Cancellation uses `sysworkq` because the OTA worker may be blocked inside
`spotflow_on_handle_firmware_update()`. A work item calls `spotflow_on_update_canceled()`
without holding `state_mutex`.

## Attempt and artifact state model

**Attempts**

- One **current** attempt in RAM and persistence.
- One **pending newer** attempt in RAM only (not fully persisted as a second active
  attempt). Promoted when supersession is safe.

**Artifacts**

- Processed strictly in manifest order.
- Each artifact has a terminal result: `SUCCEEDED`, `FAILED`, or `CANCELED`, or is still
  in progress.
- A whole-attempt **`updateAttemptError`** (reject before processing) is terminal for the
  entire attempt and does not merge with per-artifact result arrays.

**Inbound message handling**

| Condition | Behavior |
|---|---|
| Duplicate same `updateAttemptId` | Ignored |
| New attempt while current is terminal | Accept and start |
| New attempt while current is unfinished | Supersede when safe; stash one pending newer attempt |
| Malformed message, untrusted attempt ID | Logged and ignored |
| Malformed message, trusted attempt ID | Rejected with `updateAttemptError` |

**Supersession**

When a newer manifest arrives mid-attempt, the worker stops the current artifact when it
can do so safely (for example between download chunks or before starting the next
artifact). Results for the superseded attempt are reported; processing continues with the
promoted attempt.

A malformed `UPDATE_ARTIFACTS` message with a trustworthy but different attempt ID
follows the same supersession path: the rejection (`updateAttemptError`) is stored in the
single pending slot and reported only after the superseded attempt reaches terminal
results and is promoted.

## Main firmware

**Pre-reboot (automatic handling)**

The OTA worker streams the image through the public downloader into the MCUboot secondary
slot, persists probation metadata, requests `BOOT_UPGRADE_TEST`, and reboots. Public
pause/resume/fail APIs apply only in this phase.

**Probation record**

Stored in Settings before requesting test upgrade. Contains enough context to correlate the
next boot with the attempt, artifact, slug, version, and expected build ID.

**Post-reboot reconciliation (`spotflow_ota_init`)**

| Running build ID vs probation | MCUboot confirmed | Outcome |
|---|---|---|
| Match | No | Phase `UNCONFIRMED`; wait for `spotflow_confirm_main_firmware_image()` |
| Match | Yes | Infer success; persist version/result; clear probation; queue D2C |
| Mismatch | — | Infer rollback/failed swap; report main failed; cancel rest; clear probation |
| Unavailable | — | Report main failed; cancel rest; clear probation |

The main artifact result stays **pending** until confirmation or rollback inference so
the cloud does not see success before the device has actually run the new image.

**Identity**

Build ID from Zephyr binary descriptors (`CONFIG_SPOTFLOW_GENERATE_BUILD_ID`) is used to
compare running and downloaded images. Downloaded-image read failure is treated as a main
firmware failure input.

## Persistence

Settings keys (namespace `spotflow/ota/`):

- **Attempt** — latest received attempt ID, terminal artifact results, actionable
  cancellation, whole-attempt errors.
- **Probation** — main firmware in-flight context across reboot.
- **Version / &lt;slug&gt;** — last installed version per firmware slug.

**Write ordering**

Terminal artifact results and attempt metadata are persisted **before** queueing D2C
reporting. Probation is written **before** requesting MCUboot test upgrade. This ordering
reduces the window where a power loss leaves the cloud and device inconsistent.

**Corruption**

Corrupt records start from a safe empty view. The SDK must not report success for an
unknown image. When probation context is still trustworthy, report main `FAILED` and
remaining `CANCELED`; otherwise clear probation and wait for a new cloud attempt.

## Networking and results

- D2C publishes on `ota-cbor-d2c` at QoS **0**.
- The MQTT processing loop polls `spotflow_ota_net_send_pending_message()` alongside other
  outbound traffic.
- Multiple artifact results for the same attempt are **merged** into one pending D2C
  message before publish.
- If publish returns `-EAGAIN`, the pending message is retained and retried on the next
  poll.
- `REPORT_UPDATE_RESULTS` triggers re-send from persisted terminal state.

## Cancellation (implementation)

- Cancellation is **accepted** only while no artifact has yet succeeded in the attempt.
- After acceptance, `spotflow_is_update_canceled()` is true until the running artifact
  returns a terminal result.
- If the running artifact later returns `SUCCEEDED`, that success is kept; cancellation
  is no longer actionable for remaining artifacts unless the handler itself was canceled.
- Late `CANCEL_UPDATE` after partial success is logged internally, not exposed through the
  public API.

Delegated handlers should poll `spotflow_is_update_canceled()` and call
`spotflow_cancel_download()` when a download is active.

## v1 non-goals

- Persisting download byte offset across reboot (retries restart the HTTP download).
- D2C QoS 1 (cloud uses `REPORT_UPDATE_RESULTS` for recovery).
- Public API for ignored/late cancellation diagnostics.
- Application-provided full-manifest handling (SDK owns manifest processing).
- Multiple historical attempts in persistence (only latest attempt is stored).

## Testing

Unit tests live under `spotflow/zephyr/tests/ota/` and run on `native_sim`:

```bash
west twister -T spotflow/zephyr/tests/ota -p native_sim --inline-logs
```

| Area | Test directory | Fakes |
|---|---|---|
| CBOR protocol | `cbor/` | — |
| State machine | `state/` | — |
| Persistence | `persistence/` | Settings test backend |
| D2C networking | `net/` | MQTT publish |
| Facade / MQTT routing | `facade/` | MQTT, worker |
| Worker / delegation | `worker/` | Custom handler, persistence, net |
| Downloader | `downloader/` | Transport |
| Platform / identity | `platform/` | MCUboot, build ID |
| Main firmware | `fw_main/` | Downloader, platform, persistence, net |

Hardware validation uses `spotflow/zephyr/samples/ota` with sysbuild and MCUboot.
