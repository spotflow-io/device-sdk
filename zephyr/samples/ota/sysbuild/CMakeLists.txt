# SPDX-License-Identifier: Apache-2.0

# Register the board-specific sysbuild fragment before Sysbuild parses Kconfig.
if(BOARD MATCHES "^cy8cproto_062_4343w")
	list(APPEND SB_EXTRA_CONF_FILE
		"${CMAKE_CURRENT_LIST_DIR}/cy8cproto_062_4343w.conf"
	)
endif()

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sysbuild LANGUAGES)
