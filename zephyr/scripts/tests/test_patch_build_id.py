#!/usr/bin/env python3

import filecmp
import shutil
import tempfile
from elftools.elf.elffile import ELFFile
from intelhex import IntelHex
from pathlib import Path

import patch_build_id


def test_patch_build_id():
    with tempfile.TemporaryDirectory() as temp_dir_str:
        temp_dir = Path(temp_dir_str)

        test_dir = Path(__file__).parent
        inputs_dir = test_dir / "patch_build_id" / "inputs"
        outputs_dir = test_dir / "patch_build_id" / "outputs"

        shutil.copytree(inputs_dir, temp_dir, dirs_exist_ok=True)

        elf_filepath = temp_dir / "zephyr.elf"
        hex_filepath = temp_dir / "zephyr.hex"
        bin_filepath = temp_dir / "zephyr.bin"
        strip_filepath = temp_dir / "zephyr.strip"
        exe_filepath = temp_dir / "zephyr.exe"

        assert elf_filepath.exists()
        assert hex_filepath.exists()
        assert bin_filepath.exists()
        assert strip_filepath.exists()
        assert exe_filepath.exists()

        patch_build_id.generate_and_patch_build_id(
            str(elf_filepath),
            [
                str(hex_filepath),
                str(bin_filepath),
                str(strip_filepath),
                str(exe_filepath),
            ],
        )

        elf_bytes = elf_filepath.read_bytes()
        bin_bytes = bin_filepath.read_bytes()
        strip_bytes = strip_filepath.read_bytes()
        exe_bytes = exe_filepath.read_bytes()

        hex_bytes = IntelHex(str(hex_filepath)).tobinstr()

        expected_build_id = bytes.fromhex("f9931dd776ccc422a576d80eaed1c0fcca8f0331")
        assert elf_bytes[0x260 : 0x260 + len(expected_build_id)] == expected_build_id
        assert hex_bytes[0x160 : 0x160 + len(expected_build_id)] == expected_build_id
        assert bin_bytes[0x160 : 0x160 + len(expected_build_id)] == expected_build_id
        assert strip_bytes[0x260 : 0x260 + len(expected_build_id)] == expected_build_id
        assert exe_bytes[0x260 : 0x260 + len(expected_build_id)] == expected_build_id

        read_build_id = get_build_id_from_elf(elf_filepath)
        assert read_build_id == expected_build_id

        expected_elf_filepath = outputs_dir / "zephyr.elf"
        expected_hex_filepath = outputs_dir / "zephyr.hex"
        expected_bin_filepath = outputs_dir / "zephyr.bin"
        expected_strip_filepath = outputs_dir / "zephyr.strip"
        expected_exe_filepath = outputs_dir / "zephyr.exe"

        # Not comparing byte by byte because the files can have different line endings
        expected_hex_bytes = IntelHex(str(expected_hex_filepath)).tobinstr()

        # Check that nothing else was changed
        assert filecmp.cmp(elf_filepath, expected_elf_filepath)
        assert hex_bytes == expected_hex_bytes
        assert filecmp.cmp(bin_filepath, expected_bin_filepath)
        assert filecmp.cmp(strip_filepath, expected_strip_filepath)
        assert filecmp.cmp(exe_filepath, expected_exe_filepath)


def test_patch_build_id_esp32():
    with tempfile.TemporaryDirectory() as temp_dir_str:
        temp_dir = Path(temp_dir_str)

        test_dir = Path(__file__).parent
        inputs_dir = test_dir / "patch_build_id" / "inputs"
        outputs_dir = test_dir / "patch_build_id" / "outputs"

        shutil.copytree(inputs_dir, temp_dir, dirs_exist_ok=True)

        elf_filepath = temp_dir / "zephyr.esp32.elf"
        bin_filepath = temp_dir / "zephyr.esp32.bin"

        assert elf_filepath.exists()
        assert bin_filepath.exists()

        patch_build_id.generate_and_patch_build_id(str(elf_filepath), [str(bin_filepath)])

        elf_bytes = elf_filepath.read_bytes()
        bin_bytes = bin_filepath.read_bytes()

        expected_build_id = bytes.fromhex("bd82337de7372dbde80d5db2aaa0c36edeb2fa97")
        assert elf_bytes[0x808EC : 0x808EC + len(expected_build_id)] == expected_build_id
        assert bin_bytes[0x80BFC : 0x80BFC + len(expected_build_id)] == expected_build_id

        read_build_id = get_build_id_from_elf(elf_filepath)
        assert read_build_id == expected_build_id

        expected_elf_filepath = outputs_dir / "zephyr.esp32.elf"
        expected_bin_filepath = outputs_dir / "zephyr.esp32.bin"

        # Check that nothing else was changed
        assert filecmp.cmp(elf_filepath, expected_elf_filepath)
        assert filecmp.cmp(bin_filepath, expected_bin_filepath)

        processed_elf_filepath = outputs_dir / "zephyr.esp32.processed.elf"

        # Check that the build ID can be still loaded from the ELF file processed by the
        # ESP32 post-build step
        read_build_id_from_processed_elf = get_build_id_from_elf(processed_elf_filepath)
        assert read_build_id_from_processed_elf == expected_build_id


def get_build_id_from_elf(elf_filepath: Path) -> bytes:
    with open(elf_filepath, "rb") as elf_stream:
        elffile = ELFFile(elf_stream)

        bindesc_symbol_vaddr = patch_build_id.find_bindesc_build_id_symbol_vaddr(elffile)

        section = patch_build_id.find_section_containing_vaddr(elffile, bindesc_symbol_vaddr)

        section_start_vaddr = section["sh_addr"]
        symbol_offset_in_section = bindesc_symbol_vaddr - section_start_vaddr

        section_file_offset = section["sh_offset"]
        symbol_file_offset = section_file_offset + symbol_offset_in_section

        with open(elf_filepath, "rb") as elf_stream:
            elf_stream.seek(symbol_file_offset)

            build_id_header = elf_stream.read(patch_build_id.BUILD_ID_HEADER_SIZE)
            if build_id_header != patch_build_id.BUILD_ID_HEADER_VALUE:
                raise Exception(
                    f"Invalid build ID binary descriptor header at offset "
                    f"0x{symbol_file_offset:08x}: {build_id_header.hex()}"
                )

            return elf_stream.read(patch_build_id.BUILD_ID_VALUE_SIZE)
