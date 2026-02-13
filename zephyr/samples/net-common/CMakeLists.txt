target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/net.c
)

if (CONFIG_SPOTFLOW_USE_WIFI)
  target_sources(app PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/wifi.c
  )
endif ()

if (CONFIG_SPOTFLOW_USE_ETH)
  target_sources(app PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/eth.c
  )
endif ()
