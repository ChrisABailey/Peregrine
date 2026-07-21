# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# CGeoTrans wrapper (fvw_core/geo3) compiled in place over MSP GEOTRANS.
# geo3.rc / .def / resource plumbing is Windows-only and not built here.

add_library(fv_geo3 STATIC
  ${CMAKE_SOURCE_DIR}/fvw_core/geo3/geo3.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/geo3/GEOTRANS.CPP
)
target_include_directories(fv_geo3 PUBLIC ${CMAKE_SOURCE_DIR}/fvw_core/geo3)
target_link_libraries(fv_geo3 PUBLIC msp_ccs fv_compat)

if(FVW_BUILD_TESTS)
  # geotrans_unittest.cpp is the module's own pinned-value suite; its tests are
  # DISABLED_ upstream (build machines lacked the GEOTRANS data), so run with
  # --gtest_also_run_disabled_tests and point MSPCCS_DATA at the data files
  # that ship inside the repo.
  add_executable(fv_geo3_test
    test/geo3_test.cpp
    ${CMAKE_SOURCE_DIR}/fvw_core/geo3/geotrans_unittest.cpp
  )
  target_link_libraries(fv_geo3_test PRIVATE fv_geo3 GTest::gtest_main)
  gtest_discover_tests(fv_geo3_test
    PROPERTIES ENVIRONMENT "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib")

  # ctest skips DISABLED_ tests even with the flag in EXTRA_ARGS, so run the
  # pinned upstream suite as one explicit test. datum_ellipsoid_name is
  # excluded: our ellips.dat revision (PdfLib SDK copy) spells ellipsoid names
  # differently than the FalconView-install data the test was written against
  # (see PORTING.md blockers).
  add_test(NAME GeoTransPinnedSuite
    COMMAND fv_geo3_test --gtest_also_run_disabled_tests
      --gtest_filter=GeoTransTest.*:-GeoTransTest.DISABLED_datum_ellipsoid_name)
  set_tests_properties(GeoTransPinnedSuite PROPERTIES
    ENVIRONMENT "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib")
endif()
