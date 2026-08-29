// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/formats/enumerate.h — FvKit L1 frame enumeration.
// Contracts: port/fvkit-contracts.md (D1, D6). Mirrors the MDM contract's
// IEnumMapElementsBase (FindFirst/NextElement -> file + bbox + series):
// enumeration is a cheap directory scan by naming convention — no file is
// opened, so fields only a header read could fill (e.g. DTED edition) stay
// empty until the catalog or source needs them.
//
// The RegisterFormat multi-slot registry (generalizing fv_interfaces.h's
// single-slot pattern) is deferred until a second format adapter exists.

#pragma once

#include <cstdint>
#include <string>

#include "fvkit/geo.h"

namespace fv {

struct FrameInfo {
  std::string path;        // absolute or root-relative file path
  GeoRect bounds;          // WGS-84 coverage of this frame/cell
  std::string series_key;  // format-specific series id (e.g. "DTED1", "GNC")
  std::string edition;     // empty when the scan can't know it
  int64_t size_bytes = 0;
  // Map scale of the series (mirrors tblMapSeries Scale/ScaleUnits). Units
  // are MapScaleUnitsEnum values (fv_map_enums.h / the decoder's mapscales.h
  // — same COM ABI numbering) carried as int here because those two headers
  // both define the enum at global scope and must never meet in one TU.
  // scale == 0 means not applicable (e.g. DTED elevation) or unknown.
  double scale = 0;
  int scale_units = 0;  // 0 == MAP_SCALE_DENOMINATOR
};

// Scans one data-source directory tree for frames of one format.
// Usage: Begin(dir) once, then Next until it returns false. Enumeration
// order is deterministic (sorted by path).
struct IFrameEnumerator {
  virtual ~IFrameEnumerator() = default;
  IFrameEnumerator(const IFrameEnumerator&) = delete;
  IFrameEnumerator& operator=(const IFrameEnumerator&) = delete;

  virtual Status Begin(const std::string& dir) = 0;
  virtual bool Next(FrameInfo* info) = 0;

 protected:
  IFrameEnumerator() = default;
};

}  // namespace fv
