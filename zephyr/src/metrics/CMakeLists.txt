zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS
        spotflow_metrics_backend.c
        spotflow_metrics_aggregator.c
        spotflow_metrics_cbor.c
        spotflow_metrics_net.c
)

if(CONFIG_SPOTFLOW_METRICS_SYSTEM)
    add_subdirectory(system)
endif()
