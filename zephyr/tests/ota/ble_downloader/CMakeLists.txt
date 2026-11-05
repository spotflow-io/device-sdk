cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble_downloader)

zephyr_linker_sources(DATA_SECTIONS ../../../src/ota/downloader/spotflow_ota_downloader.ld)
zephyr_iterable_section(NAME spotflow_downloader GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT})
target_include_directories(app PRIVATE ../../../include ../../../src)
target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_OTA=1
        CONFIG_SPOTFLOW_OTA_LOG_LEVEL=3
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE=64
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_INITIAL_DELAY_MS=1
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_MAX_DELAY_MS=1
        CONFIG_SPOTFLOW_OTA_HTTP_TIMEOUT_MS=1000
        CONFIG_BT_L2CAP_TX_MTU=23
)
target_sources(app PRIVATE
        src/main.c
        ../../../src/ota/downloader/spotflow_ota_downloader.c
        ../../../src/ota/downloader/spotflow_ota_downloader_transport_ble.c
        ../../../src/ota/downloader/spotflow_ota_downloader_transport_errors.c
        ../../../src/ota/downloader/spotflow_ota_url.c
        ${ZEPHYR_BASE}/subsys/net/lib/http/http_parser_url.c
)
