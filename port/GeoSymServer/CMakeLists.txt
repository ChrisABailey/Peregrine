# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

# GeoSym rule-table layer (vpf-geosym plan phase V3): the headless,
# non-GDI foundation of FalconView's GeoSymServer — parses the SymAssign
# delimited tables and evaluates attribute-expression rules. Compiled in
# place from fvw_core/.../GeoSymServer:
#
#   DelimitedParser      generic '|'/'\t' delimited-table tokenizer (CStdioFile)
#   AttributeExpressions  ATTEXP.TXT rule engine: CVPFFormatHandler, CAEValue,
#                         CAEAttributeList, CAEEntry, CAttributeExpression(s),
#                         CECDISValues (mariner ISDM/IDSM/SSDC knobs)
#   SymColors            COLOR.TXT -> COLORREF table + brightness/contrast adjuster
#
# Phase V4 (2026-07-23) adds the CGM interpreter's PARSE half:
#
#   CGMFile              binary CGM element decoder -> CCGMDrawingObject graph.
#                        Compiled in place; the Draw/CreatePen/CreateBrush/
#                        GetFont methods and the GDI font table are severed
#                        with #ifdef _WIN32.
#   fv_cgm_symbol        fv::CgmSymbol — flattens that MFC object graph into a
#                        plain C++17 display list in symbol-local VDC coords.
#                        This is the seam V5's renderer draws through ICanvas.
#
# Still Windows-only (GDI- or assignment-coupled): SanSymbol (symbol
# assignment + drawing), SymText/draw_text (GDI text), SymLayeredDisplay
# (offscreen DC compositing), GeoSymServer.cpp (StartRendering/DrawSymbol over
# a CDC), SimpleLoc. CGMData/CGMFileCache are deferred WITH them, not with
# CGMFile: CGMData.h includes SanSymbol.h + SymText.h, so it belongs to the
# V5 symbol-assignment layer despite sitting next to the parser.

set(GEOSYM ${CMAKE_SOURCE_DIR}/fvw_core/MapDataServer/VpfMapServer/GeoSymServer)

add_library(fv_geosym STATIC
  ${GEOSYM}/DelimitedParser.cpp
  ${GEOSYM}/AttributeExpressions.cpp
  ${GEOSYM}/SymColors.cpp
  ${GEOSYM}/CGMFile.cpp
  fv_cgm_symbol.cpp
  fv_cgm_to_symbol.cpp
  fv_cgm_library.cpp
  fv_geosym_style.cpp
)
# fvw_core/Common carries CGMDefines.h (the CGM opcode constants). NOTE it
# also holds an OLDER CGMFile.{h,cpp} pair — a diverged copy we must never
# compile; GeoSymServer's own CGMFile.h wins because quoted includes resolve
# next to the including file first.
target_include_directories(fv_geosym PUBLIC
  ${GEOSYM}
  ${CMAKE_SOURCE_DIR}/fvw_core/Common
  ${CMAKE_CURRENT_SOURCE_DIR}   # fv_cgm_symbol.h (the portable V4 seam)
)
# fv_fvkit supplies the generic vector seam (fvkit/vector/style.h) the
# GeoSym style engine implements, plus the renderer its tests draw with.
# The dependency only runs this way: fvkit never links GeoSym.
target_link_libraries(fv_geosym PUBLIC fv_compat fv_fvkit)

if(FVW_BUILD_TESTS)
  add_executable(fv_geosym_test test/geosym_test.cpp test/cgm_symbol_test.cpp
                             test/geosym_style_test.cpp
                             test/cgm_library_test.cpp)
  # fv_vpf: the V5b end-to-end test renders real DNC through the seam.
# fv_png: golden-PNG side channel, same convention as canvas_test.
target_link_libraries(fv_geosym_test PRIVATE fv_geosym fv_vpf fv_png
                                             GTest::gtest_main)
  gtest_discover_tests(fv_geosym_test
    PROPERTIES ENVIRONMENT "FVW_TESTDATA_DIR=${CMAKE_SOURCE_DIR}/TestData")
endif()
