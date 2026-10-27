# Build ID generation

Spotflow uses the build ID to match core dumps and logs with their ELF files and
identify main firmware during OTA updates. The SHA-1 hash includes the bytes and
virtual addresses of allocated ELF sections, `log_strings`, and
`.spotflow.compact_logs`. Log metadata remains part of the hash even when it is
retained only in the ELF. The embedded 20-byte build ID value itself is excluded. The post-build step patches the resulting 20-byte ID into the ELF
and its related binary artifacts before vendor packaging and signing.

## Packaging metadata

`CONFIG_SPOTFLOW_BUILD_ID_INCLUDE_TI_VENDOR_IMAGE_VERSION` includes
`CONFIG_CC35XXE_VENDOR_IMAGE_VERSION` in the hash. The TI CC35xx OTA provider enables
this option by default. Changing only the container version therefore changes the
build ID even when the executable is otherwise identical. Disabling the option
preserves executable-only hashing.

The version is appended as UTF-8 after a fixed domain separator and its byte length,
encoded as an unsigned 64-bit little-endian integer. Reproducing this ID requires
the ELF and the configured vendor image version. The generator itself remains
independent of TI packaging.

An effective Kconfig change regenerates `autoconf.h`, recompiles dependent sources,
and relinks the executable. Linking reruns the ID patcher before TI builds and signs
the container. A cached command-line Kconfig override takes precedence over `prj.conf`;
update or clear that override when testing version-only rebuilds.

## Direct script use

For direct use, pass `--extra-input-file=PATH` to `scripts/patch_build_id.py`,
followed by the ELF and optional related files. The file supplies the exact UTF-8
content to hash. CMake uses this form so metadata is never interpreted as command
syntax. Omit the option to retain executable-only hashing.

## Embedded descriptor and retrieval

Our Zephyr module adds a build command that computes the build ID and patches it into the `.elf` file as the following [binary descriptor](https://docs.zephyrproject.org/latest/services/binary_descriptors/index.html):

- **ID**: `0x5f0` (`5f` resembles `sf` - **S**pot**f**low, `0` stands for our first binary descriptor ID)
- **Type**: `bytes`
- **Length**: `20`

You can retrieve the build ID from the `.elf` file using the following command:

```bash
west bindesc custom_search BYTES 0x5f0 zephyr.elf
```

> **Note:** If the architecture of the device doesn't support binary descriptors (notably, ESP32), retrieving the build ID from the `.elf` file is more complex.
> In this case, it's necessary to use the symbol table, see the function `get_build_id_from_elf` in script tests.

## Patched artifacts

Because Zephyr doesn't allow to insert a post-build command between the compilation of `zephyr.elf` and the generation of derived files such as `zephyr.hex` and `zephyr.bin`, our build command patches these files as well.
In particular, the files with the following extensions are patched:

- `.elf`
- `.hex`
- `.bin`
- `.strip`
- `.exe` when **not** targeting [native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html) (it's just a copy of the `.elf` file)

The files with the following extensions (and others that might be introduced in the future) are **not** patched, so the build IDs stored in them are filled with zeros:

- `.lst` (assembly listing)
- `.uf2`
- `.s19`
- `.exe` when targeting [native simulator](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)

