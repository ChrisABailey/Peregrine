# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# NetNMEA sentence parser (NMEA_sentence in nmea.cpp), compiled in place.
# Windows-only and NOT ported: gps.cpp (trail machinery), comm.cpp (serial
# I/O), poller.cpp (threads), INetNMEA.cpp / SentenceReader.cpp (COM feed).
# Tests are the module's own suite from fvw_core/NetNMEA_unit_tests.

add_library(fv_netnmea STATIC
  ${CMAKE_SOURCE_DIR}/fvw_core/NetNMEA/nmea.cpp
  fv_netnmea_missing.cpp
)
target_include_directories(fv_netnmea PUBLIC
  ${CMAKE_SOURCE_DIR}/fvw_core/NetNMEA
  ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool
)
target_link_libraries(fv_netnmea PUBLIC fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_netnmea_test
    ${CMAKE_SOURCE_DIR}/fvw_core/NetNMEA_unit_tests/nmea_feed_unit_tests.cpp
    test/oledatetime_test.cpp
  )
  target_link_libraries(fv_netnmea_test PRIVATE fv_netnmea GTest::gtest_main)
  gtest_discover_tests(fv_netnmea_test)
endif()
