zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_LOG_BACKEND
        spotflow_log_backend.c
        spotflow_log_cbor.c
        spotflow_log_message.c
        spotflow_log_level.c
        spotflow_log_processor.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_COMPACT_LOGS
        spotflow_compact_logs_profile.c
)

if(CONFIG_SPOTFLOW_COMPACT_LOGS)
    zephyr_linker_sources(SECTIONS spotflow_compact_logs_profile.ld)
endif()

if(CONFIG_SPOTFLOW_COMPACT_LOGS AND CONFIG_LOG_FMT_SECTION_STRIP AND
   KERNEL_VERSION_MAJOR EQUAL 3 AND KERNEL_VERSION_MINOR EQUAL 7)
    include(spotflow_log_source_compat.cmake)
endif()
