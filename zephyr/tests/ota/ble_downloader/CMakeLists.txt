cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble_downloader)

target_include_directories(app PRIVATE ../../../include ../../../src)
target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_OTA=1
        CONFIG_SPOTFLOW_OTA_LOG_LEVEL=3
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE=64
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_INITIAL_DELAY_MS=1
        CONFIG_SPOTFLOW_OTA_DOWNLOAD_RETRY_MAX_DELAY_MS=1
        CONFIG_SPOTFLOW_OTA_BLE_REQUEST_TIMEOUT_MS=3000
        CONFIG_BT_L2CAP_TX_MTU=23
)
target_sources(app PRIVATE
        src/protocol.c
        ../../../src/ota/protocol/spotflow_ota_ble_protocol.c
)
