# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ota_cbor)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../src
)

target_sources(app PRIVATE
        src/main.c
        ../../../src/ota/spotflow_ota_cbor.c
)
