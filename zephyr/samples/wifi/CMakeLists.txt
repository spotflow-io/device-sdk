# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# Debug: show exactly what search paths Zephyr will use

# todo to isolate private data. Should be removed in public repo
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
endif()


find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

target_sources(app PRIVATE
        src/main.c
        src/wifi.c
)

# todo just for better ux, remove
#add_subdirectory(../../../ports/zephyr ${CMAKE_CURRENT_BINARY_DIR}/dep_build)
