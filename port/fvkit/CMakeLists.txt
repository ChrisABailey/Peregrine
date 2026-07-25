# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

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
  proj/map_projection.cpp
  engine/map_engine.cpp
  overlay/manager.cpp
  overlay/grid_overlay.cpp
  store/tile_pack.cpp
  vector/renderer.cpp
  vector/pick.cpp
)
target_include_directories(fv_fvkit PUBLIC ${CMAKE_SOURCE_DIR}/port/include)
# stb_truetype (vendored single header, public domain) for CpuCanvas text
target_include_directories(fv_fvkit PRIVATE ${CMAKE_SOURCE_DIR}/port/vendor)
target_link_libraries(fv_fvkit
  PUBLIC fv_dted fv_dted_shaded fv_imagelib_geotiff fv_geotiff_frame
         fv_cadrg fv_cadrg_frame
         fv_imagelib_jpeg_wrapper fv_tiros_frame fv_vpf fv_map_scale_util
         fv_png SQLite::SQLite3)
target_compile_features(fv_fvkit PUBLIC cxx_std_17)

# fvrender — headless map renderer CLI (the L3b milestone demo)
add_executable(fvrender tools/fvrender.cpp)
target_link_libraries(fvrender PRIVATE fv_fvkit fv_png fv_geo_tool)
# fvpack — offline GeoPackage tile-pyramid builder (L5 milestone)
add_executable(fvpack tools/fvpack.cpp)
target_link_libraries(fvpack PRIVATE fv_fvkit)

if(FVW_BUILD_TESTS)
  add_executable(fv_fvkit_test
    test/geo_test.cpp
    test/dted_adapter_test.cpp
    test/dted_shaded_adapter_test.cpp
    test/geotiff_adapter_test.cpp
    test/cadrg_adapter_test.cpp
    test/tiros_adapter_test.cpp
    test/catalog_test.cpp
    test/canvas_test.cpp
    test/proj_engine_test.cpp
    test/overlay_test.cpp
    test/tile_pack_test.cpp
    test/vpf_adapter_test.cpp
    test/vector_renderer_test.cpp
    test/vector_pick_test.cpp
  )
  # fv_png: golden-PNG side channel in canvas_test (ported libpng)
  target_link_libraries(fv_fvkit_test PRIVATE fv_fvkit fv_png GTest::gtest_main)
  # MSPCCS_DATA: GEOTRANS data for the GeoTIFF datum shift (see
  # GeoTIFFMapServer/CMakeLists.txt; escaped list separator)
  gtest_discover_tests(fv_fvkit_test
    PROPERTIES ENVIRONMENT
    "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib\;FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
