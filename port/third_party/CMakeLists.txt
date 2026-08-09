# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# Modern third-party dependencies for the cross-platform port.
#
# WHY THIS FILE EXISTS (2026-07-25, per Chris: move to modern library versions).
# The Windows product build consumes 1998-2012-era vendored forks under
# `third_party/` and `fvw_core/ImageLib/*` — zlib 1.2.5, libpng 1.2.7,
# IJG jpeg 6b, libtiff 3.9.4, expat 2.1.0. Those trees are frozen by the hard
# rules (BuildAll.sln must keep working), so the port does NOT edit them.
# Instead the port build pulls current upstream releases here, and modules opt
# in one at a time. Windows keeps its forks; the port moves forward.
#
# POLICY
#   * FetchContent, not vendored source dumps — matches the existing gtest
#     pattern, keeps the repo (and the Peregrine public subset) small, and
#     makes a version bump a one-line edit in this file.
#   * ONE place for versions: the FVW_*_VERSION variables below.
#   * Offline builds: set FETCHCONTENT_FULLY_DISCONNECTED=ON with a populated
#     cache, or point FETCHCONTENT_SOURCE_DIR_<UPPERNAME> at a local checkout.
#   * A library is upgraded only when the port's consumers actually compile
#     against the new API — see port/PORTING.md's dependency-modernization
#     table for which are drop-ins and which are module-sized jobs.

include(FetchContent)

set(FVW_EXPAT_VERSION "2.8.2" CACHE STRING "libexpat release to build against")
set(FVW_ZLIB_VERSION  "1.3.2" CACHE STRING "zlib release to build against")
set(FVW_PROTOZERO_VERSION "1.8.2" CACHE STRING "protozero release to build against")
set(FVW_VTZERO_VERSION    "1.2.0" CACHE STRING "vtzero release to build against")
set(FVW_JSON_VERSION      "3.12.0" CACHE STRING "nlohmann/json release to build against")

# --- expat (MIT) ------------------------------------------------------------
# Replaces third_party/Expat 2.0.1 (expat.h reports 2.1.0, released 2012) for
# port code. Needed by the S-52 PresLib loader (chartsymbols.xml, plan E2) and
# by any future WMS capabilities parsing (Q12), which is NETWORK-supplied XML —
# the reason a current release matters rather than the in-tree 2012 copy.
string(REPLACE "." "_" FVW_EXPAT_TAG "${FVW_EXPAT_VERSION}")
set(EXPAT_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(EXPAT_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(EXPAT_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(expat
  URL https://github.com/libexpat/libexpat/releases/download/R_${FVW_EXPAT_TAG}/expat-${FVW_EXPAT_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(expat)

# --- zlib (zlib licence) ----------------------------------------------------
# Replaces fvw_core/ImageLib/zlib (1.2.5, 2010) for port code. Drop-in: zlib's
# API has been stable since 1.2.x, so consumers (libpng, libtiff) need no
# source changes. Exposed as fv_zlib so call sites name one thing regardless of
# which upstream target spelling a given zlib release uses.
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING  OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
  URL https://github.com/madler/zlib/releases/download/v${FVW_ZLIB_VERSION}/zlib-${FVW_ZLIB_VERSION}.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(zlib)

# zlib's own CMake has spelled the static target differently across releases
# (zlibstatic / zlib). Normalise once here.
if(TARGET zlibstatic)
  set(FVW_ZLIB_TARGET zlibstatic)
elseif(TARGET ZLIB::ZLIB)
  set(FVW_ZLIB_TARGET ZLIB::ZLIB)
else()
  set(FVW_ZLIB_TARGET zlib)
endif()

add_library(fv_zlib INTERFACE)
target_link_libraries(fv_zlib INTERFACE ${FVW_ZLIB_TARGET})
# zconf.h is generated into the build tree; the source dir alone is not enough.
target_include_directories(fv_zlib INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})

# --- protozero + vtzero (BSD-2) --------------------------------------------
# Mapbox Vector Tile decoding for the OSM track (plan section 6, phase O1).
# protozero is a zero-copy protobuf reader; vtzero is the MVT schema on top of
# it — geometry command integers, zigzag deltas and the property value variant,
# which is the fiddly half of the format. The plan called for VENDORING both
# into port/vendor/ (the stb_truetype pattern); FetchContent is the same
# decision with the 2026-07-25 mechanism, so versions stay in this one file and
# nothing multi-MB lands in the repo.
#
# Both are header-only. Their own CMakeLists build tests, examples and doc
# targets we do not want, so SOURCE_SUBDIR points at a directory with no
# CMakeLists.txt: FetchContent_MakeAvailable then POPULATES without calling
# add_subdirectory, and we publish the headers ourselves.
FetchContent_Declare(protozero
  URL https://github.com/mapbox/protozero/archive/refs/tags/v${FVW_PROTOZERO_VERSION}.tar.gz
  SOURCE_SUBDIR headers-only-no-cmake
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(protozero)
add_library(fv_protozero INTERFACE)
target_include_directories(fv_protozero INTERFACE ${protozero_SOURCE_DIR}/include)

FetchContent_Declare(vtzero
  URL https://github.com/mapbox/vtzero/archive/refs/tags/v${FVW_VTZERO_VERSION}.tar.gz
  SOURCE_SUBDIR headers-only-no-cmake
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(vtzero)
add_library(fv_vtzero INTERFACE)
target_include_directories(fv_vtzero INTERFACE ${vtzero_SOURCE_DIR}/include)
target_link_libraries(fv_vtzero INTERFACE fv_protozero)

# --- nlohmann/json (MIT) ----------------------------------------------------
# The port had no JSON reader: preferences are INI (fv::Settings, deliberately
# — a file a human annotates), and expat is XML-only. MBTiles keeps its layer
# inventory in a `json` metadata value, and O2's MapLibre style subset is JSON
# through and through, so this is the first place JSON is unavoidable.
#
# The `include.zip` RELEASE ASSET, not the source tarball: 300 KB of headers
# instead of a repository whose test corpus dwarfs this whole port. It has no
# CMakeLists at its root, so MakeAvailable populates only.
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v${FVW_JSON_VERSION}/include.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
add_library(fv_json INTERFACE)
target_include_directories(fv_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)

# --- integration smoke tests ------------------------------------------------
# expat has no consumer in the port yet (the first is the E2 PresLib loader),
# so without this the fetch would be unverified. zlib is exercised by
# port/ImageLib/test/codecs_test.cpp through libpng/libtiff.
if(FVW_BUILD_TESTS)
  add_executable(fv_expat_smoke_test test/expat_smoke_test.cpp)
  target_link_libraries(fv_expat_smoke_test PRIVATE expat GTest::gtest_main)
  gtest_discover_tests(fv_expat_smoke_test)
endif()
