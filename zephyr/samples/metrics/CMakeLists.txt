# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
endif()


find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(metrics)

if (CONFIG_SPOTFLOW_USE_WIFI)
  add_subdirectory(../wifi-common ${CMAKE_CURRENT_BINARY_DIR}/wifi-common)
endif ()

target_sources(app PRIVATE
        src/main.c
        src/metrics_collector.c
)
