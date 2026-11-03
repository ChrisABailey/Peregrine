# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# rangerings-py — the Python package example. Nothing is compiled: package/ is
# packed into com.example.pyrings.pgpkg as it stands.
cmake_minimum_required(VERSION 3.21)
project(rangerings_py NONE)

set(PYRINGS_ID com.example.pyrings)
set(PYRINGS_STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(PYRINGS_PGPKG ${CMAKE_CURRENT_BINARY_DIR}/${PYRINGS_ID}.pgpkg)
file(GLOB_RECURSE PYRINGS_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
list(FILTER PYRINGS_PACKAGE_FILES EXCLUDE REGEX "__pycache__")
add_custom_command(
  OUTPUT ${PYRINGS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${PYRINGS_STAGE}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package
          ${PYRINGS_STAGE}/${PYRINGS_ID}
  COMMAND ${CMAKE_COMMAND} -E remove_directory
          ${PYRINGS_STAGE}/${PYRINGS_ID}/python/pyrings/__pycache__
  COMMAND ${CMAKE_COMMAND} -E remove -f ${PYRINGS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E chdir ${PYRINGS_STAGE}
          ${CMAKE_COMMAND} -E tar cf ${PYRINGS_PGPKG} --format=zip ${PYRINGS_ID}
  DEPENDS ${PYRINGS_PACKAGE_FILES}
  COMMENT "Packing ${PYRINGS_ID}.pgpkg"
  VERBATIM)
add_custom_target(pyrings_pgpkg ALL DEPENDS ${PYRINGS_PGPKG})
