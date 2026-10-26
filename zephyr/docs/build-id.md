# Build ID generation

Spotflow hashes allocated ELF sections and compact-log metadata, excluding the build
ID value itself. The post-build step patches the resulting 20-byte ID into the ELF
and its related binary artifacts before vendor packaging and signing.

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

For direct use, pass `--extra-input=VALUE` to `scripts/patch_build_id.py`, followed by
the ELF and optional related files. Alternatively, `--extra-input-file=PATH` reads the
exact UTF-8 file content. CMake uses this file form so metadata containing quotes,
semicolons or shell characters is never interpreted as part of a command. Omit both
options to retain executable-only hashing.
