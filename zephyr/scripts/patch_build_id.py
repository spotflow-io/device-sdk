#!/usr/bin/env python3

import sys
import os
import hashlib
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import Section, Symbol
from elftools.elf.constants import SH_FLAGS
from intelhex import IntelHex


SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME = "bindesc_entry_spotflow_build_id"

# Binary descriptor ID is 0x5f0, type is bytes, and length is 20
BUILD_ID_HEADER_VALUE = b"\xf0\x25\x14\x00"

BUILD_ID_HEADER_SIZE = len(BUILD_ID_HEADER_VALUE)
BUILD_ID_VALUE_SIZE = 20

# Segment flags
PF_X=(1 << 0)	#/* Segment is executable */
PF_W=(1 << 1)	#/* Segment is writable */
PF_R=(1 << 2)	#/* Segment is readable */


def generate_and_patch_build_id(elf_filepath: str, other_filepaths: list[str]):
    with open(elf_filepath, "rb+") as elf_stream:
        elffile = ELFFile(elf_stream)

        bindesc_symbol_vaddr = find_bindesc_build_id_symbol_vaddr(elffile)
        bindesc_symbol_paddr = convert_vaddr_to_paddr(elffile, bindesc_symbol_vaddr)

        build_id = generate_build_id(elffile, bindesc_symbol_vaddr)

        patch_build_id_parsed_elf(elffile, elf_filepath, bindesc_symbol_vaddr, build_id)

        for filepath in other_filepaths:
            if not os.path.exists(filepath):
                # It is easier to check it here than in CMake
                continue

            if (
                filepath.endswith(".elf")
                or filepath.endswith(".exe")
                or filepath.endswith(".strip")
            ):
                patch_build_id_elf(filepath, bindesc_symbol_vaddr, build_id)
            elif filepath.endswith(".hex"):
                patch_build_id_hex(filepath, bindesc_symbol_paddr, build_id)
            elif filepath.endswith(".bin"):
                patch_build_id_bin(filepath, elffile, bindesc_symbol_paddr, build_id)
            else:
                raise Exception(f"Unsupported file type: {filepath}")


def find_bindesc_build_id_symbol_vaddr(elffile: ELFFile) -> int:
    symbol = None

    symbol_table = elffile.get_section_by_name(".symtab")
    if symbol_table is None:
        symbol_table = elffile.get_section_by_name(".dynsym")

    if symbol_table is not None:
        symbols = symbol_table.get_symbol_by_name(SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME)

    if symbols is None:
        raise Exception(f"Symbol '{SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME}' not found")

    if len(symbols) > 1:
        raise Exception(f"Symbol '{SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME}' found multiple times")

    symbol = symbols[0]
    assert symbol["st_size"] == BUILD_ID_HEADER_SIZE + BUILD_ID_VALUE_SIZE

    return symbol["st_value"]


def convert_vaddr_to_paddr(elffile: ELFFile, vaddr: int) -> int:
    for segment in elffile.iter_segments():
        if segment["p_type"] == "PT_LOAD":
            seg_vaddr = segment["p_vaddr"]
            seg_paddr = segment["p_paddr"]
            seg_size = segment["p_memsz"]

            if seg_vaddr <= vaddr < seg_vaddr + seg_size:
                offset = vaddr - seg_vaddr
                return seg_paddr + offset

    raise Exception(f"Virtual address 0x{vaddr:08x} not found in any segment of ELF file")


def generate_build_id(elffile: ELFFile, bindesc_symbol_vaddr: int):
    hash_builder = hashlib.sha1()

    for section in elffile.iter_sections():
        # Only include sections that will be loaded to the device memory
        if section["sh_flags"] & SH_FLAGS.SHF_ALLOC == 0:
            continue

        data = section.data()
        section_start_vaddr = section["sh_addr"]
        section_end_vaddr = section_start_vaddr + section["sh_size"]

        # The section address is included in the hash because loading the same data into a
        # different memory address would yield a different memory content
        hash_builder.update(section_start_vaddr.to_bytes(8, byteorder="little"))

        if section_start_vaddr <= bindesc_symbol_vaddr < section_end_vaddr:
            symbol_section_offset = bindesc_symbol_vaddr - section_start_vaddr
            build_id_section_start_offset = symbol_section_offset + BUILD_ID_HEADER_SIZE
            build_id_section_end_offset = build_id_section_start_offset + BUILD_ID_VALUE_SIZE

            # The build ID itself cannot take part in the hash
            hash_builder.update(data[:build_id_section_start_offset])
            hash_builder.update(data[build_id_section_end_offset:])
        else:
            hash_builder.update(data)

    build_id = hash_builder.digest()
    assert len(build_id) == BUILD_ID_VALUE_SIZE

    return build_id


def patch_build_id_elf(elf_filepath: str, bindesc_symbol_vaddr: int, build_id: bytes):
    with open(elf_filepath, "rb+") as elf_stream:
        elffile = ELFFile(elf_stream)
        patch_build_id_parsed_elf(elffile, elf_filepath, bindesc_symbol_vaddr, build_id)


def patch_build_id_parsed_elf(
    elffile: ELFFile,
    elf_filepath: str,
    bindesc_symbol_vaddr: int,
    build_id: bytes,
):
    section = find_section_containing_vaddr(elffile, bindesc_symbol_vaddr)

    section_start_vaddr = section["sh_addr"]
    section_file_offset = section["sh_offset"]

    symbol_section_offset = bindesc_symbol_vaddr - section_start_vaddr
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


def find_section_containing_vaddr(elffile: ELFFile, bindesc_symbol_vaddr: int) -> Section:
    for section in elffile.iter_sections():
        section_start_vaddr = section["sh_addr"]
        section_end_vaddr = section_start_vaddr + section["sh_size"]

        if section_start_vaddr <= bindesc_symbol_vaddr < section_end_vaddr:
            return section

    raise Exception(
        f"Symbol with virtual address 0x{bindesc_symbol_vaddr:08x} not found "
        "in any section of ELF file"
    )


def patch_build_id_hex(
    hex_filepath: str,
    bindesc_symbol_paddr: int,
    build_id: bytes,
):
    intel_hex = IntelHex(hex_filepath)

    header = (
        intel_hex[bindesc_symbol_paddr : bindesc_symbol_paddr + BUILD_ID_HEADER_SIZE]
        .tobinarray()
        .tobytes()
    )
    if header != BUILD_ID_HEADER_VALUE:
        raise Exception(
            f"Invalid build ID binary descriptor header at address "
            f"0x{bindesc_symbol_paddr:08x}: {header.hex()}"
        )

    build_id_address = bindesc_symbol_paddr + BUILD_ID_HEADER_SIZE

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
    bindesc_symbol_paddr: int,
    build_id: bytes,
):
    # The content of the binary file starts at the base address of the ELF file
    base_address = find_base_address(elffile)

    with open(bin_filepath, "r+b") as bin_file:
        symbol_file_offset = bindesc_symbol_paddr - base_address

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
            paddr = segment["p_paddr"]
            writable = segment["p_flags"] & PF_W == 0
            # skip segments that have no file size -> they exist only in RAM memory .bss/.noinit
            # skip segments that are writable .data, .ramfunc overlay
            if segment["p_filesz"] == 0 or writable:
                continue
            if base_address is None or paddr < base_address:
                base_address = paddr

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
