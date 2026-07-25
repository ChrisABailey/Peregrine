# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# DTED shaded-relief renderer (queue Q6b). The engine — CDtedReader
# (fvw_core/MapDataServer/DtedMapServer/renderer/DtedReader.cpp, ~2900 lines) —
# is compiled IN PLACE: it is already std::string/std::vector/FILE*-based with
# no COM/GDI/MFC/registry, needing only fv_compat's Win32/secure-CRT shims
# (COLORREF/RGB/BYTE/BOOL, sprintf_s/strncpy_s, THROW_ERROR_MSG). Its
# stdafx.h/ComErrorObject.h includes are #ifdef _WIN32-guarded so the MSVC
# product build is unchanged.
#
# fv::DtedShadedRenderer is the portable facade replacing the COM/GDI-bound
# DTEDRenderer (disp.cpp, left Windows-only): same display-mode / lighting /
# contour / band knobs, but renders one cell to an RGBA8 PixelBuffer instead of
# IGraphicsContext::PutPixmap. fv_solar_position replaces the GOV-only SLAC
# astronomy for time-of-day sun.

set(DTED_RENDERER
    ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/DtedMapServer/renderer)

add_library(fv_dted_shaded STATIC
  ${DTED_RENDERER}/DtedReader.cpp
  fv_dted_shaded_renderer.cpp
  fv_solar_position.cpp
)
target_include_directories(fv_dted_shaded
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/port/include
  PRIVATE ${DTED_RENDERER})
target_link_libraries(fv_dted_shaded PUBLIC fv_compat)
target_compile_features(fv_dted_shaded PUBLIC cxx_std_17)

if(FVW_BUILD_TESTS)
  add_executable(fv_dted_shaded_test
    test/dted_shaded_test.cpp
    test/solar_position_test.cpp)
  target_link_libraries(fv_dted_shaded_test PRIVATE fv_dted_shaded GTest::gtest_main)
  gtest_discover_tests(fv_dted_shaded_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
