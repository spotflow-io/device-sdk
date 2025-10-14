set(SRCS
    "src/net/spotflow_mqtt.c"
    "src/logging/spotflow_log_backend.c"
    "src/logging/spotflow_log_queue.c"
    "src/logging/spotflow_log_cbor.c"
    "spotflow.c"
)

idf_component_register( SRCS ${SRCS}   
                        PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update
                        INCLUDE_DIRS "include"
                        REQUIRES cbor rt
                        EMBED_TXTFILES x1_root.pem)
