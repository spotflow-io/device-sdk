"""Build-ID hashing rules for ELF-only log strings, without binary fixtures."""

import hashlib
from types import SimpleNamespace

import pytest
from elftools.elf.constants import SH_FLAGS

import patch_build_id


class ElfSection(dict):
    def __init__(self, name, address, data, allocated=False):
        super().__init__(sh_addr=address, sh_size=len(data),
            sh_flags=SH_FLAGS.SHF_ALLOC if allocated else 0)
        self.name = name
        self.contents = data

    def data(self):
        return self.contents


def build_id(*sections, descriptor_address=0x1000):
    elf = SimpleNamespace(iter_sections=lambda: iter(sections))
    return patch_build_id.generate_build_id(elf, descriptor_address)


def test_stripped_strings_affect_build_id():
    code = ElfSection(".text", 0x2000, b"same firmware", allocated=True)
    first = ElfSection("log_strings", 0xF0000000, b"message A\0")
    second = ElfSection("log_strings", 0xF0000000, b"message B\0")
    assert build_id(code, first) != build_id(code, second)
    assert build_id(code, first) != build_id(code)


def test_log_string_addresses_affect_build_id():
    first = ElfSection("log_strings", 0xF0000000, b"message\0")
    relocated = ElfSection("log_strings", 0xF0000100, b"message\0")
    assert build_id(first) != build_id(relocated)


@pytest.mark.parametrize("allocated", [False, True])
def test_log_strings_are_hashed_exactly_once(allocated):
    section = ElfSection("log_strings", 0xF0000000, b"message\0", allocated)
    expected = hashlib.sha1((0xF0000000).to_bytes(8, "little") + b"message\0").digest()
    assert build_id(section) == expected


def test_unrelated_nonallocated_sections_do_not_affect_build_id():
    code = ElfSection(".text", 0x2000, b"same firmware", allocated=True)
    debug = ElfSection(".debug_info", 0, b"debug information")
    assert build_id(code, debug) == build_id(code)


def test_build_id_value_is_excluded_from_its_allocated_section():
    header = patch_build_id.BUILD_ID_HEADER_VALUE
    first = ElfSection(".rodata", 0x1000, header + b"A" * 20 + b"tail", allocated=True)
    second = ElfSection(".rodata", 0x1000, header + b"B" * 20 + b"tail", allocated=True)
    assert build_id(first) == build_id(second)


def test_nonallocated_strings_overlapping_descriptor_address_are_hashed_in_full():
    # Non-allocated section addresses need not be disjoint from device memory.
    first = ElfSection("log_strings", 0x1000, b"head" + b"A" * 20 + b"tail")
    second = ElfSection("log_strings", 0x1000, b"head" + b"B" * 20 + b"tail")
    assert build_id(first) != build_id(second)
