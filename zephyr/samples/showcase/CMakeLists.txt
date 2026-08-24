cmake_minimum_required(VERSION 3.20.0)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_showcase)

# Automatic main-firmware handling requires MCUboot via sysbuild.
test_sysbuild()

add_subdirectory(../net-common ${CMAKE_CURRENT_BINARY_DIR}/net-common)

target_sources(app PRIVATE
    src/button.c
    src/identity.c
    src/main.c
    src/ota.c
    src/telemetry.c
)
