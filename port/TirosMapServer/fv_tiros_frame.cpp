// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::TirosFrame — see fv_tiros_frame.h. Faithful port of CTirosFrameFile::
// raw_GetFrameBoundaries (tiros3 branch) + tirostab's SeriesToMapScale /
// CalcDegreesPerPixel. Numeric quirks preserved: the boundary formulas carry
// half-pixel (dpp/2) insets and the +dpp_lat / -dpp_lon corrections exactly
// as the Windows reader computes them.

#include "fv_tiros_frame.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace fv {

namespace {

// Constants from TirosFrameFile.cpp / tirostab.h.
constexpr double kMaxLatDeg = 90.0;
constexpr int kFilenameLen = 22;   // "TOPOBATH_500M_5530.WLD"
constexpr int kSeriesStart = 9;
constexpr int kSeriesLen = 4;
constexpr int kColRowStart = 14;
constexpr int kColRowLen = 4;

std::string BaseNameUpper(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  std::string name = pos == std::string::npos ? path : path.substr(pos + 1);
  for (char& c : name) c = (char)std::toupper((unsigned char)c);
  return name;
}

// tirostab.cpp SeriesToMapScale: first 3 chars = scale number, 4th = 'M'
// (meters) else kilometers.
void SeriesToScale(const std::string& series, double* scale, bool* is_meters) {
  *scale = std::atoi(series.substr(0, 3).c_str());
  *is_meters = series.size() == 4 && series[3] == 'M';
}

// tirostab.cpp CalcDegreesPerPixel. Returns false for scales it doesn't know
// (the Windows code ATLASSERT(0)s and yields 0 dpp).
bool CalcDegreesPerPixel(double scale, bool is_meters, double* dpp_lat,
                         double* dpp_lon) {
  double tiles_ew, tiles_ns;
  if (scale == 500 && is_meters) {
    tiles_ew = 64;
    tiles_ns = 32;
  } else if (!is_meters) {  // kilometers
    tiles_ew = 32 / scale;
    tiles_ns = 16 / scale;
  } else {
    *dpp_lat = *dpp_lon = 0;
    return false;
  }
  *dpp_lat = 180.0 / (tiles_ns * kTirosCellPix);
  *dpp_lon = 360.0 / (tiles_ew * kTirosCellPix);
  return true;
}

}  // namespace

bool TirosFrame::GetFrameProperties(const std::string& path,
                                    TirosFrameProperties* props) {
  *props = TirosFrameProperties{};

  std::string name = BaseNameUpper(path);

  if (name.find("W_") != std::string::npos) {  // whole-world placeholder
    props->whole_world = true;
    props->ll_lat = -kMaxLatDeg;
    props->ll_lon = -180.0;
    props->ur_lat = kMaxLatDeg;
    props->ur_lon = 180.0;
    props->supported = true;
    return true;
  }

  if ((int)name.size() != kFilenameLen) return false;

  props->series = name.substr(kSeriesStart, kSeriesLen);
  std::string col_row = name.substr(kColRowStart, kColRowLen);
  for (char c : col_row)
    if (!std::isdigit((unsigned char)c)) return false;
  int col = (col_row[0] - '0') * 10 + (col_row[1] - '0');
  int row = (col_row[2] - '0') * 10 + (col_row[3] - '0');
  props->col = col;
  props->row = row;

  SeriesToScale(props->series, &props->scale, &props->scale_is_meters);
  double dlat, dlon;
  if (!CalcDegreesPerPixel(props->scale, props->scale_is_meters, &dlat, &dlon))
    return false;
  props->deg_per_pixel_lat = dlat;
  props->deg_per_pixel_lon = dlon;

  // Boundary formulas copied verbatim from raw_GetFrameBoundaries (tiros3).
  props->ur_lat = kMaxLatDeg - dlat / 2.0 - dlat * kTirosCellPix * row;
  props->ll_lat = props->ur_lat - dlat * kTirosCellPix + dlat;

  props->ll_lon = -180.0 + dlon / 2.0 + dlon * kTirosCellPix * col;
  if (props->ll_lon > 180.0) props->ll_lon -= 360.0;
  props->ur_lon = props->ll_lon + dlon * kTirosCellPix - dlon;
  if (props->ur_lon > 180.0) props->ur_lon -= 360.0;

  props->crosses_antimeridian = props->ur_lon < props->ll_lon;
  props->supported = true;
  return true;
}

}  // namespace fv
