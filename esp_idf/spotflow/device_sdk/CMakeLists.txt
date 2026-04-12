cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Core sources & dependencies (always built)
# ---------------------------------------------------------------------------
file(GLOB SRCS
    "src/net/*.c"
    "src/logging/*.c"
    "src/configs/*.c"
    "spotflow.c"
)

set(INCLUDE_DIRS
    "include/net"
    "include/logging"
    "include/configs"
    "include"
)

set(PRIV_REQUIRES
    mqtt
    esp_partition
    nvs_flash
    esp_netif
    app_update
    espcoredump
)

# ---------------------------------------------------------------------------
# Optional: Build ID
# ---------------------------------------------------------------------------
if(CONFIG_SPOTFLOW_USE_BUILD_ID)
    list(APPEND SRCS "src/buildid/spotflow_build_id.c")
    list(APPEND INCLUDE_DIRS "include/buildid")
    list(APPEND PRIV_REQUIRES esp_app_format)
endif()

# ---------------------------------------------------------------------------
# Optional: Coredump
# ---------------------------------------------------------------------------
if(CONFIG_ESP_COREDUMP_ENABLE)
    file(GLOB COREDUMP_SRCS "src/coredump/*.c")
    list(APPEND SRCS ${COREDUMP_SRCS})
    list(APPEND INCLUDE_DIRS "include/coredump")
endif()

# ---------------------------------------------------------------------------
# Optional: Metrics
# ---------------------------------------------------------------------------
if(CONFIG_SPOTFLOW_METRICS)
    file(GLOB METRICS_SRCS CONFIGURE_DEPENDS "src/metrics/*.c")
    file(GLOB METRICS_SYSTEM_SRCS CONFIGURE_DEPENDS "src/metrics/system/*.c")

    # -- Prune disabled system metric sources --
    if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE)
        list(REMOVE_ITEM METRICS_SYSTEM_SRCS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_reset_helper.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system.c"
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
            "LWIP_SNMP=1;MIB2_STATS=1;LWIP_NETIF_EXT_STATUS_CALLBACK=1"
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

    list(APPEND SRCS ${METRICS_SRCS} ${METRICS_SYSTEM_SRCS})
    list(APPEND INCLUDE_DIRS "include/metrics")

    # Only expose system headers if any system sources are being built
    if(METRICS_SYSTEM_SRCS)
        list(APPEND INCLUDE_DIRS "include/metrics/system")
    endif()
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
