#!/usr/bin/env python3

import tempfile
import shutil
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
        hex_bytes = hex_filepath.read_bytes()
        bin_bytes = bin_filepath.read_bytes()
        strip_bytes = strip_filepath.read_bytes()
        exe_bytes = exe_filepath.read_bytes()

        expected_elf_filepath = outputs_dir / "zephyr.elf"
        expected_hex_filepath = outputs_dir / "zephyr.hex"
        expected_bin_filepath = outputs_dir / "zephyr.bin"
        expected_strip_filepath = outputs_dir / "zephyr.strip"
        expected_exe_filepath = outputs_dir / "zephyr.exe"

        expected_build_id = bytes.fromhex("f9931dd776ccc422a576d80eaed1c0fcca8f0331")

        assert elf_bytes[0x260 : 0x260 + len(expected_build_id)] == expected_build_id
        assert bin_bytes[0x160 : 0x160 + len(expected_build_id)] == expected_build_id
        assert strip_bytes[0x260 : 0x260 + len(expected_build_id)] == expected_build_id
        assert exe_bytes[0x260 : 0x260 + len(expected_build_id)] == expected_build_id

        intel_hex = IntelHex(str(hex_filepath))
        assert (
            intel_hex.tobinstr(0x8160, size=len(expected_build_id)) == expected_build_id
        )

        # # Check that nothing else was changed
        # assert elf_bytes == expected_elf_filepath.read_bytes()
        # assert hex_bytes == expected_hex_filepath.read_bytes()
        # assert bin_bytes == expected_bin_filepath.read_bytes()
        # assert strip_bytes == expected_strip_filepath.read_bytes()
        # assert exe_bytes == expected_exe_filepath.read_bytes()
