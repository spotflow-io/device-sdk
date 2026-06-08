# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ota_fw_main)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../include
        ${CMAKE_CURRENT_LIST_DIR}/../../../src
        ${CMAKE_CURRENT_LIST_DIR}/../common
        ${CMAKE_CURRENT_LIST_DIR}/../downloader
)

target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW=1
        CONFIG_SPOTFLOW_GENERATE_BUILD_ID=1
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_OTA=1
        CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS=4
        CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE=1
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_DELAY_MS=1
        CONFIG_SPOTFLOW_OTA_HTTP_TIMEOUT_MS=1000
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE=1024
)

target_sources(app PRIVATE
        src/main.c
        ../common/spotflow_ota_platform_fake.c
        ../common/spotflow_ota_test_settings.c
        ../downloader/spotflow_ota_downloader_transport_fake.c
        ../../../src/ota/spotflow_ota_downloader.c
        ../../../src/ota/spotflow_ota_fw_custom.c
        ../../../src/ota/spotflow_ota_fw_main.c
        ../../../src/ota/spotflow_ota_identity.c
        ../../../src/ota/spotflow_ota_persistence.c
        ../../../src/ota/spotflow_ota_records_cbor.c
        ../../../src/ota/spotflow_ota_state.c
        ../../../src/ota/spotflow_ota_url.c
        ../../../src/spotflow_build_id.c
        ${ZEPHYR_BASE}/subsys/net/lib/http/http_parser_url.c
)
