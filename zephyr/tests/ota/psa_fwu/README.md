# PSA FWU unit tests

The suite builds the real PSA backend, TI provider, container parser and firmware
coordinator against a fake FWU service. The test-only `include/` directory contains
the subset of TI's PSA FWU 1.0 declarations used here, including TI component IDs,
implementation fields and the 48-byte signed-container prefix. It does not require
the TI HAL module, so the standard CI manifest can run it on native_sim and QEMU.

Keep these declarations aligned with the TI HAL when updating the TI manifest.
Hardware builds use the HAL headers and do not include these test declarations.

The suite reserves an 8 KiB ztest thread stack and enables the stack sentinel. Its
coordinator fixtures keep a full update manifest and worker job on that stack,
besides the production download/coordinator call chain; Cortex-M3's default 1 KiB
ztest stack cannot hold even those fixture frames.
