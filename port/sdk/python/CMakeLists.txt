# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# libperegrine_python — the Python bridge (extensions plan §3). pyfvw's
# sources compiled as an embedded module, over the shared core, linked against
# the libpython pybind11 found. The apps open it at the first python package.
set(PYFVW_DIR ${CMAKE_SOURCE_DIR}/port/bindings/pyfvw)
add_library(peregrine_python SHARED
  peregrine_python.cpp
  ${PYFVW_DIR}/pyfvw_module.cpp
  ${PYFVW_DIR}/pyfvw_app.cpp
  ${PYFVW_DIR}/pyfvw_draw.cpp
  ${PYFVW_DIR}/pyfvw_nav.cpp
  ${PYFVW_DIR}/pyfvw_route.cpp
  ${PYFVW_DIR}/pyfvw_analysis.cpp)
target_compile_definitions(peregrine_python PRIVATE PYFVW_EMBEDDED)
target_include_directories(peregrine_python PRIVATE ${PYFVW_DIR})
# The pyfvw headers inline a few SQLite calls (TilePackWriter).
find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)
  set(_bridge_sqlite SQLite3::SQLite3)
else()
  set(_bridge_sqlite SQLite::SQLite3)
endif()
# Everything pyfvw links is already inside peregrine_core; only the headers are needed.
target_link_libraries(peregrine_python PRIVATE peregrine_core pybind11::embed ${_bridge_sqlite}
  $<COMPILE_ONLY:fv_vpf> $<COMPILE_ONLY:fv_geosym> $<COMPILE_ONLY:fv_enc>
  $<COMPILE_ONLY:fv_osm> $<COMPILE_ONLY:fv_routing> $<COMPILE_ONLY:fv_routekit>
  $<COMPILE_ONLY:fv_geo_tool>)
if(APPLE)
  set(_bridge_rpath "@loader_path")
else()
  set(_bridge_rpath "$ORIGIN")
endif()
set_target_properties(peregrine_python PROPERTIES
  OUTPUT_NAME peregrine_python
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  INSTALL_RPATH "${_bridge_rpath}")
