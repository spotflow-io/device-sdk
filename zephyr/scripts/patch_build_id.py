#!/usr/bin/env python3

import sys
import os
import hashlib
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import Symbol
from elftools.elf.segments import Segment
from elftools.elf.constants import SH_FLAGS
from intelhex import IntelHex


SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME = "bindesc_entry_spotflow_build_id"

BUILD_ID_HEADER_SIZE = 4
BUILD_ID_VALUE_SIZE = 20


def generate_and_patch_build_id(elf_filepath, hex_filepath, bin_filepath=None):
    with open(elf_filepath, 'rb+') as file_stream:
        elffile = ELFFile(file_stream)

        bindesc_build_id_symbol = find_bindesc_build_id_symbol(elffile)

        build_id = generate_build_id(elffile, bindesc_build_id_symbol)

        patch_build_id_elf(elffile, bindesc_build_id_symbol, build_id)
        
        if hex_filepath:
            patch_build_id_hex(hex_filepath, bindesc_build_id_symbol, build_id)
            
        if bin_filepath:
            patch_build_id_bin(bin_filepath, elffile, bindesc_build_id_symbol, build_id)


def find_bindesc_build_id_symbol(elffile: ELFFile) -> Symbol:
    symbol = None

    symbol_table = elffile.get_section_by_name('.symtab')
    if symbol_table is None:
        symbol_table = elffile.get_section_by_name('.dynsym')
    
    if symbol_table is not None:
        symbol = symbol_table.get_symbol_by_name(SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME)
    
    if symbol is None:
        raise Exception(f"Symbol '{SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME}' not found")
    
    if len(symbol) > 1:
        raise Exception(f"Symbol '{SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME}' found multiple times")

    # TODO: Consider also checking the right format of the binary descriptor (tag and length, see
    #       https://docs.zephyrproject.org/latest/services/binary_descriptors/index.html)
    return symbol[0]


def generate_build_id(elffile: ELFFile, bindesc_build_id_symbol: Symbol):
    hash_builder = hashlib.sha1()

    for section in elffile.iter_sections():
        print(section.name, "...", sep="", end="")

        if section["sh_flags"] & SH_FLAGS.SHF_ALLOC == 0:
            print("not allocated, skipping")
            continue

        data = section.data()
        section_start = section["sh_addr"]
        section_end = section_start + section["sh_size"]
        symbol_start = bindesc_build_id_symbol["st_value"]

        if section_start <= symbol_start < section_end:
            # TODO: Skip just the content of the symbol (those 20 bytes), not the whole section
            print("section with build id symbol, skipping hash update")
        else:
            hash_builder.update(data)
            print("updated hash")

    build_id = hash_builder.digest()
    assert len(build_id) == BUILD_ID_VALUE_SIZE
    assert bindesc_build_id_symbol["st_size"] == BUILD_ID_HEADER_SIZE + BUILD_ID_VALUE_SIZE

    return build_id


def patch_build_id_elf(elffile: ELFFile, bindesc_build_id_symbol: Symbol, build_id: bytes):
    for section in elffile.iter_sections():
        section_file_offset = section["sh_offset"]
        section_rom_start = section["sh_addr"]
        section_rom_end = section_rom_start + section["sh_size"]
        symbol_rom_start = bindesc_build_id_symbol["st_value"]

        if section_rom_start <= symbol_rom_start < section_rom_end:
            build_id_file_offset = section_file_offset + (symbol_rom_start - section_rom_start) + BUILD_ID_HEADER_SIZE

            print(f"Patching build id '{build_id.hex()}' into ELF file at offset: 0x{build_id_file_offset:08x}")
            elffile.stream.seek(build_id_file_offset)
            elffile.stream.write(build_id)

            break


def patch_build_id_hex(hex_filepath: str, bindesc_build_id_symbol: Symbol, build_id: bytes):
    target_address = bindesc_build_id_symbol["st_value"] + BUILD_ID_HEADER_SIZE
    
    print(f"Patching build id '{build_id.hex()}' into HEX file at address: 0x{target_address:08x}")
    
    intel_hex = IntelHex(hex_filepath)
    
    for i, byte_value in enumerate(build_id):
        intel_hex[target_address + i] = byte_value
    
    intel_hex.write_hex_file(hex_filepath)


def patch_build_id_bin(bin_filepath: str, elffile: ELFFile, bindesc_build_id_symbol: Symbol, build_id: bytes):
    # The content of the binary file starts at the base address of the ELF file
    base_address = find_base_address(elffile)

    symbol_address = bindesc_build_id_symbol["st_value"]

    build_id_file_offset = symbol_address - base_address + BUILD_ID_HEADER_SIZE

    print(f"Patching build id '{build_id.hex()}' into BIN file at offset: 0x{build_id_file_offset:08x}")
    
    with open(bin_filepath, 'r+b') as bin_file:
        bin_file.seek(build_id_file_offset)
        bin_file.write(build_id)


def find_base_address(elffile: ELFFile) -> int:
    base_address = None
    
    # Find the lowest base address of a loadable segment
    for segment in elffile.iter_segments():
        if segment['p_type'] == 'PT_LOAD':
            vaddr = segment['p_vaddr']
            if base_address is None or vaddr < base_address:
                base_address = vaddr
    
    if base_address is None:
        raise Exception("No loadable segments found in ELF file")
    
    return base_address


def main():
    if len(sys.argv) < 4:
        print("Usage: patch_build_id.py <elf_file> <hex_file> <bin_file>")
        sys.exit(1)
    
    elf_filepath = sys.argv[1]
    hex_filepath = sys.argv[2]
    bin_filepath = sys.argv[3]
    
    if hex_filepath and not os.path.exists(hex_filepath):
        print(f"HEX file '{hex_filepath}' does not exist, skipping HEX patching")
        hex_filepath = None
    
    if bin_filepath and not os.path.exists(bin_filepath):
        print(f"BIN file '{bin_filepath}' does not exist, skipping BIN patching")
        bin_filepath = None
    
    generate_and_patch_build_id(elf_filepath, hex_filepath, bin_filepath)

if __name__ == "__main__":
    main()
