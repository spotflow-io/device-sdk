cmake_minimum_required(VERSION 3.16)
# Default srcs that are always buillt
file(GLOB SRCS
	"src/net/*.c"
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

# ---- Optional logging source ----
if(CONFIG_SPOTFLOW_LOG_BACKEND)
  file(GLOB LOGGING_SRCS "src/logging/*.c")
	list(APPEND SRCS ${LOGGING_SRCS})
  file(GLOB CONFIGS_SRCS "src/configs/*.c")
  list(APPEND SRCS ${CONFIGS_SRCS})
endif()

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


# Check if either dependency is configured
if(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN OR CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE)
    set(MIN_MQTT_TASK_REQUIRED_SIZE 0)

    # 1. Check CBOR requirement (2x the log length)
    if(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)
        math(EXPR MIN_MQTT_TASK_REQUIRED_SIZE "${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN} * 2")
        message(DEBUG "CBOR requirement: ${MIN_MQTT_TASK_REQUIRED_SIZE}")
    endif()

    # 2. Check Coredump requirement (chunk size + 64 overhead)
    if(CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE)
        math(EXPR CORE_MIN "${CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE} + 64")
        if(CORE_MIN GREATER MIN_MQTT_TASK_REQUIRED_SIZE)
            set(MIN_MQTT_TASK_REQUIRED_SIZE ${CORE_MIN})
            message(DEBUG "Coredump requirement: ${CORE_MIN} (new min: ${MIN_MQTT_TASK_REQUIRED_SIZE})")
        endif()
    endif()

    # 3. Only enforce if we have a minimum value
    if(MIN_MQTT_TASK_REQUIRED_SIZE GREATER 0)
        if(CONFIG_SPOTFLOW_MQTT_TASK_SIZE LESS MIN_MQTT_TASK_REQUIRED_SIZE)
            message(FATAL_ERROR
                "CONFIG_SPOTFLOW_MQTT_TASK_SIZE (${CONFIG_SPOTFLOW_MQTT_TASK_SIZE}) is smaller than "
                "required minimum (${MIN_MQTT_TASK_REQUIRED_SIZE}). Please update to match Spotflow requirements."
            )

        else()
            message(STATUS
                "CONFIG_SPOTFLOW_MQTT_TASK_SIZE (${CONFIG_SPOTFLOW_MQTT_TASK_SIZE}) "
                "meets minimum requirement (${MIN_MQTT_TASK_REQUIRED_SIZE})"
            )
        endif()
    endif()
else()
    message(STATUS "No CBOR or Coredump requirements found, using default MQTT task size")
endif()
