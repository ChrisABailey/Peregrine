# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Routable road graph + shortest path (O4).
#
# Port-native, like port/Osm and port/Enc: FalconView has no route planner to
# port. The input is a RAW OSM extract, not the MVT pyramid port/Osm reads —
# vector tiles are simplified and tile-clipped, so there is no topology in
# them to route on.
#
# Dependencies: protozero (the .osm.pbf wire format), zlib (its blob
# compression), expat (the .osm XML form), nlohmann/json (the O5c cost rule
# file). All four are already fetched in port/third_party for other modules.

add_library(fv_routing STATIC
  fv_osm_reader.cpp
  fv_road_graph.cpp
  fv_route_rules.cpp
  fv_router.cpp
)
target_include_directories(fv_routing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_routing
  PUBLIC fv_compat
  PRIVATE fv_protozero fv_zlib expat fv_json)

target_compile_features(fv_routing PUBLIC cxx_std_17)

# Where the shipped cost rules live. The file is read at RUN time and reread
# when it changes — this only tells fvgraph and the test where to look for it,
# it does not compile it in.
set(FV_ROUTE_RULES_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rules/route-weights.json")

# fvgraph — offline graph builder + route query CLI, the fvpack pattern.
add_executable(fvgraph tools/fvgraph.cpp)
target_link_libraries(fvgraph PRIVATE fv_routing)
target_compile_definitions(fvgraph PRIVATE FV_ROUTE_RULES_FILE="${FV_ROUTE_RULES_FILE}")

if(FVW_BUILD_TESTS)
  add_executable(fv_routing_test
    test/osm_reader_test.cpp
    test/road_graph_test.cpp
    test/route_rules_test.cpp
    test/router_test.cpp
  )
  # fv_zlib: osm_reader_test encodes its own .osm.pbf fixture, so it needs the
  # compressor the reader only ever inflates with.
  target_link_libraries(fv_routing_test PRIVATE fv_routing fv_zlib GTest::gtest_main)
  target_compile_definitions(fv_routing_test PRIVATE
    FV_ROUTE_RULES_FILE="${FV_ROUTE_RULES_FILE}")
  gtest_discover_tests(fv_routing_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
