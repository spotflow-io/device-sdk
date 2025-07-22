if(NOT CONFIG_SPOTFLOW)
    return()
endif()

add_subdirectory(src/logging)
add_subdirectory(src/net)
add_subdirectory(src/coredumps)

zephyr_include_directories(
        PRIVATE src
)
