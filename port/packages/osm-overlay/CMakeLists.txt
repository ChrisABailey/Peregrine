# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# osm-overlay — the peregrine.osm package: OpenStreetMap as an overlay over the
# catalog's coverage. Builds libosmoverlay against Peregrine::core and stages
# the package directory in build/packages/peregrine.osm, which the
# Peregrine app bundles under Resources/packages. The default style sheet is
# Osm/styles/peregrine-osm-overlay.json, copied in at build time.
set(OSM_OVERLAY_ID peregrine.osm)

if(APPLE)
  set(OSM_OVERLAY_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(OSM_OVERLAY_PLATFORM linux-aarch64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(OSM_OVERLAY_PLATFORM linux-x86_64)
elseif(WIN32)
  set(OSM_OVERLAY_PLATFORM windows-x86_64)
else()
  message(FATAL_ERROR "osm-overlay: no package platform key for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(osmoverlay SHARED osm_overlay.cpp)
target_link_libraries(osmoverlay PRIVATE Peregrine::core)
# OsmVectorSource is inside the core but its header is not in the SDK; this
# package builds only in the Peregrine tree.
target_include_directories(osmoverlay PRIVATE ${CMAKE_SOURCE_DIR}/port/Osm)
# No rpath of its own: @rpath/libperegrine_core resolves through the host's.
set_target_properties(osmoverlay PROPERTIES CXX_STANDARD 17 CXX_VISIBILITY_PRESET hidden
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "")

set(OSM_OVERLAY_DIR ${CMAKE_BINARY_DIR}/packages/${OSM_OVERLAY_ID})
set(OSM_OVERLAY_STAMP ${CMAKE_CURRENT_BINARY_DIR}/staged.stamp)
set(OSM_OVERLAY_STYLE ${CMAKE_SOURCE_DIR}/port/Osm/styles/peregrine-osm-overlay.json)
file(GLOB_RECURSE OSM_OVERLAY_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
add_custom_command(
  OUTPUT ${OSM_OVERLAY_STAMP}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${OSM_OVERLAY_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package ${OSM_OVERLAY_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${OSM_OVERLAY_STYLE} ${OSM_OVERLAY_DIR}/resources/osm-overlay.json
  COMMAND ${CMAKE_COMMAND} -E make_directory ${OSM_OVERLAY_DIR}/lib/${OSM_OVERLAY_PLATFORM}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:osmoverlay> ${OSM_OVERLAY_DIR}/lib/${OSM_OVERLAY_PLATFORM}/
  COMMAND ${CMAKE_COMMAND} -E touch ${OSM_OVERLAY_STAMP}
  DEPENDS osmoverlay ${OSM_OVERLAY_PACKAGE_FILES} ${OSM_OVERLAY_STYLE}
  COMMENT "Staging ${OSM_OVERLAY_ID}"
  VERBATIM)
add_custom_target(osm_overlay_package ALL DEPENDS ${OSM_OVERLAY_STAMP})

if(FVW_BUILD_TESTS)
  # Against the shared core, as the app is, so the test and the plugin share
  # one copy of every table.
  add_executable(osm_overlay_test test/osm_overlay_test.cpp)
  target_link_libraries(osm_overlay_test PRIVATE peregrine_core GTest::gtest_main)
  target_include_directories(osm_overlay_test PRIVATE ${CMAKE_SOURCE_DIR}/port/DeskKit/test
    ${CMAKE_SOURCE_DIR}/port/Osm)
  add_dependencies(osm_overlay_test osm_overlay_package)
  target_compile_definitions(osm_overlay_test PRIVATE
    FV_PACKAGES_DIR="${CMAKE_BINARY_DIR}/packages"
    FV_BASE_STYLE_JSON="${CMAKE_SOURCE_DIR}/port/Osm/styles/peregrine-osm.json"
    FV_ALT_STYLE_JSON="${CMAKE_SOURCE_DIR}/port/Osm/styles/kiawah-trails.json")
  gtest_discover_tests(osm_overlay_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/testdata")
endif()
