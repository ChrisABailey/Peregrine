# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# ENC (IHO S-57) reader — vpf-geosym-plan phase E1.
#
# Port-native code, not an in-place compile: FalconView has no S-57 reader to
# port (Applications/GeoRect/adrg/iso.cpp is the ADRG-era ASCII-only ISO 8211
# library, and S-57 is almost entirely binary subfields), so plan section 7
# called for a spec-driven reader of our own. No third-party dependency.

add_library(fv_enc STATIC
  fv_iso8211.cpp
  fv_s57.cpp
)
target_include_directories(fv_enc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_enc PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_enc_test test/iso8211_test.cpp test/s57_test.cpp)
  target_link_libraries(fv_enc_test PRIVATE fv_enc GTest::gtest_main)
  gtest_discover_tests(fv_enc_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
