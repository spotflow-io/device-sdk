# Validate default aggregation interval
if(CONFIG_SPOTFLOW_METRICS)
    set(VALID_INTERVALS 0 60 600 3600)
    if(NOT CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL IN_LIST VALID_INTERVALS)
        message(FATAL_ERROR
            "CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL must be 0, 60, 600, or 3600. "
            "Got: ${CONFIG_SPOTFLOW_METRICS_DEFAULT_AGGREGATION_INTERVAL}")
    endif()
endif()

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS
        spotflow_metrics_backend.c
        spotflow_metrics_registry.c
        spotflow_metrics_aggregator.c
        spotflow_metrics_cbor.c
        spotflow_metrics_net.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_HEARTBEAT
        spotflow_metrics_heartbeat.c
)

if(CONFIG_SPOTFLOW_METRICS_SYSTEM)
    add_subdirectory(system)
endif()
