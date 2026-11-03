# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# rangerings-c — the C plugin API example. Builds librangerings and packs it
# with package/ into com.example.rangerings.pgpkg. Needs only the SDK's
# include directory, so it also builds on its own:
#   cmake -S port/sdk/examples/rangerings-c -B build-rings && cmake --build build-rings
cmake_minimum_required(VERSION 3.21)
project(rangerings_c LANGUAGES C)

set(RINGS_ID com.example.rangerings)

# The key under `libraries` in package/peregrine-package.json for this machine.
if(APPLE)
  set(RINGS_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(RINGS_PLATFORM linux-aarch64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(RINGS_PLATFORM linux-x86_64)
elseif(WIN32)
  set(RINGS_PLATFORM windows-x86_64)
else()
  message(FATAL_ERROR "rangerings-c: no package platform key for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(rangerings SHARED rangerings.c)
target_include_directories(rangerings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
set_target_properties(rangerings PROPERTIES C_STANDARD 99 C_VISIBILITY_PRESET hidden)
if(NOT WIN32)
  target_link_libraries(rangerings PRIVATE m)
endif()

set(RINGS_STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(RINGS_PGPKG ${CMAKE_CURRENT_BINARY_DIR}/${RINGS_ID}.pgpkg)
file(GLOB_RECURSE RINGS_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
add_custom_command(
  OUTPUT ${RINGS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${RINGS_STAGE}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package
          ${RINGS_STAGE}/${RINGS_ID}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RINGS_STAGE}/${RINGS_ID}/lib/${RINGS_PLATFORM}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rangerings>
          ${RINGS_STAGE}/${RINGS_ID}/lib/${RINGS_PLATFORM}/
  COMMAND ${CMAKE_COMMAND} -E remove -f ${RINGS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E chdir ${RINGS_STAGE}
          ${CMAKE_COMMAND} -E tar cf ${RINGS_PGPKG} --format=zip ${RINGS_ID}
  DEPENDS rangerings ${RINGS_PACKAGE_FILES}
  COMMENT "Packing ${RINGS_ID}.pgpkg"
  VERBATIM)
add_custom_target(rangerings_pgpkg ALL DEPENDS ${RINGS_PGPKG})
