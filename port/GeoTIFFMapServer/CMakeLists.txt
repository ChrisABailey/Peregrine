# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# GeoTIFF frame reader: CGeoTiffFrameFile compiled in place (its own TIFF/
# GeoKey parser, no libtiff). COM surface + ImageLib/MrSID fallback are
# Windows-only; fv::GeoTiffFrame is the portable orchestrator, with datum
# shifts through fv::IDatumConvert backed by the ported GEOTRANS (fv_geo3).

# State-plane projection support, shared with ImageLib's CGeoTiff reader
add_library(fv_projsp STATIC
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer/projsp.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer/codes.cpp
)
target_include_directories(fv_projsp PUBLIC
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer
  ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool
)
target_link_libraries(fv_projsp PUBLIC fv_compat)

add_library(fv_geotiff_frame STATIC
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer/GeoTiffFrameFile.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer/GeoTiff_defines.cpp
  fv_geotiff_frame.cpp
  fv_datum_convert_geotrans.cpp
)
target_include_directories(fv_geotiff_frame PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer
  ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool
)
target_link_libraries(fv_geotiff_frame PUBLIC fv_projsp fv_geo3 fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_geotiff_frame_test test/geotiff_frame_test.cpp)
  target_link_libraries(fv_geotiff_frame_test PRIVATE fv_geotiff_frame GTest::gtest_main)
  # two ENVIRONMENT entries need an escaped list separator
  gtest_discover_tests(fv_geotiff_frame_test
    PROPERTIES ENVIRONMENT
    "MSPCCS_DATA=${CMAKE_SOURCE_DIR}/fvw_core/PdfLib/sdk/lib\;FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
