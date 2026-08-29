# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

# FvKit — the new cross-platform API layers (L0 primitives, L1 format
# adapters, ...) over the ported readers. Interface contracts:
# port/fvkit-contracts.md. Headers live in port/include/fvkit/.

# GeoTIFF pixel adapter: its TU includes ImageLib's geotiff.h, which still
# uses std::auto_ptr, so this one library is pinned to C++14 (same reason as
# fv_imagelib_geotiff). Everything it exposes through fvkit headers is
# C++14/17-agnostic. Fold back into fv_fvkit after an auto_ptr->unique_ptr
# sweep of ImageLib.
# All C++17 as of the 2026-07-15 auto_ptr sweep — the decoder-including source
# adapters no longer need a separate C++14 sub-library (folded back in here).
# System SQLite (macOS ships it with R-tree enabled) for the L2 catalog.
find_package(SQLite3 REQUIRED)
# CMake 4.4 renamed FindSQLite3's imported target to SQLite3::SQLite3 and
# deprecated SQLite::SQLite3; CMake 4.2 and older define only the old name.
# Ask for whichever this CMake actually provides.
if(TARGET SQLite3::SQLite3)
  set(FV_SQLITE3 SQLite3::SQLite3)
else()
  set(FV_SQLITE3 SQLite::SQLite3)
endif()

add_library(fv_fvkit STATIC
  formats/dted.cpp
  formats/dted_shaded_source.cpp
  formats/geotiff_enum.cpp
  formats/geotiff_source.cpp
  formats/cadrg_enum.cpp
  formats/cadrg_source.cpp
  formats/tiros_enum.cpp
  formats/tiros_source.cpp
  formats/vpf_enum.cpp
  formats/registry.cpp
  catalog/catalog.cpp
  canvas/cpu_canvas.cpp
  canvas/geo_draw.cpp
  canvas/label_placer.cpp
  geo/contour.cpp
  proj/map_projection.cpp
  settings.cpp
  scale_table.cpp
  engine/map_engine.cpp
  nav/position.cpp
  nav/heading.cpp
  nav/scripted_source.cpp
  nav/camera.cpp
  nav/camera_slew.cpp
  nav/road_snap.cpp
  nav/line_transport.cpp
  nav/nmea.cpp
  nav/gpx.cpp
  nav/gpx_recorder.cpp
  nav/trip.cpp
  overlay/manager.cpp
  overlay/grid_overlay.cpp
  overlay/grid_spacing.cpp
  overlay/point_overlay.cpp
  overlay/vector_map_overlay.cpp
  overlay/moving_map_overlay.cpp
  app/type_registry.cpp
  app/properties.cpp
  app/session.cpp
  app/editor.cpp
  app/pick.cpp
  app/search.cpp
  app/vector_hit_test.cpp
  store/tile_pack.cpp
  vector/renderer.cpp
  vector/symbol_draw.cpp
  vector/text_draw.cpp
  symbol/library_composite.cpp
  symbol/builtin.cpp
  symbol/png_library.cpp
  vector/pick.cpp
  vector/feature_rows.cpp
  vector/rules.cpp
  vector/families.cpp
  vector/scene.cpp
  vector/lookup_engine.cpp
)
target_include_directories(fv_fvkit PUBLIC ${CMAKE_SOURCE_DIR}/port/include)
# stb_truetype (vendored single header, public domain) for CpuCanvas text
target_include_directories(fv_fvkit PRIVATE ${CMAKE_SOURCE_DIR}/port/vendor)
# nlohmann/json (header-only) for vector/families.cpp. PRIVATE: no fvkit
# header exposes a json type, so this dependency stops at the .cpp.
target_link_libraries(fv_fvkit PRIVATE fv_json)
# expat (header-only from fvkit's point of view: PRIVATE, no fvkit header
# names an XML type) for nav/gpx.cpp. A GPX file comes off the open internet,
# so it is parsed by the modern vendored expat and never by hand — same rule
# as §2e's gate on network-facing input.
target_link_libraries(fv_fvkit PRIVATE expat)
target_link_libraries(fv_fvkit
  PUBLIC fv_dted fv_dted_shaded fv_imagelib_geotiff fv_geotiff_frame
         fv_cadrg fv_cadrg_frame
         fv_imagelib_jpeg_wrapper fv_tiros_frame fv_vpf fv_map_scale_util
         fv_map_series_string_converter
         fv_png fv_geo_tool ${FV_SQLITE3})
target_compile_features(fv_fvkit PUBLIC cxx_std_17)

# The CLI tools are HOST tools: they make the data a phone reads (fvrender
# proves a cut, fvpack builds a pyramid), so an iOS cross-build skips them.
if(NOT FVW_IOS)
  # fvrender — headless map renderer CLI (the L3b milestone demo)
  add_executable(fvrender tools/fvrender.cpp)
  target_link_libraries(fvrender PRIVATE fv_fvkit fv_png fv_geo_tool)
  # fvpack — offline GeoPackage tile-pyramid builder (L5 milestone)
  add_executable(fvpack tools/fvpack.cpp)
  target_link_libraries(fvpack PRIVATE fv_fvkit)
endif()

if(FVW_BUILD_TESTS)
  add_executable(fv_fvkit_test
    test/geo_test.cpp
    test/geo_contour_test.cpp
    test/geo_draw_test.cpp
    test/dted_adapter_test.cpp
    test/dted_shaded_adapter_test.cpp
    test/geotiff_adapter_test.cpp
    test/cadrg_adapter_test.cpp
    test/tiros_adapter_test.cpp
    test/catalog_test.cpp
    test/canvas_test.cpp
    test/proj_engine_test.cpp
    test/overlay_test.cpp
    test/overlay_toolkit_test.cpp
    test/grid_overlay_test.cpp
    nav/test/nav_position_test.cpp
    nav/test/nav_heading_test.cpp
    nav/test/nav_scripted_source_test.cpp
    nav/test/nav_camera_test.cpp
    nav/test/nav_camera_slew_test.cpp
    nav/test/nav_road_snap_test.cpp
    nav/test/nav_line_transport_test.cpp
    nav/test/nav_nmea_test.cpp
    nav/test/nav_gpx_test.cpp
    nav/test/nav_gpx_writer_test.cpp
    nav/test/nav_trip_test.cpp
    app/test/app_type_registry_test.cpp
    app/test/app_stack_test.cpp
    app/test/app_session_test.cpp
    app/test/app_editor_test.cpp
    app/test/app_pick_test.cpp
    app/test/app_search_test.cpp
    test/point_overlay_test.cpp
    test/vector_map_overlay_test.cpp
    test/feature_rows_test.cpp
    test/moving_map_overlay_test.cpp
    test/tile_pack_test.cpp
    test/vpf_adapter_test.cpp
    test/vector_renderer_test.cpp
    symbol/test/symbol_library_test.cpp
    test/vector_pick_test.cpp
    test/vector_rules_test.cpp
    test/vector_families_test.cpp
    test/vector_lookup_engine_test.cpp
    test/vector_scene_test.cpp
    test/settings_test.cpp
  )
  # fv_png: golden-PNG side channel in canvas_test (ported libpng)
  target_link_libraries(fv_fvkit_test PRIVATE fv_fvkit fv_png GTest::gtest_main)
  # The shipped data-family starter files, read at RUN time by the test that
  # checks they parse. Same pattern as FV_ROUTE_RULES_FILE in port/Routing.
  target_compile_definitions(fv_fvkit_test PRIVATE
    FV_FAMILIES_DNC="${CMAKE_SOURCE_DIR}/port/families/dnc-families.json"
    FV_FAMILIES_ENC="${CMAKE_SOURCE_DIR}/port/families/enc-families.json"
    FV_FAMILIES_OSM="${CMAKE_SOURCE_DIR}/port/families/osm-families.json")
  # MSPCCS_DATA: GEOTRANS data for the GeoTIFF datum shift (see
  # GeoTIFFMapServer/CMakeLists.txt; escaped list separator)
  gtest_discover_tests(fv_fvkit_test
    PROPERTIES ENVIRONMENT
    "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib\;FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
