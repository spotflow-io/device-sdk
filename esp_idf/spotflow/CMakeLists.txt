idf_component_register(SRCS "srcs/net/spotflow_mqtt.c" "srcs/logging/spotflow_log_backend.c" "srcs/logging/spotflow_log_cbor.c" "srcs/logging/spotflow_log_json.c" "spotflow.c"
                    PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update
                    INCLUDE_DIRS "include"
                    REQUIRES cbor
                    EMBED_TXTFILES mqtt_spotflow_io.pem)
