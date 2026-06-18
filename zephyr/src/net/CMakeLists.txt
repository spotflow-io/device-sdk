zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_LOG_BACKEND
        spotflow_processor.c
        spotflow_device_id.c
        spotflow_session_metadata.c
        spotflow_transport.c
)

if(CONFIG_SPOTFLOW_LOG_BACKEND AND CONFIG_SPOTFLOW_TRANSPORT_MQTT)
    zephyr_library_sources(
        spotflow_connection_helper.c
        spotflow_mqtt.c
        spotflow_tls.c
    )

    zephyr_library_link_libraries(mbedTLS)
endif()

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_TRANSPORT_BLE
        spotflow_ble_transport.c
        spotflow_ble_gatt.c
        spotflow_ble_framing.c
)

if (CONFIG_SPOTFLOW_LOG_BACKEND AND CONFIG_SPOTFLOW_TRANSPORT_MQTT)
    if (NOT CONFIG_SPOTFLOW_INGEST_KEY)
        message(FATAL_ERROR  "CONFIG_SPOTFLOW_INGEST_KEY
                              must be specified if CONFIG_SPOTFLOW_LOG_BACKEND enabled")
    endif()
endif()
