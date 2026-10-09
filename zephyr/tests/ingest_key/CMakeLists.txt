cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ingest_key_tests)

set(SPOTFLOW_TEST_SCENARIO "dynamic" CACHE STRING "Ingest key test scenario")
set(SPOTFLOW_TEST_CONFIG_KEY "" CACHE STRING "Build-time ingest key for tests")
string(TOUPPER "${SPOTFLOW_TEST_SCENARIO}" test_scenario)

target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../include
        ${CMAKE_CURRENT_LIST_DIR}/../../src
)

target_compile_definitions(app PRIVATE TEST_SCENARIO_${test_scenario}=1)
if(SPOTFLOW_TEST_SCENARIO MATCHES "^dynamic")
    target_compile_definitions(app PRIVATE CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC=1)
else()
    target_compile_definitions(app PRIVATE
            CONFIG_SPOTFLOW_INGEST_KEY="${SPOTFLOW_TEST_CONFIG_KEY}"
    )
endif()

target_sources(app PRIVATE
        src/main.c
        ../../src/net/spotflow_ingest_key.c
)
