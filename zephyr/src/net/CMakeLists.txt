zephyr_library_sources(
        spotflow_processor.c
        spotflow_connection_helper.c
        spotflow_device_id.c
        spotflow_mqtt.c
        spotflow_session_metadata.c
        spotflow_tls.c
)

zephyr_library_link_libraries(mbedTLS)

if (NOT CONFIG_SPOTFLOW_DEVICE_ID)
    message(FATAL_ERROR
        "CONFIG_SPOTFLOW_DEVICE_ID must be specified if CONFIG_SPOTFLOW is enabled")
endif ()
if (NOT CONFIG_SPOTFLOW_INGEST_KEY)
    message(FATAL_ERROR
        "CONFIG_SPOTFLOW_INGEST_KEY must be specified if CONFIG_SPOTFLOW is enabled")
endif()
