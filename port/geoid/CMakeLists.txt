# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Portable geoid-separation library, extracted from fvw_core/geoid (CGeoid).
# The ATL COM wrapper in fvw_core/geoid remains Windows-only and untouched.

add_library(fv_geoid STATIC fv_geoid.cpp)
target_include_directories(fv_geoid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_geoid PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_geoid_test test/geoid_test.cpp)
  target_link_libraries(fv_geoid_test PRIVATE fv_geoid GTest::gtest_main)
  gtest_discover_tests(fv_geoid_test)
endif()
