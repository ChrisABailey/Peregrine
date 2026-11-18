# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# web-features — the peregrine.web package: OGC API – Features and Esri
# feature layers opened by URL (port/web-features-plan.md). fvkit holds the
# protocol clients and the cache; this package supplies the libcurl transport
# (WF-D1) and registers the feature source. Built only when CMake finds
# libcurl and staged in build/packages/peregrine.web.
find_package(CURL QUIET)
if(NOT CURL_FOUND)
  message(STATUS "web-features: libcurl not found; the peregrine.web package is not built")
  return()
endif()

set(WEB_PACKAGE_ID peregrine.web)

if(APPLE)
  set(WEB_PACKAGE_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(WEB_PACKAGE_PLATFORM linux-aarch64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(WEB_PACKAGE_PLATFORM linux-x86_64)
elseif(WIN32)
  set(WEB_PACKAGE_PLATFORM windows-x86_64)
else()
  message(FATAL_ERROR "web-features: no package platform key for ${CMAKE_SYSTEM_NAME}")
endif()

if(DEFINED PEREGRINE_VERSION)
  set(WEB_USER_AGENT_VERSION ${PEREGRINE_VERSION})
else()
  set(WEB_USER_AGENT_VERSION 0.0.0)
endif()

add_library(webfeatures SHARED plugin.cpp curl_http.cpp)
target_link_libraries(webfeatures PRIVATE Peregrine::core CURL::libcurl)
target_compile_definitions(webfeatures PRIVATE PEREGRINE_WEB_VERSION="${WEB_USER_AGENT_VERSION}")
# No rpath of its own: @rpath/libperegrine_core resolves through the host's.
set_target_properties(webfeatures PROPERTIES CXX_STANDARD 17 CXX_VISIBILITY_PRESET hidden
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "")

set(WEB_PACKAGE_DIR ${CMAKE_BINARY_DIR}/packages/${WEB_PACKAGE_ID})
set(WEB_PACKAGE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/staged.stamp)
file(GLOB_RECURSE WEB_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
add_custom_command(
  OUTPUT ${WEB_PACKAGE_STAMP}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${WEB_PACKAGE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package ${WEB_PACKAGE_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${WEB_PACKAGE_DIR}/lib/${WEB_PACKAGE_PLATFORM}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:webfeatures> ${WEB_PACKAGE_DIR}/lib/${WEB_PACKAGE_PLATFORM}/
  COMMAND ${CMAKE_COMMAND} -E touch ${WEB_PACKAGE_STAMP}
  DEPENDS webfeatures ${WEB_PACKAGE_FILES}
  COMMENT "Staging ${WEB_PACKAGE_ID}"
  VERBATIM)
add_custom_target(web_features_package ALL DEPENDS ${WEB_PACKAGE_STAMP})

if(FVW_BUILD_TESTS AND NOT WIN32)
  # The client compiled in, against the shared core as the app is.
  add_executable(web_features_test test/web_features_test.cpp curl_http.cpp)
  target_link_libraries(web_features_test PRIVATE peregrine_core CURL::libcurl GTest::gtest_main)
  target_include_directories(web_features_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/port/DeskKit/test)
  add_dependencies(web_features_test web_features_package)
  target_compile_definitions(web_features_test PRIVATE
    FV_PACKAGES_DIR="${CMAKE_BINARY_DIR}/packages")
  gtest_discover_tests(web_features_test)
endif()
