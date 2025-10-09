zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_LOG_BACKEND
        spotflow_log_backend.c
        spotflow_log_cbor.c
        spotflow_cbor_output_context.c
        spotflow_log_net.c
)
