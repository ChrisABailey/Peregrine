# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# analysis-py — a larger Python package: measurement, terrain profile and
# viewshed. Nothing is compiled: package/ is packed into
# com.example.analysis.pgpkg as it stands.
cmake_minimum_required(VERSION 3.21)
project(analysis_py NONE)

set(ANALYSIS_ID com.example.analysis)
set(ANALYSIS_STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(ANALYSIS_PGPKG ${CMAKE_CURRENT_BINARY_DIR}/${ANALYSIS_ID}.pgpkg)
file(GLOB_RECURSE ANALYSIS_PACKAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/package/*)
list(FILTER ANALYSIS_PACKAGE_FILES EXCLUDE REGEX "__pycache__")
add_custom_command(
  OUTPUT ${ANALYSIS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${ANALYSIS_STAGE}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package
          ${ANALYSIS_STAGE}/${ANALYSIS_ID}
  COMMAND ${CMAKE_COMMAND} -E remove_directory
          ${ANALYSIS_STAGE}/${ANALYSIS_ID}/python/pyanalysis/__pycache__
  COMMAND ${CMAKE_COMMAND} -E remove -f ${ANALYSIS_PGPKG}
  COMMAND ${CMAKE_COMMAND} -E chdir ${ANALYSIS_STAGE}
          ${CMAKE_COMMAND} -E tar cf ${ANALYSIS_PGPKG} --format=zip ${ANALYSIS_ID}
  DEPENDS ${ANALYSIS_PACKAGE_FILES}
  COMMENT "Packing ${ANALYSIS_ID}.pgpkg"
  VERBATIM)
add_custom_target(analysis_pgpkg ALL DEPENDS ${ANALYSIS_PGPKG})
