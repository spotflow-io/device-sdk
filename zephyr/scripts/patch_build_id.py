#!/usr/bin/env python3

import sys
import hashlib
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import Symbol
from elftools.elf.constants import SH_FLAGS


# TODO: Create an argument out of this (and maybe also from those below)
SPOTFLOW_BINDESC_BUILD_ID_SYMBOL_NAME = "bindesc_entry_spotflow_build_id"

BUILD_ID_HEADER_SIZE = 4
BUILD_ID_VALUE_SIZE = 20


def generate_build_id(filepath):
    hash_builder = hashlib.sha1()

    with open(filepath, 'rb+') as file_stream:
        elffile = ELFFile(file_stream)

        bindesc_build_id_symbol = find_bindesc_build_id_symbol(elffile)

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

    return hash_builder.digest()


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

    return symbol[0]


def main():
    build_id = generate_build_id(sys.argv[1])

    print(build_id)
    # TODO: Patch build id to the binary

if __name__ == "__main__":
    main()
