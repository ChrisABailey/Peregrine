# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# peregrine_core — the one archive the macOS app's Xcode project links: every
# static library in DeskKit's link closure, merged with Apple's libtool. Not
# part of ALL; build it with `cmake --build build --target peregrine_core`.
# The app itself is Peregrine.xcodeproj (port/desktop-plan.md §2d).
if(NOT APPLE OR FVW_IOS)
  return()
endif()

include(${CMAKE_SOURCE_DIR}/port/cmake/FvwStaticClosure.cmake)

add_library(peregrine_core_deps INTERFACE)
target_link_libraries(peregrine_core_deps INTERFACE fv_deskkit)

fvw_collect_static_libs(peregrine_core_deps PEREGRINE_STATIC_LIBS)

set(PEREGRINE_CORE_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libperegrine_core.a")
set(_lib_files "")
foreach(_l IN LISTS PEREGRINE_STATIC_LIBS)
  list(APPEND _lib_files "$<TARGET_FILE:${_l}>")
endforeach()

add_custom_command(
  OUTPUT ${PEREGRINE_CORE_ARCHIVE}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${PEREGRINE_CORE_ARCHIVE}
  COMMAND xcrun libtool -static -o ${PEREGRINE_CORE_ARCHIVE} ${_lib_files}
  DEPENDS ${PEREGRINE_STATIC_LIBS}
  COMMENT "Merging the Peregrine core into libperegrine_core.a"
  COMMAND_EXPAND_LISTS
  VERBATIM)
add_custom_target(peregrine_core DEPENDS ${PEREGRINE_CORE_ARCHIVE})
