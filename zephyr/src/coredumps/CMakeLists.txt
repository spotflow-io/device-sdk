zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_COREDUMPS
        spotflow_coredumps_backend.c
        spotflow_coredumps_cbor.c
        spotflow_coredumps_net.c
        spotflow_reboot.c
)
