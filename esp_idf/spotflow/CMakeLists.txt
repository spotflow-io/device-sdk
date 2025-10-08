idf_component_register(SRCS "src/net/spotflow_mqtt.c" "src/logging/spotflow_log_backend.c" "src/logging/spotflow_log_cbor.c" "src/logging/spotflow_log_json.c" "spotflow.c"
                    PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update
                    INCLUDE_DIRS "include"
                    REQUIRES cbor
                    EMBED_TXTFILES x1_root.pem)
