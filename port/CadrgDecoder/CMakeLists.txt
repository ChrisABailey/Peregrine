# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# CADRG/CIB RPF frame decoder (ImageLib/cadrg), compiled in place: NITF/RPF
# header parsing, VQ decompression (rpf_comp), palette LUTs, frame->pixels
# (RPFRenderer). NitfFileHeader.cpp is excluded (separate _bstr_t/_tfsopen
# utility, not part of the frame-decode path).

set(CADRG ${CMAKE_SOURCE_DIR}/fvw_core/ImageLib/cadrg)

add_library(fv_cadrg STATIC
  ${CADRG}/cdb_base.cpp
  ${CADRG}/imgdisp.cpp
  ${CADRG}/io.cpp
  ${CADRG}/nitf.cpp
  ${CADRG}/polar_utils.cpp
  ${CADRG}/rpf_comp.cpp
  ${CADRG}/rpf_util.cpp
  ${CADRG}/RpfZoneScales.cpp
)
target_include_directories(fv_cadrg PUBLIC ${CADRG} ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool)
# C++17 since 2026-07-15 (the lone 'register' in cdb_base.cpp was removed).
target_link_libraries(fv_cadrg PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_cadrg_test test/cadrg_test.cpp)
  target_link_libraries(fv_cadrg_test PRIVATE fv_cadrg GTest::gtest_main)
  gtest_discover_tests(fv_cadrg_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
