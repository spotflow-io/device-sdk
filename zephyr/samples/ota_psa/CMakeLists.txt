cmake_minimum_required(VERSION 3.20.0)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ota_psa)

if(NOT CONFIG_BOARD_LP_EM_CC35X1_CC3551E OR
    NOT CONFIG_CC35XXE_FLASH_PROFILE_IS25WJ032F OR
    NOT CONFIG_CC35XXE_GENERATE_FLASH_MAP_CONFIG)
    message(FATAL_ERROR
        "This sample requires lp_em_cc35x1/cc3551e and the generated is25wj032f OTA map")
endif()

# The overlay fixes physical addresses. A profile name alone cannot validate a
# custom map or discovery configuration against those addresses.
if(NOT "${CONFIG_CC35XXE_MEMORY_CONFIG_JSON}" STREQUAL "" OR
    NOT "${CONFIG_CC35XXE_FLASH_DISCOVERY_CONFIG_EXT_MEM}" STREQUAL "" OR
    NOT "${CONFIG_CC35XXE_FLASH_DISCOVERY_CONFIG_OTFDE}" STREQUAL "" OR
    NOT "${CONFIG_CC35XXE_FLASH_DISCOVERY_CONFIG_XSPI}" STREQUAL "")
    message(FATAL_ERROR "Custom flash maps require a matching sample overlay")
endif()

add_subdirectory(../net-common ${CMAKE_CURRENT_BINARY_DIR}/net-common)
target_sources(app PRIVATE src/main.c)
