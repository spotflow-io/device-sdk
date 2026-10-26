# PSA FWU unit tests

The suite builds the real PSA backend, TI provider, container parser and firmware
coordinator against a fake FWU service. The test-only `include/` directory contains
the subset of TI's PSA FWU 1.0 declarations used here, including TI component IDs,
implementation fields and the 48-byte signed-container prefix. It does not require
the TI HAL module, so the standard CI manifest can run it on native_sim and QEMU.

Keep these declarations aligned with the TI HAL when updating the TI manifest.
Hardware builds use the HAL headers and do not include these test declarations.
