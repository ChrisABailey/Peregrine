// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// CGM display list -> product-neutral VectorSymbol (draw plan G2).
//
// EXTRACTED, NOT WRITTEN. ToVectorSymbol was file-local in
// fv_geosym_style.cpp, which meant CGM -> VectorSymbol was written, tested and
// unreachable: only the GeoSym style engine could turn a .cgm into something
// the renderer draws. G2 lifts it out verbatim so any symbol library can (see
// fv_cgm_library.h), under the plan's R2 — no behaviour change, and the GeoSym
// goldens are the test.
//
// It stays in port/GeoSymServer rather than moving to fvkit because it needs
// fv::CgmSymbol and CSymColorAdjuster, both of which live here. The dependency
// only runs one way: fvkit never links GeoSym.

#ifndef FV_CGM_TO_SYMBOL_H_
#define FV_CGM_TO_SYMBOL_H_

#include "fvkit/symbol/library.h"  // fv::VectorSymbol

// SymColors.h is MFC-shaped (LPCTSTR, CString) and does not include the
// precompiled header it was written under, so stdafx.h has to come first —
// the same order every .cpp in this directory already uses.
#include "stdafx.h"     // POSIX branch: fv_compat + CString + MFC containers
#include "SymColors.h"  // CSymColorAdjuster, COLORREF
#include "fv_cgm_symbol.h"

namespace fv {

// A Win32 COLORREF (0x00BBGGRR) as an opaque FvColor.
FvColor FromColorRef(COLORREF c);

// Flattens a parsed CGM symbol into the seam's display list, running every
// colour through `adjuster` (GeoSym's brightness/contrast/fill-replacement
// table) on the way.
//
// Y IS FLIPPED HERE, and that is the subtle part: CgmSymbol's coordinates are
// y-DOWN because the parser already applied the picture's VDC direction
// multipliers once, while VectorSymbol is y-UP. Windows applies them a SECOND
// time in CCGMDrawingObject::RotateVDC when it builds the vertices it draws,
// so applying them again here is what matches GDI — see ApplyVdcDir in the
// implementation, and the ledger's "VectorSymbol is y-UP" rule.
VectorSymbol ToVectorSymbol(const CgmSymbol& cgm,
                            const CSymColorAdjuster& adjuster);

}  // namespace fv

#endif  // FV_CGM_TO_SYMBOL_H_
