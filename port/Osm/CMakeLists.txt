# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# OSM vector tiles (MBTiles container + Mapbox Vector Tiles) — vpf-geosym-plan
# section 6, phase O1.
#
# Port-native code, like port/Enc: FalconView has no vector-tile reader to
# port. What it does have is the seam every other vector product now rides, so
# this target is deliberately thin — a container reader, a tile decoder, and an
# IVectorSource over the two.
#
# Dependencies are all header-only and all fetched in port/third_party:
# vtzero/protozero (MVT wire format), nlohmann/json (the `json` metadata
# value's vector_layers inventory). zlib is for the gzip framing MBTiles
# wraps each tile in; SQLite comes from fv_fvkit's own link line.

add_library(fv_osm STATIC
  fv_mvt.cpp
  fv_mbtiles.cpp
  fv_osm_vector_source.cpp
  fv_osm_style.cpp
  fv_osm_format.cpp
)
target_include_directories(fv_osm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fv_osm
  PUBLIC fv_compat fv_fvkit
  PRIVATE fv_vtzero fv_json fv_zlib)

if(FVW_BUILD_TESTS)
  add_executable(fv_osm_test
    test/web_mercator_test.cpp
    test/mvt_test.cpp
    test/mbtiles_test.cpp
    test/osm_vector_source_test.cpp
    test/osm_style_test.cpp
    test/osm_format_test.cpp
    test/osm_render_test.cpp
  )
  # fv_zlib: mvt_test builds its own gzip-framed tiles, so it needs the
  # compressor the reader only ever inflates with.
  # fv_png: golden-PNG side channel for the Atlanta render (O3), the same
  # convention as canvas_test, the GeoSym harbor golden and E3a's Charleston.
  target_link_libraries(fv_osm_test PRIVATE fv_osm fv_zlib fv_png GTest::gtest_main)
  # FVW_OSM_STYLE_DIR: the reference style ships with the source (styles/), not
  # with TestData — it is a deliverable of this module, not sample data.
  gtest_discover_tests(fv_osm_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData\;FVW_OSM_STYLE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/styles")
endif()
