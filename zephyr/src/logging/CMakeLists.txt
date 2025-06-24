zephyr_library_sources_ifdef(CONFIG_LOG_BACKEND_SPOTFLOW
        spotflow_log_backend.c
        spotflow_cbor.c
        spotflow_cbor_output_context.c
)
