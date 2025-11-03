cmake_minimum_required(VERSION 3.16)
set(SRCS
    "src/net/spotflow_mqtt.c"
    "src/logging/spotflow_log_backend.c"
    "src/logging/spotflow_log_queue.c"
    "src/logging/spotflow_log_cbor.c"
    "spotflow.c"
)

if(CONFIG_SPOTFLOW_GENERATE_BUILD_ID)
    list(APPEND SRCS "src/buildid/spotflow_build_id.c")
endif()

if(CONFIG_ESP_COREDUMP_ENABLE)
    list(APPEND SRCS "src/coredump/spotflow_coredump.c")
    list(APPEND SRCS "src/coredump/spotflow_coredump_cbor.c")
    list(APPEND SRCS "src/coredump/spotflow_coredump_queue.c")
endif()

idf_component_register( SRCS ${SRCS}   
                        PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update espcoredump
                        INCLUDE_DIRS "include"
                        REQUIRES cbor
                        EMBED_TXTFILES x1_root.pem)

# ---- Enforce MQTT buffer size sanity check ----
if (CONFIG_MQTT_BUFFER_SIZE LESS CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)
    message(WARNING
        "CONFIG_MQTT_BUFFER_SIZE (${CONFIG_MQTT_BUFFER_SIZE}) "
        "is smaller than CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN (${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN}). "
        "Forcing MQTT_BUFFER_SIZE to match Spotflow requirements.")
    # Force the MQTT buffer size to be large enough
    set(CONFIG_MQTT_BUFFER_SIZE ${CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN} CACHE INT "MQTT buffer size" FORCE)
endif()

# Force the linker to keep the symbol in final ELF
# This adds an undefined reference so the linker must keep the symbol
target_link_libraries(${COMPONENT_LIB} PRIVATE "-Wl,--undefined=bindesc_entry_spotflow_build_id")

if(CONFIG_SPOTFLOW_GENERATE_BUILD_ID)
    # Get the project name from the parent scope
    idf_build_get_property(project_name PROJECT_NAME)
    idf_build_get_property(project_dir PROJECT_DIR)
    idf_build_get_property(build_dir BUILD_DIR)
    
    # Paths
    set(SPOTFLOW_ELF "${build_dir}/${project_name}.elf")
    set(SPOTFLOW_BIN "${build_dir}/${project_name}.bin")
    set(SPOTFLOW_BUILD_ID_DONE "${build_dir}/spotflow_build_id_done")
    
    # Get the script path relative to component directory
    get_filename_component(spotflow_patch_build_id_script
        "${COMPONENT_DIR}/scripts/patch_build_id.py" ABSOLUTE)

    # Custom command that will run after the ELF is created
    add_custom_command(
        OUTPUT ${SPOTFLOW_BUILD_ID_DONE}
        COMMAND ${PYTHON} "${spotflow_patch_build_id_script}" "${SPOTFLOW_ELF}"  "${SPOTFLOW_BIN}"
        COMMAND ${CMAKE_COMMAND} -E touch "${SPOTFLOW_BUILD_ID_DONE}"
        DEPENDS ${SPOTFLOW_ELF}
        COMMENT "Patching and printing Spotflow Build ID..."
        VERBATIM
        WORKING_DIRECTORY ${project_dir}
    )

    # Create a custom target that depends on the app being built
    add_custom_target(spotflow_build_id ALL
        DEPENDS ${SPOTFLOW_BUILD_ID_DONE}
    )
    
    # Make sure this runs after the main app is linked
    add_dependencies(spotflow_build_id app)
endif()