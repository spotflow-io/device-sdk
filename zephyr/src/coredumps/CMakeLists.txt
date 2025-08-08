zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_COREDUMPS
        spotflow_coredumps_backend.c
        spotflow_coredumps_cbor.c
        spotflow_coredumps_net.c
)

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_USE_DEFAULT_REBOOT_HANDLER
        spotflow_reboot.c
)
