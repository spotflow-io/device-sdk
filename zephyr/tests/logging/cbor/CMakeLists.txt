cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_log_cbor_tests)
target_include_directories(app PRIVATE ../../../src)
target_compile_definitions(app PRIVATE
    CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL=0
    CONFIG_SPOTFLOW_LOG_BUFFER_SIZE=128
    CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN=512
    CONFIG_SPOTFLOW_LOG_INCLUDE_BODY_TEMPLATE=1
    $<$<BOOL:${CONFIG_TEST_COMPACT_LOGS}>:CONFIG_SPOTFLOW_COMPACT_LOGS=1>
)
target_sources(app PRIVATE src/main.c ../../../src/logging/spotflow_log_cbor.c
    ../../../src/logging/spotflow_log_message.c ../../../src/logging/spotflow_log_level.c)
