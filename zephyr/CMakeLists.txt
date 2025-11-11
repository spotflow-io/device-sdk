if(NOT CONFIG_SPOTFLOW)
    return()
endif()

add_subdirectory(src)
add_subdirectory(src/logging)
add_subdirectory(src/net)
add_subdirectory(src/config)
add_subdirectory(src/coredumps)

zephyr_include_directories(
        src
)

# Prepend patching the .elf, .hex, and .bin files with the build ID to post-build commands
# so that the build ID is contained in the binary image. It's insufficient to patch just
# the .elf file, because Zephyr's extra post-build commands are inserted after the generation
# of the .hex and .bin files.
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

    # Their existence is checked in the script
    set(POTENTIALLY_PATCHED_FILES
        ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.elf
        ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.hex
        ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.bin
        ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.strip
    )
    if(NOT CONFIG_NATIVE_LIBRARY)
        # The .exe file is just a copy of the .elf file unless targeting native simulator
        # (currently unsupported)
        list(
            APPEND
            POTENTIALLY_PATCHED_FILES
            ${CMAKE_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.exe
        )
    endif()

    set_property(
        GLOBAL
        PROPERTY extra_post_build_commands
            COMMAND
                ${PYTHON_EXECUTABLE}
                ${spotflow_patch_build_id_script}
                ${POTENTIALLY_PATCHED_FILES}
            ${PROPERTY_EXTRA_POST_BUILD_COMMANDS}
    )
endif()
