# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_metrics_unit_test)

set(SPOTFLOW_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# Test sources
target_sources(app PRIVATE
    src/main.c
    src/test_aggregator.c
    src/test_timeseries.c
    src/mocks.c
)

# Compile metrics module sources directly (bypassing full SDK dependencies)
target_sources(app PRIVATE
    ${SPOTFLOW_MODULE_DIR}/src/metrics/spotflow_metrics_aggregator.c
    ${SPOTFLOW_MODULE_DIR}/src/metrics/spotflow_metrics_backend.c
)

# Include paths
target_include_directories(app PRIVATE
    ${SPOTFLOW_MODULE_DIR}/src
    ${SPOTFLOW_MODULE_DIR}/src/metrics
)

# Define Kconfig symbols that the metrics module expects
target_compile_definitions(app PRIVATE
    CONFIG_SPOTFLOW_METRICS=1
    CONFIG_SPOTFLOW_METRICS_MAX_LABELS_PER_METRIC=4
    CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED=32
    CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE=16
    CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE=512
    CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL=3
    CONFIG_SPOTFLOW_METRICS_HEARTBEAT=0
)
