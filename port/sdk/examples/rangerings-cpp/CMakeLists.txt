# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# rangerings-cpp — the native plugin example. Builds libnativerings against
# Peregrine::core and packs it with package/ into com.example.nativerings.pgpkg.
# Inside the Peregrine tree it uses the core being built; on its own it finds
# a built Peregrine's CMake package:
#   cmake -S port/sdk/examples/rangerings-cpp -B build-native \
#     -DPeregrine_DIR=<peregrine build>/port/sdk && cmake --build build-native
# The plugin loads only into the Peregrine build it was compiled against.
cmake_minimum_required(VERSION 3.21)
project(rangerings_cpp LANGUAGES CXX)

if(NOT TARGET Peregrine::core)
  find_package(Peregrine CONFIG REQUIRED)
endif()

set(NATIVE_RINGS_ID com.example.nativerings)

# The key under `libraries` in package/peregrine-package.json for this machine.
if(APPLE)
  set(NATIVE_RINGS_PLATFORM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(NATIVE_RINGS_PLATFORM linux-aarch64)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(NATIVE_RINGS_PLATFORM linux-x86_64)
elseif(WIN32)
  set(NATIVE_RINGS_PLATFORM windows-x86_64)
else()
  message(FATAL_ERROR "rangerings-cpp: no package platform key for ${CMAKE_SYSTEM_NAME}")
endif()

add_library(nativerings SHARED rangerings.cpp)
target_link_libraries(nativerings PRIVATE Peregrine::core)
# No rpath of its own: @rpath/libperegrine_core resolves through the host's,
# so the plugin binds to the core the app loaded, not the one it was built
# against.
set_target_properties(nativerings PROPERTIES CXX_STANDARD 17 CXX_VISIBILITY_PRESET hidden
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "")

set(NATIVE_RINGS_STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(NATIVE_RINGS_PGPKG ${CMAKE_CURRENT_BINARY_DIR}/${NATIVE_RINGS_ID}.pgpkg)
file(GLOB_RECURSE NATIVE_RINGS_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
add_custom_command(
  OUTPUT ${NATIVE_RINGS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${NATIVE_RINGS_STAGE}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package
          ${NATIVE_RINGS_STAGE}/${NATIVE_RINGS_ID}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${NATIVE_RINGS_STAGE}/${NATIVE_RINGS_ID}/lib/${NATIVE_RINGS_PLATFORM}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:nativerings>
          ${NATIVE_RINGS_STAGE}/${NATIVE_RINGS_ID}/lib/${NATIVE_RINGS_PLATFORM}/
  COMMAND ${CMAKE_COMMAND} -E remove -f ${NATIVE_RINGS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E chdir ${NATIVE_RINGS_STAGE}
          ${CMAKE_COMMAND} -E tar cf ${NATIVE_RINGS_PGPKG} --format=zip ${NATIVE_RINGS_ID}
  DEPENDS nativerings ${NATIVE_RINGS_PACKAGE_FILES}
  COMMENT "Packing ${NATIVE_RINGS_ID}.pgpkg"
  VERBATIM)
add_custom_target(nativerings_pgpkg ALL DEPENDS ${NATIVE_RINGS_PGPKG})
