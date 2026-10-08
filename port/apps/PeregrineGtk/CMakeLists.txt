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
  map_canvas.cpp
  map_window.cpp
)
target_link_libraries(peregrine-gtk PRIVATE fv_deskkit PkgConfig::GTKMM4)
# The core links the frozen libpng 1.2 fork (fv_png) statically. Without this
# its png_* symbols are exported from the executable and interpose the system
# libpng16 that GTK loads, which crashes the first PNG GTK reads or writes.
target_link_options(peregrine-gtk PRIVATE "LINKER:--exclude-libs,ALL")

# The --shot smoke test: an empty catalog renders and reports the empty
# state. Needs an X server, so it runs under xvfb-run when that is present.
if(FVW_BUILD_TESTS)
  find_program(XVFB_RUN xvfb-run)
  if(XVFB_RUN)
    add_test(NAME peregrine_gtk.shot_empty_catalog
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:peregrine-gtk>
        -DXVFB_RUN=${XVFB_RUN}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/shot_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/shot_test.cmake)
  endif()
endif()
