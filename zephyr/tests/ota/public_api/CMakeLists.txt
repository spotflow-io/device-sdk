# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ota_public_api)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../include
        ${CMAKE_CURRENT_LIST_DIR}/../../../src
        ${CMAKE_CURRENT_LIST_DIR}/../common
)

target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_OTA=1
        CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS=4
)

target_sources(app PRIVATE
        src/main.c
        ../common/spotflow_ota_test_fakes.c
        ../common/spotflow_ota_test_settings.c
        ../../../src/ota/spotflow_ota.c
        ../../../src/ota/protocol/spotflow_ota_cbor.c
        ../../../src/ota/firmware/spotflow_ota_fw_custom.c
        ../../../src/ota/core/spotflow_ota_log.c
        ../../../src/ota/protocol/spotflow_ota_net.c
        ../../../src/ota/persistence/spotflow_ota_persistence.c
        ../../../src/ota/persistence/spotflow_ota_records_cbor.c
        ../../../src/ota/core/spotflow_ota_state.c
        ../../../src/ota/core/spotflow_ota_worker.c
)
