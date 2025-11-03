# patch_build_id.cmake
message(STATUS "Patching build ID...")
file(READ "${PROJECT_ELF}" elf_data HEX)
# You can use objcopy or Python here to write the ID
# Example: write into section or symbol with esptool or objcopy