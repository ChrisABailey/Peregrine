# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# RouteKit — the route document, the planner and the overlay (Pippin plan P5).
#
# THE MODULE EXISTS BECAUSE OF ONE INVARIANT: fvkit does not link
# port/Routing. It is stated twice in the ledger and it is what keeps the map
# engine out of `fvgraph` and the router out of every shell that only draws a
# chart. A route overlay needs BOTH — GeoDraw and the app layer from fvkit, the
# graph and the router from Routing — so it lives here, beside them rather than
# under either. `port/apps/Pippin` already links its two the same way.
#
# Dependencies: fv_fvkit, fv_routing, and nlohmann/json for READING `.fvrte`
# (the writer is hand-rolled — see fv_route_doc.h on why byte-compatibility
# with route.py's `json.dump` is not something a generic serializer provides).

add_library(fv_routekit STATIC
  fv_road_graph_overlay.cpp
  fv_route_doc.cpp
  fv_route_edit.cpp
  fv_route_overlay.cpp
  fv_route_planner.cpp
)
target_include_directories(fv_routekit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_routekit
  PUBLIC fv_fvkit fv_routing
  PRIVATE fv_json)
target_compile_features(fv_routekit PUBLIC cxx_std_17)

if(FVW_BUILD_TESTS)
  add_executable(fv_routekit_test
    test/road_graph_overlay_test.cpp
    test/road_graph_search_test.cpp
    test/route_doc_test.cpp
    test/route_edit_test.cpp
    test/route_overlay_test.cpp
    test/route_search_test.cpp
    test/route_planner_test.cpp
  )
  target_link_libraries(fv_routekit_test PRIVATE fv_routekit GTest::gtest_main)
  # The shipped cost rules, and the `.fvrte` route.py wrote: both are FIXTURES
  # here, and both live where their own module keeps them rather than being
  # copied into this one.
  target_compile_definitions(fv_routekit_test PRIVATE
    FV_ROUTE_RULES_FILE="${CMAKE_SOURCE_DIR}/port/Routing/rules/route-weights.json"
    FV_ROUTE_FIXTURE_FILE="${CMAKE_SOURCE_DIR}/port/Routing/rules/Ruddy Turnstone to the beachclub.fvrte")
  gtest_discover_tests(fv_routekit_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
