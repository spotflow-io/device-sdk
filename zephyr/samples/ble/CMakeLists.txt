cmake_minimum_required(VERSION 3.20.0)

# Board overlays use Zephyr 4.4 layouts; extend them for Zephyr 4.5 and newer.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE} COMPONENTS version)
if (KERNEL_VERSION_STRING VERSION_GREATER_EQUAL "4.5.0")
  set(coredump_board "${BOARD}")
  if (NOT coredump_board)
    set(coredump_board "$ENV{BOARD}")
  endif ()
  string(REGEX REPLACE "@[^/]*" "" coredump_board "${coredump_board}")
  string(REPLACE "/" "_" coredump_board "${coredump_board}")
  set(coredump_partition_overlay
      "${CMAKE_CURRENT_SOURCE_DIR}/boards/zephyr-4.5/${coredump_board}.overlay")
  if (EXISTS "${coredump_partition_overlay}")
    list(APPEND EXTRA_DTC_OVERLAY_FILE "${coredump_partition_overlay}")
  endif ()
endif ()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spotflow_ble)

target_sources(app PRIVATE src/main.c)
