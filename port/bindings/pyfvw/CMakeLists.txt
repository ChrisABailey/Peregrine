# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# pyfvw — pybind11 bindings for FvKit (contracts D5). Slice 1: L0 types +
# DTED/GeoTIFF adapters. Skipped gracefully if no Python dev environment.

include(FetchContent)
FetchContent_Declare(
  pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(pyfvw pyfvw_module.cpp pyfvw_app.cpp pyfvw_draw.cpp pyfvw_nav.cpp
                          pyfvw_route.cpp)
# fv_vpf = VpfVectorSource, fv_geosym = GeoSymStyleEngine; both link fv_fvkit
# (the shared VectorRenderer + canvas + projection).
# fv_routing = the O4 road graph + router (pyfvw.routing).
# fv_routekit = the route document, planner, overlay and editor (pyfvw.route).
# It links BOTH fvkit and fv_routing, which is the one thing fvkit may not do --
# and is why the route overlay is a separate submodule with its own
# registration call rather than part of app.register_builtin_types().
target_link_libraries(pyfvw PRIVATE fv_fvkit fv_geo_tool fv_vpf fv_geosym fv_enc fv_osm
                                   fv_routing fv_routekit)

if(FVW_BUILD_TESTS)
  # pytest suite (skips itself when numpy/TestData are absent)
  # PYTHON_EXECUTABLE: set by pybind11 (classic FindPythonInterp mode);
  # matches the interpreter the module was built for.
  add_test(NAME pyfvw_pytest
    COMMAND ${PYTHON_EXECUTABLE} -m pytest -q ${CMAKE_CURRENT_SOURCE_DIR}/test)
  # ENVIRONMENT is a real CMake list; plain ';' separates entries here (unlike
  # gtest_discover_tests, which needs '\;'). The earlier '\;' made one giant
  # PYTHONPATH value, so MSPCCS_DATA/FVW_TESTDATA_DIR were never set.
  set_tests_properties(pyfvw_pytest PROPERTIES ENVIRONMENT
    "PYTHONPATH=$<TARGET_FILE_DIR:pyfvw>;MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib;FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
