# Build ID generation

Spotflow uses the build ID to match core dumps and logs with their ELF files and
identify main firmware during OTA updates. The SHA-1 hash includes the bytes and
virtual addresses of allocated ELF sections, `log_strings`, and
`.spotflow.compact_logs`. Log metadata remains part of the hash even when it is
retained only in the ELF. The embedded 20-byte build ID value itself is excluded.
The post-build step patches the resulting ID into the ELF and its related binary
artifacts before vendor packaging and signing.

## Packaging metadata

`CONFIG_SPOTFLOW_BUILD_ID_INCLUDE_TI_VENDOR_IMAGE_VERSION` includes
`CONFIG_CC35XXE_VENDOR_IMAGE_VERSION` in the hash. The TI CC35xx OTA provider enables
this option by default. Changing only the container version therefore changes the
build ID even when the executable is otherwise identical. Disabling the option
preserves executable-only hashing.

CMake writes the configured version to an input file. The generator appends that
file's raw bytes after the ELF section input.
Reproducing this ID requires the ELF and the exact input bytes. The generator
itself remains independent of TI packaging.

An effective Kconfig change regenerates `autoconf.h`, recompiles dependent sources,
and relinks the executable. Linking reruns the ID patcher before TI builds and signs
the container. A cached command-line Kconfig override takes precedence over `prj.conf`;
update or clear that override when testing version-only rebuilds.

## Embedded descriptor and retrieval

The module patches the ID into the ELF as a Zephyr
[binary descriptor](https://docs.zephyrproject.org/latest/services/binary_descriptors/index.html):

- **ID**: `0x5f0` (`5f` resembles `sf` - **S**pot**f**low, `0` stands for our first binary descriptor ID)
- **Type**: `bytes`
- **Length**: `20`

You can retrieve the build ID from the `.elf` file using the following command:

```bash
west bindesc custom_search BYTES 0x5f0 zephyr.elf
```

On architectures without binary-descriptor support (notably ESP32), retrieve the
ID through the ELF symbol table. See `get_build_id_from_elf` in
[`test_patch_build_id.py`](../scripts/tests/test_patch_build_id.py) for an example.

## Patched artifacts

Zephyr generates derived files before the module's post-build command runs, so
the command patches those files as well as `zephyr.elf`. It patches these extensions:

- `.elf`
- `.hex`
- `.bin`
- `.strip`
- `.exe` when **not** targeting [native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html) (it's just a copy of the `.elf` file)

The following outputs are **not** patched and retain an all-zero build ID:

- `.lst` (assembly listing)
- `.uf2`
- `.s19`
- `.exe` when targeting [native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
