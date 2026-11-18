# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# ais-overlay — the peregrine.ais package: live AIS traffic from Open Waters
# (HTTPS through libcurl, the system copy on macOS) and a local NMEA receiver.
# Builds libaistraffic against Peregrine::core and stages the package in
# build/packages/peregrine.ais. Without libcurl the package is not built and
# Peregrine runs without AIS.
find_package(CURL QUIET)
if(NOT CURL_FOUND)
  message(STATUS "ais-overlay: libcurl not found; the peregrine.ais package is not built")
  return()
endif()

set(AIS_PACKAGE_ID peregrine.ais)

if(APPLE)
  set(AIS_PACKAGE_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(AIS_PACKAGE_PLATFORM linux-aarch64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(AIS_PACKAGE_PLATFORM linux-x86_64)
elseif(WIN32)
  set(AIS_PACKAGE_PLATFORM windows-x86_64)
else()
  message(FATAL_ERROR "ais-overlay: no package platform key for ${CMAKE_SYSTEM_NAME}")
endif()

set(AIS_TRAFFIC_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/ais_traffic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sse_stream.cpp)

add_library(aistraffic SHARED plugin.cpp ${AIS_TRAFFIC_SOURCES})
target_link_libraries(aistraffic PRIVATE Peregrine::core CURL::libcurl)
# No rpath of its own: @rpath/libperegrine_core resolves through the host's.
set_target_properties(aistraffic PROPERTIES CXX_STANDARD 17 CXX_VISIBILITY_PRESET hidden
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "")

set(AIS_PACKAGE_DIR ${CMAKE_BINARY_DIR}/packages/${AIS_PACKAGE_ID})
set(AIS_PACKAGE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/staged.stamp)
file(GLOB_RECURSE AIS_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
add_custom_command(
  OUTPUT ${AIS_PACKAGE_STAMP}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${AIS_PACKAGE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package ${AIS_PACKAGE_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${AIS_PACKAGE_DIR}/lib/${AIS_PACKAGE_PLATFORM}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:aistraffic> ${AIS_PACKAGE_DIR}/lib/${AIS_PACKAGE_PLATFORM}/
  COMMAND ${CMAKE_COMMAND} -E touch ${AIS_PACKAGE_STAMP}
  DEPENDS aistraffic ${AIS_PACKAGE_FILES}
  COMMENT "Staging ${AIS_PACKAGE_ID}"
  VERBATIM)
add_custom_target(ais_overlay_package ALL DEPENDS ${AIS_PACKAGE_STAMP})

if(FVW_BUILD_TESTS AND NOT WIN32)
  # The overlay's sources compiled in, against the shared core as the app is,
  # so the test and the loaded plugin share one copy of every table.
  add_executable(ais_traffic_test test/ais_traffic_test.cpp ${AIS_TRAFFIC_SOURCES})
  target_link_libraries(ais_traffic_test PRIVATE peregrine_core CURL::libcurl GTest::gtest_main)
  target_include_directories(ais_traffic_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/port/DeskKit/test)
  add_dependencies(ais_traffic_test ais_overlay_package)
  target_compile_definitions(ais_traffic_test PRIVATE
    FV_PACKAGES_DIR="${CMAKE_BINARY_DIR}/packages")
  gtest_discover_tests(ais_traffic_test)
endif()
