cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_persistent_buffer_tests)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../include
        ${CMAKE_CURRENT_LIST_DIR}/../../src
)

target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE=256
        CONFIG_SPOTFLOW_LOG_BACKEND=1
        CONFIG_SPOTFLOW_METRICS=1
        CONFIG_SPOTFLOW_METRICS_HEARTBEAT=1
        CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC=4
        CONFIG_SPOTFLOW_METRICS_MAX_NAME_LENGTH=64
)

target_sources(app PRIVATE
        src/main.c
        ../../src/persistent_buffer/spotflow_persistent_buffer.c
)
