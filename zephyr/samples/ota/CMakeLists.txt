cmake_minimum_required(VERSION 3.20.0)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ota)

# This sample enables automatic main-firmware handling and requires MCUboot via sysbuild.
test_sysbuild()

add_subdirectory(../net-common ${CMAKE_CURRENT_BINARY_DIR}/net-common)

target_sources(app PRIVATE
    src/main.c
)

# PSoC6 RAM-load under MCUboot:
# - Omit the app CM0+ ROM blob so vectors follow ROM_START_OFFSET.
# - ROM_START_OFFSET is 0x2000 so load_addr+header matches the CM4 VTOR.
# - Re-sign with max-align=512 / erase-value 0 (imgtool CLI cannot).
if(BOARD MATCHES "^cy8cproto_062_4343w")
	set(snippet_base ${CMAKE_BINARY_DIR}/zephyr/include/generated)
	# Keep all ROM_START includes except the CM0+ sleep image reservation.
	file(WRITE ${snippet_base}/snippets-rom-start.ld
		"/* Sort key: \"!\" */#include \"../arch/common/rom_start_address.ld\"\n"
		"/* Sort key: \"$\" */#include \"../arch/common/fill_with_zeros.ld\"\n"
		"/* Sort key: \"0x0\" */#include \"../arch/common/rom_start_offset.ld\"\n"
		"/* Spotflow OTA: skip soc CM0+ blob; pad so vectors are at load+0x2000. */\n"
		"__cy_m0p_code_start = . ;\n"
		"__cy_m0p_code_end = . ;\n"
		". += 0x1e00;\n"
		". = ALIGN(4);\n"
		"/* Sort key: \"0x0vectors\" */#include \"../arch/arm/core/vector_table.ld\"\n"
		"/* Sort key: \"0x0vectors\" */#include \"zephyr/linker/irq-vector-table-section.ld\"\n"
		"/* Sort key: \"0x1vectors\" */#include \"../arch/arm/core/cortex_m/vector_table_pad.ld\"\n"
	)
	file(WRITE ${snippet_base}/snippets-ram-sections.ld
		"/* Spotflow OTA: no CM0+ RAM reservation under MCUboot RAM-load. */\n"
		"SECTION_PROLOGUE(.ram_m0p_image,(NOLOAD),)\n"
		"{\n"
		"} GROUP_DATA_LINK_IN(RAMABLE_REGION, RAMABLE_REGION)\n"
		"/* Sort key: \"default\" */#include \"../arch/common/ramfunc.ld\"\n"
	)

	set(psoc6_resign ${CMAKE_CURRENT_SOURCE_DIR}/scripts/psoc6_resign.py)
	set(psoc6_key ${ZEPHYR_MCUBOOT_MODULE_DIR}/root-rsa-2048.pem)
	if(NOT EXISTS "${psoc6_key}")
		set(psoc6_key ${ZEPHYR_BASE}/../bootloader/mcuboot/root-rsa-2048.pem)
	endif()
	# Run after the stock imgtool sign (also a zephyr_final POST_BUILD).
	add_custom_command(TARGET zephyr_final POST_BUILD
		COMMAND ${PYTHON_EXECUTABLE} ${psoc6_resign}
			--in-bin ${CMAKE_BINARY_DIR}/zephyr/zephyr.bin
			--out-bin ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.bin
			--out-hex ${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.hex
			--key ${psoc6_key}
			--header-size 0x2000
		COMMAND ${CMAKE_COMMAND} -E copy
			${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.bin
			${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.confirmed.bin
		COMMAND ${CMAKE_COMMAND} -E copy
			${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.hex
			${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.confirmed.hex
		COMMENT "Re-signing PSoC6 image for RAM_LOAD_REVERT (max-align 512)"
	)
endif()
