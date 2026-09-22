cmake_minimum_required(VERSION 3.20.0)

set(CONF_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ble/prj.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/../ble/boards/lp_em_cc2340r5.conf
    ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble_bmi270)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/drivers/sensor/bosch/bmi270)
