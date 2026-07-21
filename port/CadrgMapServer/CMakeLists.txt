# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Portable CADRG/RPF frame georeferencing (fv::CadrgFrame), extracted from the
# Windows-only CRpfFrameFile COM object. Reuses CArcZone/PolarUtil/RPFHelper
# from the ported decoder (fv_cadrg). The COM surface + SQL catalog scale
# lookup stay Windows-only; scale comes from a built-in MIL-STD-2411A table.

add_library(fv_cadrg_frame STATIC fv_cadrg_frame.cpp)
target_include_directories(fv_cadrg_frame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_cadrg_frame PUBLIC fv_cadrg fv_compat)

if(FVW_BUILD_TESTS)
  # C++17 (the public header is clean std::string; <filesystem> in the test);
  # links the C++14 lib fine (same toolchain/ABI, as fvkit does).
  add_executable(fv_cadrg_frame_test test/cadrg_frame_test.cpp)
  target_link_libraries(fv_cadrg_frame_test
    PRIVATE fv_cadrg_frame GTest::gtest_main)
  gtest_discover_tests(fv_cadrg_frame_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
