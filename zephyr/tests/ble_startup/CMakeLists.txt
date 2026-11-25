cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble_startup_tests)

target_include_directories(app PRIVATE ../../src ../../include)
zephyr_linker_sources(SECTIONS sections-rom.ld)
target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_LOG_BACKEND=1
        CONFIG_SPOTFLOW_OTA=1
        CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS=4
        CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE=32
        CONFIG_BT_L2CAP_TX_MTU=23
        CONFIG_BT_DEVICE_NAME="spotflow-test"
)
target_sources(app PRIVATE
        src/main.c
        ../../src/net/transport/ble/spotflow_ble_transport.c
        ../../src/net/transport/ble/spotflow_ble_gatt.c
)

if(SPOTFLOW_TEST_BLE_INGEST_KEY)
    target_compile_definitions(app PRIVATE
            CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY=1
            CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC=1
    )
    if(NOT SPOTFLOW_TEST_BLE_UNENCRYPTED)
        target_compile_definitions(app PRIVATE CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION=1)
    endif()
endif()
