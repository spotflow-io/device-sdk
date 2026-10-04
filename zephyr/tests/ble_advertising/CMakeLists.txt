cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble_advertising_tests)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../src
)

target_compile_definitions(app PRIVATE
        CONFIG_BT_L2CAP_TX_MTU=23
)

# The Spotflow BLE Kconfig options depend on CONFIG_SPOTFLOW_TRANSPORT_BLE, which this
# unit test doesn't enable, so the backoff schedule is configured here directly.
if(NOT SPOTFLOW_TEST_NO_ADV_BACKOFF)
  target_compile_definitions(app PRIVATE
          CONFIG_SPOTFLOW_BLE_ADV_BACKOFF=1
          CONFIG_SPOTFLOW_BLE_ADV_FAST_DURATION=20
          CONFIG_SPOTFLOW_BLE_ADV_SLOW_INTERVAL_MS=1000
          CONFIG_SPOTFLOW_BLE_ADV_SLOW_DURATION=120
          CONFIG_SPOTFLOW_BLE_ADV_IDLE_INTERVAL_MS=10240
  )
endif()

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE ../../src/net/transport/ble/spotflow_ble_advertising.c)
