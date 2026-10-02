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
