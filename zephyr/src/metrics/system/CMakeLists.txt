# Validate aggregation interval
set(VALID_INTERVALS 0 60 3600 86400)
if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL IN_LIST VALID_INTERVALS)
    message(FATAL_ERROR
        "CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL must be 0, 60, 3600, or 86400. "
        "Got: ${CONFIG_SPOTFLOW_METRICS_SYSTEM_AGGREGATION_INTERVAL}")
endif()

zephyr_library_sources(spotflow_metrics_system.c)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
    spotflow_metrics_system_heap.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK
    spotflow_metrics_system_network.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
    spotflow_metrics_system_cpu.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
    spotflow_metrics_system_connection.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
    spotflow_metrics_system_stack.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
    spotflow_reset_helper.c
)
