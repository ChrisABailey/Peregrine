# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# ENC (IHO S-57 data + S-52 symbology) — vpf-geosym-plan phases E1 and E2.
#
# Port-native code, not an in-place compile: FalconView has no S-57 reader to
# port (Applications/GeoRect/adrg/iso.cpp is the ADRG-era ASCII-only ISO 8211
# library, and S-57 is almost entirely binary subfields), so plan section 7
# called for a spec-driven reader of our own.
#
# E2 adds the S-52 presentation library and the S-57 Appendix A catalogue, so
# this target now links expat (the CURRENT release from port/third_party, not
# the 2012 in-tree fork — see the dependency-modernization table) and fvkit for
# the VectorSymbol seam symbol display lists are expressed in.

add_library(fv_enc STATIC
  fv_iso8211.cpp
  fv_s57.cpp
  fv_s52_preslib.cpp
  fv_s57_catalog.cpp
  fv_enc_vector_source.cpp
  fv_s52_style.cpp
  fv_enc_format.cpp
)
target_include_directories(fv_enc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_enc PUBLIC fv_compat fv_fvkit PRIVATE expat)

if(FVW_BUILD_TESTS)
  add_executable(fv_enc_test
    test/iso8211_test.cpp
    test/s57_test.cpp
    test/s52_preslib_test.cpp
    test/s57_catalog_test.cpp
    test/enc_vector_source_test.cpp
    test/s52_style_test.cpp
    test/enc_format_test.cpp
  )
  # fv_png: golden-PNG side channel for the E3a chart render, same
  # convention as canvas_test and the GeoSym harbor golden.
  target_link_libraries(fv_enc_test PRIVATE fv_enc fv_png GTest::gtest_main)
  gtest_discover_tests(fv_enc_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
