# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ota_facade)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../include
        ${CMAKE_CURRENT_LIST_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_OTA=1
        CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS=4
)

target_sources(app PRIVATE
        src/main.c
        ../../../src/net/spotflow_session_metadata.c
        ../../../src/ota/spotflow_ota.c
        ../../../src/ota/spotflow_ota_persistence.c
        ../../../src/ota/spotflow_ota_records_cbor.c
        ../../../src/ota/spotflow_ota_state.c
)
