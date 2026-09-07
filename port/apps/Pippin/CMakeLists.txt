# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# pippin_core — the one library the iOS app links (Pippin plan, P1).
#
# THE PORT IS FIFTEEN STATIC LIBRARIES WITH A DEPENDENCY ORDER, and an Xcode
# target that has to name them all in the right order is a maintenance
# hazard living outside CMake, where nothing checks it. So this directory
# produces exactly two things:
#
#   * `pippin_core_deps`, an INTERFACE target — the dependency, for anything
#     inside this build that wants to link the core the CMake way.
#   * `libpippin_core.a`, ONE archive with every object in it, merged by
#     Apple's own `libtool -static`. That is what PippinKit's Xcode target
#     links in P2, and it is why P1 can claim "the libraries link" as its
#     acceptance: producing this file requires every one of them to have
#     compiled for the phone's architecture.
#
# AND THIS DIRECTORY IS ALSO ADDED ON THE MAC, for one target: the gtest over
# the pure-C++ half of PippinKit (P4's CoreLocation sentinel table). The plan's
# standing rule is that the mac is the test bed for every line of this C++, so
# a piece of Pippin that can be tested has to be reachable from the mac build;
# everything iOS is under `if(FVW_IOS)` and the mac build configures none of it.
if(NOT FVW_IOS)
  if(FVW_BUILD_TESTS)
    # PippinKit's public headers are Objective-C and its private ones are not.
    # `PPLocationFix.h`, `PPCameraFit.h` and `PPRouteStore.{h,cpp}` are the
    # private, `std::`-only kind — no CoreLocation and no Foundation in them —
    # which is exactly what makes this target possible. P6 put the route
    # document's whole lifecycle in that half deliberately, so "relaunch and the
    # route is still there" is a gtest rather than something to remember to try
    # in the simulator, and P7 put the zoom-out rule there for the same reason.
    # P9's `PPPointStore.{h,cpp}` is the same kind again — the seed, the write
    # per edit and the nearest-wins hit test, none of which needs a screen.
    # P18's `PPBaseCoverage.h` is the newest: whether the cached base map still
    # covers the live camera is two projections and some arithmetic, and a
    # cache that says yes once too often draws the WRONG MAP — a failure that
    # is a feeling in the simulator and an equality here.
    add_executable(fv_pippin_test
      test/base_coverage_test.cpp
      test/camera_fit_test.cpp
      test/follow_cadence_test.cpp
      test/location_fix_test.cpp
      test/point_store_test.cpp
      test/route_store_test.cpp
      test/search_rules_test.cpp
      PippinKit/PPPointStore.cpp
      PippinKit/PPRouteStore.cpp)
    target_include_directories(fv_pippin_test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/PippinKit)
    # RouteKit is linked BESIDE fvkit for the invariant this directory's iOS
    # half already states: fvkit does not link port/Routing, and a route needs
    # both.
    target_link_libraries(fv_pippin_test PRIVATE fv_fvkit fv_routekit
      GTest::gtest_main)
    # The shipped cost rules, as a fixture — the same file the pack stages, kept
    # where its own module keeps it rather than copied here.
    target_compile_definitions(fv_pippin_test PRIVATE
      FV_ROUTE_RULES_FILE="${CMAKE_SOURCE_DIR}/port/Routing/rules/route-weights.json"
      # The pack's own point set, so the test can read the file stage_data.py
      # WROTE rather than one it built itself. That is the only check anywhere
      # that the Python schema in the staging script and the C++ reader still
      # agree — see the note over `POINTS_SCHEMA`. The pack is git-ignored, so
      # a tree that has never staged skips rather than fails.
      FV_PIPPIN_POINTS_SEED="${CMAKE_CURRENT_SOURCE_DIR}/Data/kiawah.fvpoints")
    gtest_discover_tests(fv_pippin_test
      PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")

    # P14's share parser is Swift, so it cannot be a case in the gtest above
    # — but it is the same KIND of thing (a pure function over strings, with
    # no screen anywhere near it) and the plan's rule is that those are tested
    # off the phone. The script compiles `PlaceLink.swift` against its table
    # of real share URLs and runs it. Only on a mac, and only when there is a
    # Swift compiler to do it with; a Linux tree simply has one test fewer.
    #
    # P16's render gate is the second one: also Swift, also a pure function
    # with one bit of state, and its two real failure modes (a ship that never
    # wakes the loop again, a chevron left pinned to the edge of a stale frame)
    # are both invisible in a screenshot. They are cases in the table instead.
    #
    # P17's accuracy policy is the third and has the strongest claim of the
    # three: the hazard it exists to avoid IS its state machine (a receiver
    # flapping between tiers at the viewport edge costs more than no policy at
    # all), and neither that nor a receiver left coarse can be seen on a screen.
    if(APPLE)
      find_program(FVW_XCRUN xcrun)
      if(FVW_XCRUN)
        add_test(NAME pippin_place_link
          COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/test/run_place_link_test.sh
                  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
        add_test(NAME pippin_render_gate
          COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/test/run_render_gate_test.sh
                  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
        add_test(NAME pippin_location_policy
          COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/test/run_location_policy_test.sh
                  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
      endif()
    endif()
  endif()
  return()
endif()

# ROUTING IS LINKED BESIDE FVKIT, NOT UNDER IT. The ledger's invariant is
# that fvkit does not link port/Routing (stated twice; it is what keeps the
# map engine out of fvgraph). The app layer is exactly the one allowed to
# have both — and P5's RouteKit is exactly that: it links fvkit AND Routing,
# which is why it is a module of its own and why it belongs on this line.
add_library(pippin_core_deps INTERFACE)
target_link_libraries(pippin_core_deps INTERFACE fv_fvkit fv_osm fv_routing fv_routekit)

# Walk the link graph and collect every static library in it. CMake has no
# "give me the transitive closure" query, so this is the closure — visited
# set and all — computed at configure time. Anything that is not a static
# library we build (an imported .tbd like SQLite, a `-framework` flag, an
# INTERFACE shim like fv_compat) is NOT an archive and is skipped: those are
# link-line arguments, recorded below for the Xcode target to repeat.
function(fvw_collect_static_libs target out_var)
  set(_seen "")
  set(_stack "${target}")
  set(_libs "")
  while(_stack)
    list(POP_FRONT _stack _t)
    if(NOT TARGET ${_t})
      continue()
    endif()
    if(_t IN_LIST _seen)
      continue()
    endif()
    list(APPEND _seen ${_t})
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "STATIC_LIBRARY")
      list(APPEND _libs ${_t})
    endif()
    foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_deps ${_t} ${_prop})
      if(_deps)
        foreach(_d IN LISTS _deps)
          # Strip the $<LINK_ONLY:...> wrapper PRIVATE deps arrive in.
          string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" _d "${_d}")
          if(TARGET ${_d})
            list(APPEND _stack ${_d})
          endif()
        endforeach()
      endif()
    endforeach()
  endwhile()
  set(${out_var} "${_libs}" PARENT_SCOPE)
endfunction()

fvw_collect_static_libs(pippin_core_deps PIPPIN_STATIC_LIBS)
list(LENGTH PIPPIN_STATIC_LIBS _n)
message(STATUS "pippin_core: merging ${_n} static libraries")

set(PIPPIN_CORE_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libpippin_core.a")
set(_lib_files "")
foreach(_l IN LISTS PIPPIN_STATIC_LIBS)
  list(APPEND _lib_files "$<TARGET_FILE:${_l}>")
endforeach()

# `libtool -static` is the Apple-supported way to merge archives; `ar` would
# drop duplicate member names silently (several modules have a `util.cpp`).
# Through `xcrun` so it is the toolchain's own, and with no extra flags:
# Xcode 26's libtool no longer takes -no_warning_for_no_symbol_table.
add_custom_command(
  OUTPUT ${PIPPIN_CORE_ARCHIVE}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${PIPPIN_CORE_ARCHIVE}
  COMMAND xcrun libtool -static -o ${PIPPIN_CORE_ARCHIVE} ${_lib_files}
  DEPENDS ${PIPPIN_STATIC_LIBS}
  COMMENT "Merging the Peregrine core into libpippin_core.a"
  COMMAND_EXPAND_LISTS
  VERBATIM)
add_custom_target(pippin_core ALL DEPENDS ${PIPPIN_CORE_ARCHIVE})

# The link-line arguments that are NOT in the archive, written where the
# Xcode target can read them rather than rediscovered by a link failure.
# SQLite is the SDK's (fv_fvkit's find_package resolves to libsqlite3.tbd on
# iOS), and CoreFoundation is what that build of SQLite calls into. There is
# no UIKit and no Foundation here — nothing below PippinKit knows iOS exists,
# which is the plan's boundary rule.
file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pippin_core_link_flags.txt"
  CONTENT "-lsqlite3 -lz -framework CoreFoundation\n")

# The link check. Built for iOS only — on the mac every one of these calls is
# already under ctest, and it is the CROSS link this exists to prove.
if(FVW_IOS)
  add_executable(pippin_link_check link_check.cpp)
  target_link_libraries(pippin_link_check PRIVATE pippin_core_deps)
  # No bundle, no Info.plist, no signing: a plain Mach-O the simulator can
  # spawn. A .app is P2's business.
  set_target_properties(pippin_link_check PROPERTIES MACOSX_BUNDLE OFF)
endif()
