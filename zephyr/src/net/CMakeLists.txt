zephyr_library_sources_ifdef(CONFIG_SPOTFLOW
        spotflow_processor.c
        spotflow_device_id.c
        spotflow_session_metadata.c
        spotflow_transport.c
)

if(CONFIG_SPOTFLOW_TRANSPORT_MQTT)
    zephyr_library_sources(
        transport/mqtt/spotflow_connection_helper.c
        transport/mqtt/spotflow_mqtt.c
        transport/mqtt/spotflow_mqtt_session.c
        transport/mqtt/spotflow_tls.c
    )

    zephyr_library_link_libraries(mbedTLS)
endif()

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_TRANSPORT_BLE
        transport/ble/spotflow_ble_transport.c
        transport/ble/spotflow_ble_gatt.c
        transport/ble/spotflow_ble_framing.c
)

if(CONFIG_SPOTFLOW_TRANSPORT_MQTT)
    if (NOT CONFIG_SPOTFLOW_INGEST_KEY)
        message(FATAL_ERROR "CONFIG_SPOTFLOW_INGEST_KEY must be specified when CONFIG_SPOTFLOW_TRANSPORT_MQTT is enabled")
    endif()
endif()
