# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# ViewKit (fv::view) — the interactive map view behind every desktop shell:
# viewport and camera, input to camera, the per-group scale ladder, the
# preview transform and the render scheduler. Headless; port/desktop-plan.md §2a.

add_library(fv_viewkit STATIC
  fv_view_ladder.cpp
  fv_view_map_view.cpp
  fv_view_scheduler.cpp
  fv_view_viewport.cpp
)
target_include_directories(fv_viewkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(fv_viewkit PUBLIC fv_fvkit Threads::Threads)
target_compile_features(fv_viewkit PUBLIC cxx_std_17)

if(FVW_BUILD_TESTS)
  add_executable(fv_viewkit_test
    test/ladder_test.cpp
    test/map_view_test.cpp
    test/scheduler_test.cpp
    test/viewport_test.cpp
  )
  target_link_libraries(fv_viewkit_test PRIVATE fv_viewkit GTest::gtest_main)
  gtest_discover_tests(fv_viewkit_test)
endif()
