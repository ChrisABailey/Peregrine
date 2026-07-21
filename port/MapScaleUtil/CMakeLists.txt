# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Portable map-scale utility, extracted from fvw_core/MapScaleUtil
# (CMapScaleUtil). The ATL COM wrapper stays Windows-only.

add_library(fv_map_scale_util STATIC fv_map_scale_util.cpp)
target_include_directories(fv_map_scale_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_map_scale_util PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_map_scale_util_test test/map_scale_util_test.cpp)
  target_link_libraries(fv_map_scale_util_test PRIVATE fv_map_scale_util GTest::gtest_main)
  gtest_discover_tests(fv_map_scale_util_test)
endif()
