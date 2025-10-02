idf_component_register(SRCS "spotflow.c" "spotflow_mqtt.c"
                    PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update
                    INCLUDE_DIRS "include"
                    REQUIRES cbor
                    EMBED_TXTFILES mqtt_spotflow_io.pem)
