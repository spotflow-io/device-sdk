zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS
        spotflow_metrics_backend.c
        spotflow_metrics_aggregator.c
        spotflow_metrics_cbor.c
        spotflow_metrics_net.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM
        spotflow_metrics_system.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
        spotflow_reset_helper.c
)
