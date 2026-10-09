# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# The application log (fvkit/log.h, fv_log_c.h): levels, sinks, the file sink.
# Below fvkit and fv_compat, so it links nothing of the port's own.
find_package(Threads REQUIRED)
add_library(fv_log STATIC fv_log.cpp fv_log_file.cpp)
target_include_directories(fv_log PUBLIC ${CMAKE_SOURCE_DIR}/port/include)
target_link_libraries(fv_log PUBLIC Threads::Threads)

if(FVW_BUILD_TESTS)
  add_executable(fv_log_test test/log_test.cpp)
  target_link_libraries(fv_log_test PRIVATE fv_log GTest::gtest_main)
  gtest_discover_tests(fv_log_test)
endif()
