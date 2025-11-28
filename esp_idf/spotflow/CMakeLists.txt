cmake_minimum_required(VERSION 3.16)
# Default srcs that are always buillt
file(GLOB SRCS
	"src/net/*.c"
	"src/logging/*.c"
	"src/configs/*.c"
	"spotflow.c"
)

# Pric_Requires that are always built
set(PRIV_REQUIRES
	mqtt
	esp_partition
	nvs_flash
	esp_netif
	app_update
	espcoredump
)

# ---- Optional build ID source ----
if(CONFIG_SPOTFLOW_USE_BUILD_ID)
	list(APPEND SRCS "src/buildid/spotflow_build_id.c")
	list(APPEND PRIV_REQUIRES esp_app_format)
endif()

# ---- Optional coredump sources ----
if(CONFIG_ESP_COREDUMP_ENABLE)
	file(GLOB CORE_DUMP_SRCS "src/coredump/*.c")
	list(APPEND SRCS ${CORE_DUMP_SRCS})
endif()

idf_component_register( SRCS ${SRCS}
						PRIV_REQUIRES ${PRIV_REQUIRES}
						INCLUDE_DIRS "include"
						REQUIRES cbor
						EMBED_TXTFILES x1_root.pem)

# ---- Enforce MQTT buffer size sanity check ----
if (CONFIG_MQTT_BUFFER_SIZE LESS CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)
	message(WARNING
		"CONFIG_MQTT_BUFFER_SIZE (${CONFIG_MQTT_BUFFER_SIZE}) "
		"is smaller than CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN (${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN}). "
		"Forcing MQTT_BUFFER_SIZE to match Spotflow requirements.")
	# Force the MQTT buffer size to be large enough
	set(CONFIG_MQTT_BUFFER_SIZE ${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN} CACHE INT "MQTT buffer size" FORCE)
endif()