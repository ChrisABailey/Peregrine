// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::CadrgFrame — see fv_cadrg_frame.h. Reuses CArcZone / PolarUtil from the
// ported decoder (fv_cadrg); extracts the base-34 frame-number decode, the
// equal-arc / polar corner math (from CRpfFrameFile), and the data-series ->
// scale table (Windows read it from a SQL catalog).

#include "fv_cadrg_frame.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <cstdio>

#include "RpfZoneScales.h"   // CArcZone, CADRG_FRAME_WIDTH_IN_PIX, WORLD_DEG
#include "cdb_base.h"        // cadrg_cib_base (embedded coverage section)
#include "mapscales.h"       // MapScaleUnitsEnum
#include "polar_utils.h"     // PolarUtil

namespace fv {

namespace {

// "a is east of b" on the longitude circle. Inlined from RPFHelper::is_east_of
// (imgdisp.cpp) to avoid pulling in CString-heavy imgdisp.h for one function.
bool IsEastOf(double a, double b) {
  double diff = a - b;
  if (diff < 0.0) diff += 360.0;  // eastward distance from b to a
  return 0.0 < diff && diff <= 180.0;
}

// Standard MIL-STD-2411A CADRG data-series scales, keyed by the lower-case
// 2-char extension prefix. Only gn/jn/lf/tl appear in TestData; those four
// are cross-checked against each frame's embedded RPF coverage section
// (MatchesEmbeddedCoverage gtest). The rest are the well-known standard
// values, not verified against sample data here.
struct SeriesEntry { const char* code; const char* name; double scale_denom; };
const SeriesEntry kSeriesTable[] = {
    {"gn", "GNC", 5000000.0},   // Global Navigation Chart
    {"jn", "JNC", 2000000.0},   // Jet Navigation Chart
    {"on", "ONC", 1000000.0},   // Operational Navigation Chart
    {"tp", "TPC", 500000.0},    // Tactical Pilotage Chart
    {"lf", "LFC", 500000.0},    // Low Flying Chart
    {"jg", "JOG", 250000.0},    // Joint Operations Graphic
    {"tf", "TFC", 250000.0},    // Transit Flying Chart
    {"tc", "TLM100", 100000.0}, // Topographic Line Map 1:100k
    {"tl", "TLM", 50000.0},     // Topographic Line Map 1:50k
};

const SeriesEntry* LookupSeries(const std::string& code2) {
  for (const auto& e : kSeriesTable)
    if (code2 == e.code) return &e;
  return nullptr;
}

// The bare 8.3 name (12 chars: XXXXXXXX.YYZ). Returns empty on anything else.
std::string BaseName83(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  std::string name = pos == std::string::npos ? path : path.substr(pos + 1);
  if (name.size() != 12 || name[8] != '.') return {};
  return name;
}

// CADRG frame number: first 5 chars of the name are the frame index (6 for
// CIB, detected by the 'i' series letter), base-34 encoded skipping 'I'/'O'.
// Ported from CArcZoneBase::GetFrameNumberFromFileName + CArcZone's
// GetNumCharsInFrameNum. Returns -1 on an invalid digit.
int FrameNumberFromName(const std::string& name83) {
  const bool is_cib = std::tolower((unsigned char)name83[9]) == 'i';
  const int chars = is_cib ? 6 : 5;

  int frame_number = 0;
  for (int i = 0; i < chars; ++i) {
    char c = (char)std::tolower((unsigned char)name83[i]);
    if (c == '0') continue;

    int digit;
    if (c >= '1' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'z') {
      if (c == 'i' || c == 'o') return -1;  // unused letters
      if (c < 'i')      digit = c - 'a' + 10;
      else if (c < 'o') digit = c - 'a' - 1 + 10;  // skip 'i'
      else              digit = c - 'a' - 2 + 10;  // skip 'i' and 'o'
    } else {
      return -1;
    }

    int product = 1;  // 34^(chars - i - 1); most-significant char first
    for (int n = chars - i; n > 1; --n) product *= 34;
    frame_number += digit * product;
  }
  return frame_number;
}

void BoundsEqualArc(CArcZone& zone, double scale, MapScaleUnitsEnum units,
                    int row, int col, CadrgFrameProperties* p) {
  const double f = (double)CADRG_FRAME_WIDTH_IN_PIX;
  double southern_lat = zone.m_bUpperHemisphere
                            ? zone.GetEquatorwardExtent(scale, units)
                            : zone.GetPolewardExtent(scale, units);
  double dlat, dlon;
  zone.CalculateDegreesPerPixel(scale, units, &dlat, &dlon);

  p->ll_lat = southern_lat + row * (f * dlat);
  p->ll_lon = -180.0 + col * (f * dlon);
  p->ur_lat = p->ll_lat + f * dlat;
  p->ur_lon = p->ll_lon + f * dlon;
  if (p->ur_lon > 180.0) {  // wrap across the antimeridian, as on Windows
    p->ur_lon -= WORLD_DEG;
    p->crosses_antimeridian = true;
  }
  p->deg_per_pixel_lat = dlat;
  p->deg_per_pixel_lon = dlon;
}

void BoundsPolar(CArcZone& zone, double scale, MapScaleUnitsEnum units,
                 int row, int col, CadrgFrameProperties* p) {
  const int f = CADRG_FRAME_WIDTH_IN_PIX;
  int rows, cols;
  zone.CalculateNumRowsCols(scale, units, &rows, &cols);
  const int R = (cols * f) / 2;
  const double pol_const = zone.calc_polar_pix_const(scale);
  PolarUtil polar;

  // frame centered on the pole: full longitude range up to the pole
  if (row == rows / 2 && col == cols / 2) {
    double lat;
    polar.polar_xy_to_lat(-R + col * f, -R + row * f, pol_const,
                          zone.m_bUpperHemisphere, lat);
    if (zone.m_bUpperHemisphere) { p->ll_lat = lat; p->ur_lat = 90.0; }
    else                         { p->ll_lat = -90.0; p->ur_lat = lat; }
    p->ll_lon = -180.0;
    p->ur_lon = 180.0;
    return;
  }

  // otherwise min/max over the four corners
  p->ll_lat = 90.0;
  p->ur_lat = -90.0;
  bool first = true;
  for (double dx = 0.0; dx <= 1.0; dx += 0.5) {
    for (double dy = 0.0; dy <= 1.0; dy += 0.5) {
      int x = (int)(-R + (col + dx) * f);
      int y = (int)(-R + (row + dy) * f);
      double lat, lon;
      polar.polar_xy_to_lat(x, y, pol_const, zone.m_bUpperHemisphere, lat);
      polar.polar_xy_to_lon(x, y, zone.m_bUpperHemisphere, lon);
      p->ll_lat = std::min(p->ll_lat, lat);
      p->ur_lat = std::max(p->ur_lat, lat);
      if (first) {
        p->ll_lon = p->ur_lon = lon;
        first = false;
      } else {
        if (IsEastOf(lon, p->ur_lon)) p->ur_lon = lon;
        if (!IsEastOf(lon, p->ll_lon)) p->ll_lon = lon;
      }
    }
  }
  p->crosses_antimeridian = p->ur_lon < p->ll_lon;
}

}  // namespace

bool CadrgFrame::GetFrameProperties(const std::string& path,
                                    CadrgFrameProperties* props) {
  *props = CadrgFrameProperties{};

  std::string name = BaseName83(path);
  if (name.empty()) return false;

  // series = first 2 chars of the extension, lower-cased
  std::string code2 = {(char)std::tolower((unsigned char)name[9]),
                       (char)std::tolower((unsigned char)name[10])};
  const SeriesEntry* series = LookupSeries(code2);
  if (series == nullptr) return false;
  props->series_code = code2;
  props->series_name = series->name;
  props->scale_denom = series->scale_denom;

  std::wstring wname(name.begin(), name.end());
  CArcZone zone;
  if (!zone.SetFromFileName(wname)) return false;
  props->zone_index = zone.m_iZoneIndex;
  props->upper_hemisphere = zone.m_bUpperHemisphere;
  props->is_polar = (zone.m_iZoneIndex == CADRG_POLAR_ZONE_INDEX);

  int frame_number = FrameNumberFromName(name);
  if (frame_number < 0) return false;
  props->frame_number = frame_number;

  const MapScaleUnitsEnum units = MAP_SCALE_DENOMINATOR;
  int rows, cols;
  if (!zone.CalculateNumRowsCols(series->scale_denom, units, &rows, &cols) ||
      cols <= 0)
    return false;
  const int row = frame_number / cols;
  const int col = frame_number % cols;

  if (props->is_polar)
    BoundsPolar(zone, series->scale_denom, units, row, col, props);
  else
    BoundsEqualArc(zone, series->scale_denom, units, row, col, props);

  props->supported = true;
  return true;
}

bool ReadCadrgFileCoverage(const std::string& path, CadrgFileCoverage* cov) {
  *cov = CadrgFileCoverage{};
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) return false;

  cadrg_cib_base frame;
  bool ok = frame.read(fp) == SUCCESS;
  std::fclose(fp);
  if (!ok) return false;

  const RPF_coverage_section* c = frame.get_coverage();
  cov->nw_lat = c->m_nw_lat;  cov->nw_lon = c->m_nw_lon;
  cov->sw_lat = c->m_sw_lat;  cov->sw_lon = c->m_sw_lon;
  cov->ne_lat = c->m_ne_lat;  cov->ne_lon = c->m_ne_lon;
  cov->se_lat = c->m_se_lat;  cov->se_lon = c->m_se_lon;
  cov->ns_resolution = c->m_north_south_resolution;
  cov->ew_resolution = c->m_east_west_resolution;
  return true;
}

}  // namespace fv
