if(NOT CONFIG_SPOTFLOW)
    return()
endif()

add_subdirectory(src)
add_subdirectory(src/logging)
add_subdirectory(src/net)

zephyr_include_directories(
        src
)

# Prepend patching the .elf and .hex files with the build ID to post-build commands so
# that the build ID is contained in the binary image. It's insufficient to patch just
# the .elf file, because Zephyr's extra post-build commands are inserted after the
# generation of the .hex and .bin files.
#
# TODO: Patch the .bin file as well.
if(CONFIG_SPOTFLOW_GENERATE_BUILD_ID) 
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
                ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.hex
            ${PROPERTY_EXTRA_POST_BUILD_COMMANDS}
    )
endif()
