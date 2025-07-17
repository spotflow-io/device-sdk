if(NOT CONFIG_SPOTFLOW)
    return()
endif()

add_subdirectory(src)
add_subdirectory(src/logging)
add_subdirectory(src/net)

zephyr_include_directories(
        src
)

# FIXME: Not working, because we cannot insert the step before the generation of
# merged.hex. Therefore, we'll have to patch that as well.
if(CONFIG_SPOTFLOW_GENERATE_BUILD_ID)
    # Prepend patching the .elf file with the build ID to post-build commands so
    # that the build ID is contained in the binary image.
    get_filename_component(
        spotflow_patch_build_id_script
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/patch_build_id.py
        ABSOLUTE
    )
    get_property(
        PROPERTY_EXTRA_POST_BUILD_COMMANDS
        GLOBAL
        PROPERTY extra_post_build_commands
    )
    set_property(
        GLOBAL
        PROPERTY extra_post_build_commands
            COMMAND
                ${PYTHON_EXECUTABLE}
                ${spotflow_patch_build_id_script}
                ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.elf
            ${PROPERTY_EXTRA_POST_BUILD_COMMANDS}
    )
endif()
