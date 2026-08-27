# LP-EM-CC2340R5 Minimal Backend Memory

These measurements come from the `ble_minimal` sample built with the TI Zephyr
downstream and Zephyr SDK 0.16.8. They are link-time reservations from
`zephyr.map` and Zephyr's `ram_report`, not runtime high-water measurements.

## Image Summary

| Memory | Used | Capacity | Free | Usage |
|---|---:|---:|---:|---:|
| SRAM | 32,236 B | 36,864 B | 4,628 B | 87.45% |
| Application flash | 207,524 B | 524,288 B | 316,764 B | 39.58% |

The TI CCFG region uses its separate 2,048 B allocation in full.


## Source Ownership

Zephyr's `ram_report` attributes 31,191 B to symbols. The remaining 1,045 B is
address-space reservation and padding that the symbol-level report does not
assign to a source path. The source-level view is:

| Source group | Symbol-attributed SRAM | Image share | Main allocations |
|---|---:|---:|---|
| Zephyr kernel and subsystems | 11,613 B | 36.03% | Kernel heaps/stacks, Bluetooth host, logging, coredump backend, and drivers. |
| TI SimpleLink HAL and runtime | 9,818 B | 30.46% | Controller heap, TI DPL object slabs, crypto/RF/RNG state, and drivers. |
| Generated and pathless symbols | 4,503 B | 13.97% | ISR stack, kernel thread objects, TI controller globals, and shared generated state. |
| Spotflow module and sample source tree | 3,575 B | 11.09% | Minimal telemetry state, processor thread, configuration, and sample handles. |
| Symbols hidden by the report | 1,682 B | 5.22% | Linker-generated and tool-hidden symbols. |
| Unattributed reservation and padding | 1,045 B | 3.24% | Primarily reserved address space not represented as ordinary symbols. |
| **Total used SRAM** | **32,236 B** | **100%** | |

Source ownership is not identical to logical ownership. For example, Zephyr
places thread objects in generated sections, while the thread may be created by
Spotflow or TI. The detailed views below classify the largest allocations by
their runtime purpose.

## Zephyr Breakdown

The 11,613 B attributed directly to `ZEPHYR_BASE` is distributed as follows:

| Zephyr component | Size | Notable allocations |
|---|---:|---|
| Kernel | 7,142 B | 3,008 B dynamic stack pool, 1,536 B system heap, 1,280 B system work queue stack, 896 B main stack, and 320 B idle stack. |
| Bluetooth host | 2,412 B | ATT slabs and buffers, ACL TX buffers, connection state, HCI buffers, L2CAP, and GATT state. |
| Logging | 1,307 B | 640 B log processor stack, 256 B deferred log buffer, 168 B thread object, and logging control state. |
| Drivers | 444 B | SPI, flash, entropy, GPIO, serial, Bluetooth HCI, and timer state. |
| Coredump backend | 128 B | Flash stream state, semaphore, and read buffers. |
| Architecture support | 72 B | ARM coredump and TLS state. |
| Power management | 68 B | PM state and notification data. |
| C library and OS helpers | 36 B | Allocator and output hooks. |
| Storage | 4 B | Flash-map state. |
| **Total attributed to Zephyr** | **11,613 B** | |

The 3,008 B dynamic stack pool contains two approximately 1,500 B stacks used
by TI runtime tasks. It appears under the Zephyr kernel because Zephyr provides
the dynamic-thread stack allocator, even though TI controller activity drives
the allocation.

## TI Breakdown

The TI SimpleLink module contributes 9,818 B of symbol-attributed SRAM:

| TI component | Size | Notable allocations |
|---|---:|---|
| BLE controller heap and descriptor | 6,292 B | 6,272 B fixed link-layer heap plus its 20 B Zephyr heap descriptor. |
| TI DPL compatibility layer | 1,549 B | Fixed slabs for clocks, tasks, semaphores, message queues, and events. |
| TI drivers and runtime | 1,973 B | Crypto objects, RF command state, RNG pool state, power management, battery monitor, and driver configuration. |
| Device support | 4 B | Low-level radio clock dependency state. |
| **Total attributed to TI HAL** | **9,818 B** | |

Additional TI BLE controller globals are linked from generated or prebuilt
objects and therefore appear in the `Generated and pathless symbols` group.

## Spotflow Breakdown

The logically identifiable Spotflow allocation is 3,755 B. This adjusts the
source report by excluding a 48 B flash-resident static-thread descriptor and
the sample's 24 B of application state, while including 252 B of Spotflow BLE
transport and Session Metadata state reported without a source path.

| Spotflow component | SRAM | Contents |
|---|---:|---|
| Minimal telemetry backend | 2,101 B | Static metrics, log slots, shared CBOR workspace, coredump cursor, reset state, and scheduler state. |
| Processor thread | 1,320 B | 1,152 B stack and 168 B Zephyr thread object. |
| BLE transport | 168 B | Connection and framing transport state. |
| Session Metadata | 84 B | 64 B workspace and 20 B mutex. |
| Configuration | 58 B | Pending-message buffer, state, and mutex. |
| Device/run identity state | 12 B | Cached device ID and run ID. |
| System metric handles | 12 B | Heap-free, heap-allocated, and connection metric handles. |
| **Identifiable Spotflow SRAM** | **3,755 B** | |

The 2,101 B minimal telemetry backend itself consists of:

| Minimal backend component | SRAM | Contents |
|---|---:|---|
| Metrics | 1,336 B | 792 B series pool, 504 B metric registry, 20 B mutex, and heartbeat/registry state. |
| Shared serializer and scheduler | 257 B | 256 B CBOR workspace and round-robin source index. |
| Logging | 252 B | Two 120 B log slots and 12 B backend/sequence state. |
| Coredumps | 168 B | 128 B chunk buffer, upload cursor, ID, ordinal, and retry state. |
| Reset cause | 37 B | 32 B label value, metric handle, and initialization flag. |
| System metric scheduling | 34 B | Collection deadline, initialization result/state, and mutex. |
| Stack metrics | 17 B | Thread/label pointers, metric handles, and initialization flag. |
| **Minimal backend total** | **2,101 B** | |

Spotflow telemetry performs no heap allocation with the minimal backend. The
1,536 B Zephyr system heap remains available to the platform and supports the
CC23x0 entropy driver's temporary 1,024 B startup allocation.

## Application State

The sample contributes 24 B of directly attributed state: three 4 B metric
handles and a 12 B GPIO callback object. Its code runs on the existing main
thread and does not reserve another application thread stack.

## Runtime Caveats

Link-time allocation answers how SRAM is reserved, but not how much stack and
heap are used at runtime. Hardware validation should measure:

- Main, system work queue, logging, Spotflow processor, ISR, and TI task stack
  high-water marks.
- Peak system-heap use during entropy initialization.
- TI controller heap pressure during connection, notification bursts, and
  reconnects.
- Telemetry behavior during log bursts, metric backpressure, and coredump
  upload.
