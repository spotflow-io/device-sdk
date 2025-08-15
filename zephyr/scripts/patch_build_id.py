#!/usr/bin/env python3

import sys
import os
import hashlib
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import Symbol
from elftools.elf.constants import SH_FLAGS
from intelhex import IntelHex


SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME = "bindesc_entry_spotflow_build_id"

# Binary descriptor ID is 0x5f0, type is bytes, and length is 20
BUILD_ID_HEADER_VALUE = b"\xf0\x25\x14\x00"

BUILD_ID_HEADER_SIZE = len(BUILD_ID_HEADER_VALUE)
BUILD_ID_VALUE_SIZE = 20


def generate_and_patch_build_id(elf_filepath: str, other_filepaths: list[str]):
    with open(elf_filepath, "rb+") as elf_stream:
        elffile = ELFFile(elf_stream)

        bindesc_build_id_symbol = find_bindesc_build_id_symbol(elffile)

        build_id = generate_build_id(elffile, bindesc_build_id_symbol)

        patch_build_id_parsed_elf(
            elffile, elf_filepath, bindesc_build_id_symbol, build_id
        )

        for filepath in other_filepaths:
            if not os.path.exists(filepath):
                # It is easier to check it here than in CMake
                continue

            if (
                filepath.endswith(".elf")
                or filepath.endswith(".exe")
                or filepath.endswith(".strip")
            ):
                patch_build_id_elf(filepath, bindesc_build_id_symbol, build_id)
            elif filepath.endswith(".hex"):
                patch_build_id_hex(filepath, bindesc_build_id_symbol, build_id)
            elif filepath.endswith(".bin"):
                patch_build_id_bin(filepath, elffile, bindesc_build_id_symbol, build_id)
            else:
                raise Exception(f"Unsupported file type: {filepath}")


def find_bindesc_build_id_symbol(elffile: ELFFile) -> Symbol:
    symbol = None

    symbol_table = elffile.get_section_by_name(".symtab")
    if symbol_table is None:
        symbol_table = elffile.get_section_by_name(".dynsym")

    if symbol_table is not None:
        symbol = symbol_table.get_symbol_by_name(SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME)

    if symbol is None:
        raise Exception(f"Symbol '{SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME}' not found")

    if len(symbol) > 1:
        raise Exception(
            f"Symbol '{SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME}' found multiple times"
        )

    return symbol[0]


def generate_build_id(elffile: ELFFile, bindesc_build_id_symbol: Symbol):
    hash_builder = hashlib.sha1()

    for section in elffile.iter_sections():
        # Only include sections that will be loaded to the device memory
        if section["sh_flags"] & SH_FLAGS.SHF_ALLOC == 0:
            continue

        data = section.data()
        section_start = section["sh_addr"]
        section_end = section_start + section["sh_size"]
        symbol_start = bindesc_build_id_symbol["st_value"]

        # The section address is included in the hash because loading the same data into a
        # different memory address would yield a different memory content
        hash_builder.update(section_start.to_bytes(8, byteorder="little"))

        if section_start <= symbol_start < section_end:
            symbol_start_offset = symbol_start - section_start
            build_id_start_offset = symbol_start_offset + BUILD_ID_HEADER_SIZE
            build_id_end_offset = build_id_start_offset + BUILD_ID_VALUE_SIZE

            # The build ID itself cannot take part in the hash
            hash_builder.update(data[:build_id_start_offset])
            hash_builder.update(data[build_id_end_offset:])
        else:
            hash_builder.update(data)

    build_id = hash_builder.digest()
    assert len(build_id) == BUILD_ID_VALUE_SIZE
    assert (
        bindesc_build_id_symbol["st_size"] == BUILD_ID_HEADER_SIZE + BUILD_ID_VALUE_SIZE
    )

    return build_id


def patch_build_id_elf(
    elf_filepath: str, bindesc_build_id_symbol: Symbol, build_id: bytes
):
    with open(elf_filepath, "rb+") as elf_stream:
        elffile = ELFFile(elf_stream)
        patch_build_id_parsed_elf(
            elffile, elf_filepath, bindesc_build_id_symbol, build_id
        )


def patch_build_id_parsed_elf(
    elffile: ELFFile,
    elf_filepath: str,
    bindesc_build_id_symbol: Symbol,
    build_id: bytes,
):
    for section in elffile.iter_sections():
        section_file_offset = section["sh_offset"]
        section_rom_start = section["sh_addr"]
        section_rom_end = section_rom_start + section["sh_size"]
        symbol_rom_start = bindesc_build_id_symbol["st_value"]

        if section_rom_start <= symbol_rom_start < section_rom_end:
            symbol_section_offset = symbol_rom_start - section_rom_start
            symbol_file_offset = section_file_offset + symbol_section_offset

            elffile.stream.seek(symbol_file_offset)
            header = elffile.stream.read(BUILD_ID_HEADER_SIZE)
            if header != BUILD_ID_HEADER_VALUE:
                raise Exception(
                    f"Invalid build ID binary descriptor header at offset "
                    f"0x{symbol_file_offset:08x}: {header.hex()}"
                )

            build_id_file_offset = symbol_file_offset + BUILD_ID_HEADER_SIZE

            print(
                f"Patching build ID '{build_id.hex()}' into ELF file '{elf_filepath}' at offset: "
                f"0x{build_id_file_offset:08x}"
            )
            elffile.stream.seek(build_id_file_offset)
            elffile.stream.write(build_id)

            break


def patch_build_id_hex(
    hex_filepath: str, bindesc_build_id_symbol: Symbol, build_id: bytes
):
    intel_hex = IntelHex(hex_filepath)

    symbol_address = bindesc_build_id_symbol["st_value"]
    header = (
        intel_hex[symbol_address : symbol_address + BUILD_ID_HEADER_SIZE]
        .tobinarray()
        .tobytes()
    )
    if header != BUILD_ID_HEADER_VALUE:
        raise Exception(
            f"Invalid build ID binary descriptor header at address "
            f"0x{symbol_address:08x}: {header.hex()}"
        )

    build_id_address = symbol_address + BUILD_ID_HEADER_SIZE

    print(
        f"Patching build ID '{build_id.hex()}' into HEX file '{hex_filepath}' at address: "
        f"0x{build_id_address:08x}"
    )

    for i, byte_value in enumerate(build_id):
        intel_hex[build_id_address + i] = byte_value

    intel_hex.write_hex_file(hex_filepath)


def patch_build_id_bin(
    bin_filepath: str,
    elffile: ELFFile,
    bindesc_build_id_symbol: Symbol,
    build_id: bytes,
):
    # The content of the binary file starts at the base address of the ELF file
    base_address = find_base_address(elffile)

    with open(bin_filepath, "r+b") as bin_file:
        symbol_address = bindesc_build_id_symbol["st_value"]
        symbol_file_offset = symbol_address - base_address

        bin_file.seek(symbol_file_offset)
        header = bin_file.read(BUILD_ID_HEADER_SIZE)
        if header != BUILD_ID_HEADER_VALUE:
            raise Exception(
                f"Invalid build ID binary descriptor header at offset "
                f"0x{symbol_file_offset:08x}: {header.hex()}"
            )

        build_id_file_offset = symbol_file_offset + BUILD_ID_HEADER_SIZE

        print(
            f"Patching build ID '{build_id.hex()}' into BIN file '{bin_filepath}' at offset: "
            f"0x{build_id_file_offset:08x}"
        )

        bin_file.seek(build_id_file_offset)
        bin_file.write(build_id)


def find_base_address(elffile: ELFFile) -> int:
    base_address = None

    # Find the lowest base address of a loadable segment
    for segment in elffile.iter_segments():
        if segment["p_type"] == "PT_LOAD":
            vaddr = segment["p_vaddr"]
            if base_address is None or vaddr < base_address:
                base_address = vaddr

    if base_address is None:
        raise Exception("No loadable segments found in ELF file")

    return base_address


def main():
    if len(sys.argv) < 2:
        print("Usage: patch_build_id.py <elf_file> [<other_file>...]")
        print("Recognized file extensions: .bin, .elf, .hex")
        sys.exit(1)

    elf_filepath = sys.argv[1]
    other_filepaths = sys.argv[2:]

    generate_and_patch_build_id(elf_filepath, other_filepaths)


if __name__ == "__main__":
    main()
