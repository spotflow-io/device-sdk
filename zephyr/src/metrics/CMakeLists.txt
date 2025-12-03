zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_PIPELINE
        spotflow_metrics_backend.c
        spotflow_metrics_cbor.c
        spotflow_metrics.c
        spotflow_metrics_net.c
)
