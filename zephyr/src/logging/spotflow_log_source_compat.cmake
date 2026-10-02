# Apply the Zephyr 3.7 registration compatibility header to logging producers
# after all libraries have been declared. Build-time header generators such as
# offsets must not include log.h before their generated headers are available.
function(spotflow_retain_log_source_names)
    get_property(logging_libraries GLOBAL PROPERTY ZEPHYR_LIBS)
    list(APPEND logging_libraries app)
    foreach(logging_library IN LISTS logging_libraries)
        target_compile_options(${logging_library} PRIVATE
            "$<$<COMPILE_LANGUAGE:C,CXX>:-include>"
            "$<$<COMPILE_LANGUAGE:C,CXX>:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/spotflow_log_source_compat.h>"
        )
    endforeach()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL spotflow_retain_log_source_names)
