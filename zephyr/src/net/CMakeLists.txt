zephyr_library_sources_ifdef(CONFIG_LOG_BACKEND_SPOTFLOW
        spotflow_processor.c
        spotflow_connection_helper.c
        spotflow_device_id.c
        spotflow_mqtt.c
        spotflow_tls.c
)

zephyr_library_link_libraries(mbedTLS)

if (CONFIG_LOG_BACKEND_SPOTFLOW)
    if (NOT CONFIG_SPOTFLOW_INGEST_KEY)
        message(FATAL_ERROR  "CONFIG_SPOTFLOW_INGEST_KEY
                              must be specified if CONFIG_LOG_BACKEND_SPOTFLOW enabled")
    endif()
endif()
