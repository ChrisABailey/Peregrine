# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# ImageLib codec sub-libraries, compiled in place from fvw_core/ImageLib/*.
# These are 2010-era vendored forks (zlib 1.2.5, libpng, IJG jpeg 8/12-bit,
# libtiff 3.9.4, giflib); kept as-is rather than swapped for system libs so
# decode output matches the Windows build byte-for-byte.
#
# This directory covers ONLY the codecs (Phase 2a). The ImageLib core
# (Image.cpp, Dib.cpp, CString-heavy; cadrg/ and nitf/ which depend on it)
# is Phase 2b. Windows-only forever: kakadu/ (commercial SDK), gdi_plus.cpp.

set(IL ${CMAKE_SOURCE_DIR}/fvw_core/ImageLib)

# --- zlib ---
file(GLOB ZLIB_SRC ${IL}/zlib/*.c)
list(FILTER ZLIB_SRC EXCLUDE REGEX "example\\.c$")
add_library(fv_z STATIC ${ZLIB_SRC})
target_include_directories(fv_z PUBLIC ${IL}/zlib)
# The vendored copy calls MSVC CRT names (_open/_lseek/...) directly; map
# them back to POSIX without touching the sources.
target_compile_definitions(fv_z PRIVATE
  _open=open _lseek=lseek _read=read _write=write _close=close)
target_compile_options(fv_z PRIVATE -include unistd.h)
target_compile_options(fv_z PRIVATE -w)

# --- libpng (uses zlib) ---
file(GLOB PNG_SRC ${IL}/png/*.c)
# PngFile.c is a Windows file-dialog sample harness, not part of libpng
list(FILTER PNG_SRC EXCLUDE REGEX "PngFile\\.c$|pngtest\\.c$")
add_library(fv_png STATIC ${PNG_SRC})
target_include_directories(fv_png PUBLIC ${IL}/png)
# pngconf.h's TARGET_OS_MAC sniffing selects the Mac OS 9 <fp.h> branch on
# modern macOS; pre-include <math.h> and mark it so that branch is skipped.
target_compile_options(fv_png PRIVATE -include math.h)
target_link_libraries(fv_png PUBLIC fv_z)
target_compile_options(fv_png PRIVATE -w)

# --- IJG jpeg (8-bit) ---
file(GLOB JPEG_SRC ${IL}/jpeg/*.cpp ${IL}/jpeg/*.CPP)
list(REMOVE_DUPLICATES JPEG_SRC)  # APFS is case-insensitive; globs overlap
# CDJPEG.cpp is cjpeg/djpeg command-line-tool support (pulls ImageLib core
# stdafx); not part of the library
# Library proper = IJG J*.cpp files. Everything else is either cjpeg/djpeg
# tool code (CD/RD/WR*) or FalconView's CString wrapper class (jpeg.cpp,
# ported with the ImageLib core in Phase 2b).
list(FILTER JPEG_SRC INCLUDE REGEX "/[Jj][^/]*$")
list(FILTER JPEG_SRC EXCLUDE REGEX "/jpeg\\.cpp$")
add_library(fv_jpeg STATIC ${JPEG_SRC} fv_jpeg_posix.cpp)
target_include_directories(fv_jpeg PUBLIC ${IL}/jpeg ${IL})
target_link_libraries(fv_jpeg PUBLIC fv_compat)
set_target_properties(fv_jpeg PROPERTIES CXX_STANDARD 14)
target_compile_options(fv_jpeg PRIVATE -w)

# --- IJG jpeg (12-bit fork, *12-suffixed symbols) ---
file(GLOB JPEG12_SRC ${IL}/jpeg12/*.cpp ${IL}/jpeg12/*.CPP)
list(REMOVE_DUPLICATES JPEG12_SRC)
list(FILTER JPEG12_SRC INCLUDE REGEX "/[Jj][^/]*$")
list(FILTER JPEG12_SRC EXCLUDE REGEX "/jpeg12?\\.cpp$")
add_library(fv_jpeg12 STATIC ${JPEG12_SRC})
target_include_directories(fv_jpeg12 PUBLIC ${IL}/jpeg12 ${IL})
target_link_libraries(fv_jpeg12 PUBLIC fv_compat)
set_target_properties(fv_jpeg12 PROPERTIES CXX_STANDARD 14)
target_compile_options(fv_jpeg12 PRIVATE -w)

# --- libtiff (with POSIX glue replacing tif_win32.c) ---
file(GLOB TIFF_SRC ${IL}/tiff/*.c)
list(FILTER TIFF_SRC EXCLUDE REGEX "tif_win32\\.c$")
# tif_jpeg.c needs a plain-C libjpeg. The Windows tiff.vcxproj points at
# GDAL's bundled libjpeg (not FalconView's C++ jpeg fork) — mirror that.
file(GLOB GDAL_JPEG_SRC ${CMAKE_SOURCE_DIR}/third_party/gdal_1.9.1/frmts/jpeg/libjpeg/*.c)
list(FILTER GDAL_JPEG_SRC EXCLUDE REGEX "jmemmac|jmemdos|jmemname")
add_library(fv_gdal_jpeg STATIC ${GDAL_JPEG_SRC})
target_include_directories(fv_gdal_jpeg PUBLIC
  ${CMAKE_SOURCE_DIR}/third_party/gdal_1.9.1/frmts/jpeg/libjpeg)
target_compile_options(fv_gdal_jpeg PRIVATE -w)

add_library(fv_tiff STATIC ${TIFF_SRC} fv_tif_posix.c)
target_include_directories(fv_tiff PUBLIC ${IL}/tiff)
target_include_directories(fv_tiff PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/gdal_1.9.1/frmts/jpeg/libjpeg)
# tif_config.h spells 64-bit types as MSVC's (unsigned) __int64 keyword
target_compile_definitions(fv_tiff PRIVATE "__int64=long long")
target_link_libraries(fv_tiff PUBLIC fv_z fv_gdal_jpeg)
target_compile_options(fv_tiff PRIVATE -w)

if(FVW_BUILD_TESTS)
  # Two binaries: FalconView's C++ jpeg and GDAL's C jpeg (used by tiff)
  # both export unmangled jpeg symbols, as they may — on Windows they live
  # in different DLLs and never meet in one link.
  add_executable(fv_imagelib_codecs_test test/codecs_test.cpp)
  target_link_libraries(fv_imagelib_codecs_test
    PRIVATE fv_png fv_jpeg fv_jpeg12 GTest::gtest_main)
  gtest_discover_tests(fv_imagelib_codecs_test)

  add_executable(fv_tiff_test test/tiff_test.cpp)
  target_link_libraries(fv_tiff_test PRIVATE fv_tiff GTest::gtest_main)
  gtest_discover_tests(fv_tiff_test)
endif()
