# Build ID generation

Spotflow uses the build ID to match core dumps and logs with their ELF files and
identify main firmware during OTA updates. The SHA-1 hash includes the bytes and
virtual addresses of allocated ELF sections, `log_strings`, and
`.spotflow.compact_logs`. Log metadata remains part of the hash even when it is
retained only in the ELF. The embedded 20-byte build ID value itself is excluded.
The post-build step patches the resulting ID into the ELF and its related binary
artifacts before vendor packaging and signing.

## Packaging metadata

The generator's `--extra-input-file` option appends a file's raw bytes after
the ELF section input. Omitted or empty metadata preserves the original hash.
The input is not decoded or normalized; reproducing the ID requires the ELF
and the exact metadata bytes. Related binary artifacts receive the same ID.

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
