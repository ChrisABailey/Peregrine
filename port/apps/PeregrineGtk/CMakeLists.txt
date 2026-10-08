# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# peregrine-gtk — the Linux desktop app, a GTK4 (gtkmm-4) shell over
# DeskKit's DeskHost (port/linux-plan.md). Skipped with a message when
# gtkmm-4.0 is not installed, so the core still builds and tests without it.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GTKMM4 QUIET IMPORTED_TARGET gtkmm-4.0>=4.10)
endif()
if(NOT GTKMM4_FOUND)
  message(STATUS "peregrine-gtk: gtkmm-4.0 >= 4.10 not found; the Linux app is not built")
  return()
endif()

add_executable(peregrine-gtk
  application.cpp
  main.cpp
  main_menu.cpp
  map_canvas.cpp
  map_window.cpp
)
target_link_libraries(peregrine-gtk PRIVATE fv_deskkit PkgConfig::GTKMM4)
# The core links the frozen libpng 1.2 fork (fv_png) statically. Without this
# its png_* symbols are exported from the executable and interpose the system
# libpng16 that GTK loads, which crashes the first PNG GTK reads or writes.
target_link_options(peregrine-gtk PRIVATE "LINKER:--exclude-libs,ALL")

# The menu, action, shortcut and frame-placement tests run without a display. The --shot
# tests need an X server, so they run under xvfb-run when that is present:
# one with an empty catalog, and one with a catalog built from testdata/.
if(FVW_BUILD_TESTS)
  add_executable(peregrine_gtk_test test/main_menu_test.cpp main_menu.cpp map_canvas.cpp)
  target_include_directories(peregrine_gtk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(peregrine_gtk_test PRIVATE fv_deskkit PkgConfig::GTKMM4 GTest::gtest_main)
  target_link_options(peregrine_gtk_test PRIVATE "LINKER:--exclude-libs,ALL")
  gtest_discover_tests(peregrine_gtk_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/testdata")

  add_executable(peregrine-gtk-build-catalog test/build_catalog.cpp)
  target_link_libraries(peregrine-gtk-build-catalog PRIVATE fv_deskkit)

  find_program(XVFB_RUN xvfb-run)
  if(XVFB_RUN)
    add_test(NAME peregrine_gtk.shot_empty_catalog
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:peregrine-gtk>
        -DXVFB_RUN=${XVFB_RUN}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/shot_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/shot_test.cmake)
    add_test(NAME peregrine_gtk.shot_testdata
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:peregrine-gtk>
        -DBUILD_CATALOG=$<TARGET_FILE:peregrine-gtk-build-catalog>
        -DDATA=${CMAKE_SOURCE_DIR}/testdata
        -DGEOTRANS=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib
        -DXVFB_RUN=${XVFB_RUN}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/shot_testdata
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/shot_testdata.cmake)
    set_tests_properties(peregrine_gtk.shot_testdata PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
  endif()
endif()
