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
# ---- Optional metrics sources ----
if(CONFIG_SPOTFLOW_METRICS)
	file(GLOB_RECURSE METRICS_SRCS CONFIGURE_DEPENDS
	"src/metrics/*.c"
	"src/metrics/system/**/*.c"
	)
  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE)
    list(REMOVE_ITEM METRICS_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_reset_helper.c
    )
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_connection.c
      )
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_heap.c
      )
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_network.c
      )
  else()
      idf_build_set_property(COMPILE_DEFINITIONS "LWIP_SNMP=1;MIB2_STATS=1;LWIP_NETIF_EXT_STATUS_CALLBACK=1" APPEND)
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_cpu.c
      )
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_connection.c
      )
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system.c
      )
  endif()

  if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK)
      list(REMOVE_ITEM METRICS_SRCS
          ${CMAKE_CURRENT_SOURCE_DIR}/src/metrics/system/spotflow_metrics_system_stack.c
      )
  endif()
	list(APPEND SRCS ${METRICS_SRCS})
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
