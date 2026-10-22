# CC35X1 startup stall investigation

Date: 2026-10-22. Board: `lp_em_cc35x1/cc3551e`, test device `ti-cc35`.

## Diagnosis

The first-trial startup stall is in TI's flash/clock integration. ITS initialization
runs before `main` and erases its scratch metadata sectors. TI's flash polling
routines read `SYSTIM.TIME1U` from RAM with interrupts locked. The Zephyr port uses
SysTick and does not execute the TI `Power_init()` path that normally enables the
SYSTIM peripheral clock. The counter was therefore being used without its clock.

On failure, the erase routine returned `FLASH_STIG_TIMEOUT` from its execute-erase
stage, skipped its controller/XIP restoration, and attempted to return to a caller
in external flash. This turned an erase error into a permanent startup stall while
interrupts remained locked.

Enabling SYSTIM early, with the original flash and XIP routines restored, completed
first-trial updates in both physical vendor slots. Diagnostics emitted **after** ITS
initialization reported success and showed that the clock had been disabled before
our initialization on both the trial and confirmation boots. The post-ITS print
cannot hide an error that would otherwise prevent ITS from returning.

The shared Spotflow firmware coordinator and PSA lifecycle required no changes.

## Evidence and isolation

Deferred logs made the last Wi-Fi firmware-version line misleading. Direct UART
markers proved that the two-second sleep and Wi-Fi station-role initialization
returned. The remaining startup stack was in ITS scratch-sector erase.

TI relocks debug on a device reset. Debug authentication resets/halts M33, so the
post-authentication CPU registers are not a live stalled backtrace. We used retained
RAM phase records and saved Zephyr thread stacks, alongside direct UART markers.
A healthy boot and a later, separate transport panic were also inspected live with
GDB/OpenOCD.

The memory-only probes recorded:

- `FLASH_STIG_TIMEOUT` (`1`) from execute-erase configuration stage `9`.
- The execute-erase completion marker, without the post-cleanup entry marker.
- Timeout start `0`, current `207580`, threshold `200000`, and last polled flash
  status `0x03000303` in one failing run.
- A retained main-thread stack through `tfm_its_init()`, metadata scratch erase,
  `XMEMWFF3_erase()`, `doErase()`, and the RAM flash routines.

The unclocked counter is not a valid time base. The numerical observations alone
are insufficient to establish normal flash timing; the clock-only hardware
experiment isolates the missing initialization.

| Image | Change under test | Outcome |
|---|---|---|
| `0.0.9.0` | Original executable, newer numeric version | First trial stalled. |
| `0.0.10.0`, `0.0.12.0`, `0.0.13.0` | Progress/timer probes | Reproduced; Wi-Fi completion established. |
| `0.0.11.0`, `0.0.14.0` | Heavier UART/thread probes | Passed; instrumentation hid the symptom. |
| `0.0.15.0`-`0.0.17.0` | Memory-only flash probes | Reproduced; timeout and skipped cleanup captured. |
| `0.0.18.0` | Erase-error cleanup and XIP mask correction | Trial and confirmation passed. |
| `0.0.19.0` | XIP mask correction alone; no probes | Stalled; mask correction alone is insufficient. |
| `0.0.20.0` | Shared cleanup plus synchronous ITS result | Passed; ITS returned success on both boots. |
| `0.0.21.0` | SYSTIM initialization; original flash/XIP code | Passed into vendor slot 2; ITS succeeded. |
| `0.0.22.0` | Same clock-only executable, newer metadata | Passed into vendor slot 1; ITS succeeded. |
| `0.0.23.0` | Combined fixes, no probes | Passed into vendor slot 2 on the first trial. |
| `0.0.24.0` | Final source through normal Kconfig/CMake build | Passed into vendor slot 1 on the first trial. |

The four consecutive updates from `0.0.21.0` through `0.0.24.0` required no manual
reset or retry. The final two used all fixes without probes. The board is confirmed
on `0.0.24.0`, build ID `e0b3ff25bc66df110686b00c6c177f52aad27faf`, and Wi-Fi/MQTT
reconnected. Cloud reports one succeeded device for each of the four deployments.
The final signed artifact is 912904 bytes, below the 1114132-byte limit; RAM usage is
393776 of 520192 bytes. `b-psa` contains this normal build, with both the numeric
version and build-ID extra input set to `0.0.24.0`.

All rollouts targeted the existing dedicated cohort `xVb0ptSkTbwF`, containing only
`ti-cc35`. Failed unconfirmed trials were recovered through reset and rollback.
No factory erase, reprovisioning, or permanent debug-policy change was needed.

## Final HAL changes

The source changes are in the `modules/hal/ti` repository and remain unstaged and
uncommitted, as requested for other repositories.

1. `simplelink_wff3/kernel/zephyr/dpl/ClockP_zephyr.c` enables `SYSTIM.CLKCFG.EN`
   during `PRE_KERNEL_2`, before any flash users. It does not initialize TI's entire
   power-management policy.
2. `simplelink_wff3/source/ti/drivers/xmem/flash/FlashWFF3.c` routes read, write,
   and erase completion through a RAM helper that attempts controller/XIP
   restoration on command/preparation errors and preserves the original error.
3. `simplelink_wff3/source/ti/devices/cc35xx/driverlib/xip.c` parenthesizes the
   composite mask used when waiting for OTFDE to become active or suspended.
4. Flash polling uses the raw microsecond counter. Previously it divided
   `TIME1U` by the RTOS tick period, then compared it with a microsecond threshold.
   With this build's 1000-us tick period, an intended 200-ms timeout could become
   200 seconds. Keeping the raw counter also fixes wrap arithmetic. The public
   `FlashSetTickPeriod()` entry point remains as a compatibility no-op, documented
   in `FlashWFF3.h`.

The mask and timeout-unit defects are independently established. They are not
substitutes for the missing-clock diagnosis. Expanding the old mask produced
`(status & ACTIVE | SUSPENDED) == 0`, which is always false; ARM disassembly confirms
that the original enable routine contains no polling loop.

All temporary changes to the Wi-Fi driver, scheduler, idle/fault handlers, startup
tracing, and ITS initialization were removed.

## Regression checks

The host tests compile the actual TI sources against simulated **32-bit** MMIO
registers. They do not emulate flash electrical timing.

- XIP enable: disabled hardware must wait; active and suspended hardware return
  (three cases). The original source fails; the corrected source passes.
- STIG read/write/erase: success, timeout, and preparation failure, crossed with
  1-us/1000-us RTOS periods and normal/wrapping counters (36 cases).
- The original cleanup fails the injected timeout by returning with XIP disabled.
- Cleanup with the old time units fails the period regression: 2000 polling
  commands instead of two for the same simulated 200-ms deadline.

Run from Windows:

```powershell
wsl.exe -d Ubuntu -- bash /mnt/c/ti/cc35/analysis/psa-fwu/run-flash-regressions.sh
```

The existing Spotflow native suites were not rerun for these HAL-only source
changes. The previous 163-test refactor result remains historical evidence, not a
claim that those tests exercise TI hardware.

## Evidence files and limits

Workspace-relative evidence is under `analysis/psa-fwu/`:

- `startup-investigation-uart.log`: all experiments, synchronous ITS results,
  rollback reports, and final confirmations.
- `startup15-retained.log`, `startup16-retained.log`, `startup17-retained.log`:
  RAM phase/error records and retained stack evidence.
- `startup15-xip-enable-disassembly.log`: the optimized-away original wait loop.
- `xip-original-test.log`, `xip-fixed-test.log`, `flash-original-test.log`,
  `flash-fixed-test.log`, `flash-old-time-units-test.log`: negative and positive
  regression results.
- `image-0.0.21.0/` onward: full local ELFs, signed images, and build logs.
- `startup-final-builds.json`, `startup-final-validation.json`: final identities,
  signed-artifact checks, RAM helper placement, and cloud success counts.
- `hal-startup-fixes.patch`: the four-file unstaged HAL patch.

Diagnostic images through `0.0.23.0` were packaged locally from a fixed CMake
configuration to avoid rebuilding unrelated objects for each numeric version.
Their embedded build IDs and signed containers used the diagnostic version;
`diagnostic-packaging.json` records that distinction. The final normal build uses
Kconfig/CMake for the version, hash input, packaging, and signing together.

One heavily instrumented, debug-resumed `0.0.14.0` download hit a separate
heap-corruption panic in TI's transport thread. Its live GDB evidence is in
`startup14-download-heap-fault.log`. A full hardware reset recovered it; subsequent
clean runs did not repeat it. Its root cause was not established, and these startup
fixes should not be presented as a fix for that separate observation.

Validation covers this CC3551E board and flash profile. It is not a general audit
of all TI flash error paths or other CC35XX boards.
