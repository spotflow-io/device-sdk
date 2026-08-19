cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ota_mqtt_session)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_OTA=1
)

target_sources(app PRIVATE
        src/main.c
        ../../../src/net/transport/mqtt/spotflow_mqtt_session.c
)
