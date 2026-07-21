# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# ImageLib core, Phase 2b slice 1: the CJpeg wrapper class (jpeg/jpeg.cpp) —
# FalconView's JPEG file reader over the vendored IJG codec. Unlocks TIROS
# (.WLD files are JPEGs). Later slices: gif/, geotiff.cpp, Image.cpp, nitf/.

add_library(fv_imagelib_jpeg_wrapper STATIC
  ${CMAKE_SOURCE_DIR}/fvw_core/ImageLib/jpeg/jpeg.cpp
)
target_include_directories(fv_imagelib_jpeg_wrapper PUBLIC
  ${CMAKE_SOURCE_DIR}/fvw_core/ImageLib/jpeg
  ${CMAKE_SOURCE_DIR}/fvw_core/ImageLib
  ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool
)
# C++17 (project default) since 2026-07-15: jpeg.h's std::auto_ptr member is
# now std::unique_ptr<BYTE[]>. Links the vendored C++14 IJG codec (fv_jpeg) fine.
target_link_libraries(fv_imagelib_jpeg_wrapper PUBLIC fv_jpeg fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_imagelib_jpeg_wrapper_test test/jpeg_wrapper_test.cpp)
  target_link_libraries(fv_imagelib_jpeg_wrapper_test
    PRIVATE fv_imagelib_jpeg_wrapper GTest::gtest_main)
  gtest_discover_tests(fv_imagelib_jpeg_wrapper_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()

# --- slice 2: CGeoTiff reader (geotiff.cpp) + gif ---
set(ILC ${CMAKE_SOURCE_DIR}/fvw_core/ImageLib)

add_library(fv_imagelib_geotiff STATIC
  ${ILC}/geotiff.cpp
  ${ILC}/geotiff2.cpp
  ${ILC}/fid.cpp
  ${ILC}/image_d.cpp
  ${ILC}/transfrm.cpp
  ${ILC}/Util.cpp
  ${ILC}/MetadataStructs.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/svd.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/bdsvd.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/bidiagonal.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/blas.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/lq.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/qr.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/reflections.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/rotations.cpp
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/Libs/ap.cpp
  fv_imagelib_util_posix.cpp
)
target_include_directories(fv_imagelib_geotiff PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${ILC} ${ILC}/tiff ${ILC}/jpeg
  ${CMAKE_SOURCE_DIR}/fvw_core/geo_tool
  ${CMAKE_SOURCE_DIR}/fvw_core/geo3
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD
  ${CMAKE_SOURCE_DIR}/fvw_core/Utility/SVD/Libs
  ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/GeoTIFFMapServer
)
# C++17 since 2026-07-15 (auto_ptr in geotiff.h/image_d.h/geotiff2.cpp/StdAfx.h
# swept: unique_ptr<T[]> for verified array members, fvw_auto_ptr shim for the
# shared transfer-on-copy typedefs).
target_link_libraries(fv_imagelib_geotiff PUBLIC fv_imagelib_jpeg_wrapper fv_projsp fv_geo3 fv_compat)

add_library(fv_imagelib_gif STATIC
  ${ILC}/gif/cgif.cpp
  ${ILC}/gif/file_pov.cpp
  ${ILC}/gif/gif.cpp
  ${ILC}/gif/gifdecod.cpp
)
target_include_directories(fv_imagelib_gif PUBLIC ${ILC}/gif ${ILC})
# stays C++14: gif.cpp still uses the 'register' storage class (off the FvKit
# path; sweep when gif gets a consumer/test).
set_target_properties(fv_imagelib_gif PROPERTIES CXX_STANDARD 14)
target_link_libraries(fv_imagelib_gif PUBLIC fv_imagelib_geotiff fv_compat)

if(FVW_BUILD_TESTS)
  add_executable(fv_imagelib_geotiff_test test/geotiff_reader_test.cpp)
  target_link_libraries(fv_imagelib_geotiff_test
    PRIVATE fv_imagelib_geotiff fv_imagelib_gif GTest::gtest_main)
  gtest_discover_tests(fv_imagelib_geotiff_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
