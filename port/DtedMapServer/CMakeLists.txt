# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# Portable DTED cell reader, extracted from CDtedMemoryMappedFile
# (DtedElevation.cpp). The COM service (CDted: caches, block fill, level
# fallback) stays Windows-only.

add_library(fv_dted STATIC fv_dted_cell.cpp)
target_include_directories(fv_dted PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_dted PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_dted_test test/dted_cell_test.cpp)
  target_link_libraries(fv_dted_test PRIVATE fv_dted GTest::gtest_main)
  gtest_discover_tests(fv_dted_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
