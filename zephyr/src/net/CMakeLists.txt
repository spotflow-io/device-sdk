zephyr_library_sources(
        spotflow_processor.c
        spotflow_device_id.c
        spotflow_session_metadata.c
        spotflow_transport.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_TRANSPORT_MQTT
        spotflow_ingest_key.c
        transport/mqtt/spotflow_connection_helper.c
        transport/mqtt/spotflow_mqtt.c
        transport/mqtt/spotflow_mqtt_session.c
        transport/mqtt/spotflow_tls.c
)

if(CONFIG_SPOTFLOW_TRANSPORT_MQTT)
    zephyr_library_link_libraries(mbedTLS)
endif()

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_TRANSPORT_BLE
        transport/ble/spotflow_ble_transport.c
        transport/ble/spotflow_ble_gatt.c
        transport/ble/spotflow_ble_framing.c
)
