zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_OTA
        spotflow_ota.c
        protocol/spotflow_ota_cbor.c
        downloader/spotflow_ota_downloader.c
        downloader/spotflow_ota_downloader_transport_errors.c
        downloader/spotflow_ota_downloader_transport_range.c
        downloader/spotflow_ota_downloader_transport_socket.c
        firmware/spotflow_ota_fw_custom.c
        core/spotflow_ota_log.c
        protocol/spotflow_ota_net.c
        persistence/spotflow_ota_persistence.c
        persistence/spotflow_ota_records_cbor.c
        core/spotflow_ota_state.c
        downloader/spotflow_ota_url.c
        core/spotflow_ota_worker.c
)

if(CONFIG_SPOTFLOW_OTA)
        zephyr_linker_sources(DATA_SECTIONS downloader/spotflow_ota_downloader.ld)
        zephyr_iterable_section(
                NAME spotflow_downloader
                GROUP DATA_REGION
                ${XIP_ALIGN_WITH_INPUT}
        )
endif()

zephyr_library_sources_ifdef(CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE
        firmware/spotflow_ota_fw_main.c
        platform/spotflow_ota_identity.c
        platform/spotflow_ota_platform.c
)
