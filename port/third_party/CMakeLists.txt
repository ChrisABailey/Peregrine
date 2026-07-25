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

# --- integration smoke tests ------------------------------------------------
# expat has no consumer in the port yet (the first is the E2 PresLib loader),
# so without this the fetch would be unverified. zlib is exercised by
# port/ImageLib/test/codecs_test.cpp through libpng/libtiff.
if(FVW_BUILD_TESTS)
  add_executable(fv_expat_smoke_test test/expat_smoke_test.cpp)
  target_link_libraries(fv_expat_smoke_test PRIVATE expat GTest::gtest_main)
  gtest_discover_tests(fv_expat_smoke_test)
endif()
