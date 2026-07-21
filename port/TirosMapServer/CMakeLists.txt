# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Portable TIROS frame georeferencing (fv::TirosFrame), extracted from the
# Windows-only CTirosFrameFile COM object. Filename -> series/col/row ->
# equal-arc WGS-84 bounds. Pixels come from the CJpeg reader (ImageLibCore).

add_library(fv_tiros_frame STATIC fv_tiros_frame.cpp)
target_include_directories(fv_tiros_frame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_tiros_frame PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_tiros_frame_test test/tiros_frame_test.cpp)
  target_link_libraries(fv_tiros_frame_test PRIVATE fv_tiros_frame GTest::gtest_main)
  gtest_discover_tests(fv_tiros_frame_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
