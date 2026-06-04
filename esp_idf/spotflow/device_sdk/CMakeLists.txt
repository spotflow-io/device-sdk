cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Core sources & dependencies (always built)
# ---------------------------------------------------------------------------
file(GLOB SRCS
	  "src/net/*.c"
	  "spotflow.c"
)

set(INCLUDE_DIRS
    "include"
)

set(PRIV_REQUIRES
    mqtt
    esp_partition
    nvs_flash
    esp_netif
    app_update
)

# ---- Optional logging source ----
if(CONFIG_SPOTFLOW_LOG_BACKEND)
  file(GLOB LOGGING_SRCS "src/logging/*.c")
	list(APPEND SRCS ${LOGGING_SRCS})
  file(GLOB CONFIGS_SRCS "src/configs/*.c")
  list(APPEND SRCS ${CONFIGS_SRCS})
endif()

# ---------------------------------------------------------------------------
# Optional: Build ID
# ---------------------------------------------------------------------------
if(CONFIG_SPOTFLOW_USE_BUILD_ID)
    list(APPEND SRCS "src/buildid/spotflow_build_id.c")
    list(APPEND PRIV_REQUIRES esp_app_format)
endif()

# ---------------------------------------------------------------------------
# Optional: Coredump
# ---------------------------------------------------------------------------
if(CONFIG_SPOTFLOW_COREDUMP_BACKEND)
list(APPEND PRIV_REQUIRES espcoredump)
endif()

if(CONFIG_ESP_COREDUMP_ENABLE)
    file(GLOB COREDUMP_SRCS "src/coredump/*.c")
    list(APPEND SRCS ${COREDUMP_SRCS})
endif()

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
# ---------------------------------------------------------------------------
# Optional: Metrics
# ---------------------------------------------------------------------------
if(CONFIG_SPOTFLOW_METRICS)
    file(GLOB METRICS_SRCS CONFIGURE_DEPENDS "src/metrics/*.c")
# ---------------------------------------------------------------------------
# Optional: Individual System Metrics
# ---------------------------------------------------------------------------
    if(CONFIG_SPOTFLOW_METRICS_SYSTEM)
      file(GLOB METRICS_SYSTEM_SRCS CONFIGURE_DEPENDS "src/metrics/system/*.c")

      # -- Prune disabled system metric sources --
      if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE)
          list(REMOVE_ITEM METRICS_SYSTEM_SRCS
              "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_reset_helper.c"
          )
      endif()

      if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION)
          list(REMOVE_ITEM METRICS_SYSTEM_SRCS
              "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_connection.c"
          )
      endif()

      if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP)
          list(REMOVE_ITEM METRICS_SYSTEM_SRCS
              "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_heap.c"
          )
      endif()

      if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK)
          list(REMOVE_ITEM METRICS_SYSTEM_SRCS
              "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_network.c"
          )
      else()
          idf_build_set_property(COMPILE_DEFINITIONS
              "LWIP_SNMP=1;MIB2_STATS=1;LWIP_NETIF_EXT_STATUS_CALLBACK=1" # Some additional configs for LWIP networking Stats which are not exposed by esp idf.
              APPEND
          )
      endif()

      if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU)
          list(REMOVE_ITEM METRICS_SYSTEM_SRCS
              "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_cpu.c"
          )
      endif()

      if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK)
          list(REMOVE_ITEM METRICS_SYSTEM_SRCS
              "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_stack.c"
          )
      endif()
    endif()

    list(APPEND SRCS ${METRICS_SRCS} ${METRICS_SYSTEM_SRCS})
endif()

# ---------------------------------------------------------------------------
# Register component
# ---------------------------------------------------------------------------
idf_component_register(
    SRCS            ${SRCS}
    INCLUDE_DIRS    ${INCLUDE_DIRS}
    REQUIRES        cbor
    PRIV_REQUIRES   ${PRIV_REQUIRES}
    EMBED_TXTFILES  x1_root.pem
)

# ---------------------------------------------------------------------------
# Sanity check: MQTT buffer must be >= max CBOR log length
# ---------------------------------------------------------------------------
if(CONFIG_MQTT_BUFFER_SIZE LESS CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)
    message(WARNING
        "CONFIG_MQTT_BUFFER_SIZE (${CONFIG_MQTT_BUFFER_SIZE}) is smaller than "
        "CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN (${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN}). "
        "Forcing MQTT_BUFFER_SIZE to match Spotflow requirements."
    )
    set(CONFIG_MQTT_BUFFER_SIZE ${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN}
        CACHE INT "MQTT buffer size" FORCE
    )
endif()
