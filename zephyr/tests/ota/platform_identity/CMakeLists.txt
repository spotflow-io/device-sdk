# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ota_platform_identity)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../../include
        ${CMAKE_CURRENT_LIST_DIR}/../../../src
        ${CMAKE_CURRENT_LIST_DIR}/../common
)

target_compile_definitions(app PRIVATE
        CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL=3
        CONFIG_SPOTFLOW_OTA=1
)

target_sources(app PRIVATE
        src/main.c
        ../common/spotflow_ota_platform_fake.c
        ../../../src/ota/spotflow_ota_identity.c
        ../../../src/spotflow_build_id.c
)
