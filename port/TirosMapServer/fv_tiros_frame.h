// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_tiros_frame.h — portable TIROS frame georeferencing, extracted from the
// Windows-only CTirosFrameFile COM object
// (fvw_core/MapDataServer/TirosMapServer/TirosFrameFile.cpp) and tirostab.
//
// TIROS "tiros3" frames are JPEG tiles (1350x1350) named e.g.
// TopoBath_500m_5530.WLD, where the georeferencing is derived entirely from
// the filename: the 4-char series ("500M"/"001K" -> scale + units) and a
// 4-digit column/row within a global equal-arc tile grid. The pixels come
// from the ported CJpeg reader; this class answers "where on Earth is it".
//
// (The whole-world "W_" placeholder and the older "tiros2" *.trs naming are
// recognized for bounds but tiros3 is what TestData exercises.)

#pragma once

#include <string>

namespace fv {

struct TirosFrameProperties {
  double ll_lat = 0, ll_lon = 0, ur_lat = 0, ur_lon = 0;  // WGS-84
  double scale = 0;              // 500, 1, ... (see scale_is_meters)
  bool scale_is_meters = false;  // else kilometers
  std::string series;            // 4-char, upper-case ("500M", "001K")
  double deg_per_pixel_lat = 0;  // tile is 1350 x 1350 pixels
  double deg_per_pixel_lon = 0;
  int col = -1;
  int row = -1;
  bool whole_world = false;      // "W_" placeholder frame
  bool crosses_antimeridian = false;
  bool supported = false;
};

// The TIROS tile is always this many pixels square.
constexpr int kTirosCellPix = 1350;

class TirosFrame {
 public:
  // path may be a full path or a bare name; only the basename is used, and
  // (as on Windows) it is matched upper-cased. Returns false
  // (props->supported == false) for names that aren't recognizable tiros3
  // tiles (wrong length, unparseable series/col/row).
  bool GetFrameProperties(const std::string& path, TirosFrameProperties* props);
};

}  // namespace fv
