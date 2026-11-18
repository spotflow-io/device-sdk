cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble_config_tests)

target_include_directories(app PRIVATE ../../src ../../include)
target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_BLE_RX_THREAD_STACK_SIZE=2048
        CONFIG_BT_L2CAP_TX_MTU=23
)
target_sources(app PRIVATE
        src/main.c
        ../../src/config/spotflow_config.c
        ../../src/config/spotflow_config_net.c
        ../../src/config/spotflow_config_cbor.c
        ../../src/logging/spotflow_log_level.c
        ../../src/net/transport/ble/spotflow_ble_rx.c
)
