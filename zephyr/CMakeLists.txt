if(NOT CONFIG_SPOTFLOW)
    return()
endif()

add_subdirectory(src/logging)
add_subdirectory(src/net)

zephyr_include_directories(
        src
)
