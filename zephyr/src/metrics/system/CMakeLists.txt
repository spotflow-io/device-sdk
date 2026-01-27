# Validate aggregation interval
set(VALID_INTERVALS 60 600 3600)
if(NOT CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL IN_LIST VALID_INTERVALS)
    message(FATAL_ERROR
        "CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL must be 60, 600, or 3600. "
        "Got: ${CONFIG_SPOTFLOW_METRICS_SYSTEM_INTERVAL}")
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
