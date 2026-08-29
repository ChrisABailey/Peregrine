# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# geo_tool: geodesy utilities (distance, bounds, mercator, magnetic variation,
# coordinate strings) compiled in place. The COM twin fvw_core/GeoTool stays
# Windows-only. coordstr.cpp/milgrid.cpp call CGeoTrans, hence fv_geo3.

set(GEO_TOOL_DIR ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool)

add_library(fv_geo_tool STATIC
  ${GEO_TOOL_DIR}/bound.cpp
  ${GEO_TOOL_DIR}/check.cpp
  ${GEO_TOOL_DIR}/coordstr.cpp
  ${GEO_TOOL_DIR}/distance.cpp
  ${GEO_TOOL_DIR}/east_of.cpp
  ${GEO_TOOL_DIR}/enclose.cpp
  ${GEO_TOOL_DIR}/geodist.cpp
  ${GEO_TOOL_DIR}/geomag.cpp
  ${GEO_TOOL_DIR}/mercator.cpp
  ${GEO_TOOL_DIR}/milgrid.cpp
  ${GEO_TOOL_DIR}/overlap.cpp
  ${GEO_TOOL_DIR}/range.cpp
)
target_include_directories(fv_geo_tool PUBLIC ${GEO_TOOL_DIR})
# COMPILING_GEO_TOOL enables the ERR_report no-op macro in Stdafx.h (on
# Windows it also selects dllexport; harmless here).
target_compile_definitions(fv_geo_tool PRIVATE COMPILING_GEO_TOOL)
target_link_libraries(fv_geo_tool PUBLIC fv_geo3 fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_geo_tool_test test/geo_tool_test.cpp)
  target_link_libraries(fv_geo_tool_test PRIVATE fv_geo_tool GTest::gtest_main)
  gtest_discover_tests(fv_geo_tool_test
    PROPERTIES ENVIRONMENT "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib")
endif()
