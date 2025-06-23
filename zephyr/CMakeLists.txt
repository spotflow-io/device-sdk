if(NOT CONFIG_SPOTFLOW)
    return()
endif()

zephyr_include_directories(include)
zephyr_include_directories(../../include)

add_subdirectory(../../src/logging  ${CMAKE_CURRENT_BINARY_DIR}/logging_build)
add_subdirectory(../../src/net  ${CMAKE_CURRENT_BINARY_DIR}/net_build)

# not sure if needed
#zephyr_library_link_libraries_ifdef(CONFIG_MBEDTLS mbedTLS)

# Add Spotflow libraries to the Zephyr build system - not needed most likely
#zephyr_interface_library_named(spotflow)
#zephyr_library()

#zephyr_library_sources(
#    ../../src/log.c
#)
#
#zephyr_include_directories(
#    # shared headers
#    ../../include
#    #zephyr specific headers
#    ./include
#)
