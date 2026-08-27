zephyr_library_sources(spotflow_minimal_backend.c)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_LOG_BACKEND
    spotflow_minimal_logging.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS
    spotflow_minimal_metrics.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM
    spotflow_minimal_system.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP
    ../../metrics/system/spotflow_metrics_system_heap.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU
    ../../metrics/system/spotflow_metrics_system_cpu.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
    ../../metrics/system/spotflow_metrics_system_connection.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK
    spotflow_minimal_stack.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE
    spotflow_minimal_reset.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_COREDUMPS
    spotflow_minimal_coredump.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_USE_DEFAULT_REBOOT_HANDLER
    ../../coredumps/spotflow_reboot.c
)
