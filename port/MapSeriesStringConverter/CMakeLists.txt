# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Portable map-series string formatter, extracted from
# fvw_core/MapSeriesStringConverter (CMapSeriesStringConverter).
# The ATL COM wrapper (BSTR/VARIANT surface) stays Windows-only.

add_library(fv_map_series_string_converter STATIC fv_map_series_string_converter.cpp)
target_include_directories(fv_map_series_string_converter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_map_series_string_converter PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_map_series_string_converter_test test/map_series_string_converter_test.cpp)
  target_link_libraries(fv_map_series_string_converter_test
    PRIVATE fv_map_series_string_converter GTest::gtest_main)
  gtest_discover_tests(fv_map_series_string_converter_test)
endif()
