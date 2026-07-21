// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_cadrg_frame.h — portable CADRG/RPF frame georeferencing, extracted from
// the Windows-only CRpfFrameFile COM object
// (fvw_core/MapDataServer/CadrgMapServer/RpfFrameFile.cpp) and CArcZoneBase.
//
// A CADRG frame carries NO georeferencing of its own: its bounds are derived
// from (a) the RPF 8.3 filename — which encodes the arc zone (last char) and
// a base-34 frame index — and (b) the fixed map scale of its data series
// (the 2-char extension prefix). The pixel decoder (RPFRenderer, ported in
// port/CadrgDecoder) is a separate concern; this class answers "where on
// Earth is this frame".
//
// The zone math (CArcZone) and polar projection (PolarUtil) are reused as-is
// from the ported decoder (fv_cadrg). What was Windows-only and is extracted
// here: the base-34 frame-number decode, the equal-arc/polar corner
// computation, and the data-series -> scale table (the Windows product read
// scale from a SQL catalog, tbl_map_series_cadrg).

#pragma once

#include <string>

namespace fv {

struct CadrgFrameProperties {
  double ll_lat = 0, ll_lon = 0, ur_lat = 0, ur_lon = 0;  // WGS-84
  double scale_denom = 0;         // map scale denominator (e.g. 5e6 for GNC)
  std::string series_code;        // 2-char, lower-case ("gn","jn","lf","tl")
  std::string series_name;        // "GNC","JNC","LFC","TLM", or "" if unknown
  double deg_per_pixel_lat = 0;   // frame is 1536 x 1536 pixels
  double deg_per_pixel_lon = 0;
  int frame_number = -1;          // base-34 index within the zone
  int zone_index = -1;            // 0..7 equal-arc, 8 = polar
  bool upper_hemisphere = true;
  bool is_polar = false;
  // NOTE: ur_lon may be < ll_lon when the frame straddles the antimeridian
  // (preserved from the Windows reader; the FvKit adapter maps this to
  // GeoRect's crossing convention).
  bool crosses_antimeridian = false;
  bool supported = false;         // false if zone/series/frame not recognized
};

class CadrgFrame {
 public:
  // path may be a full path or a bare 8.3 name; only the 8.3 basename is
  // used. Returns false (props->supported == false) if the name is not valid
  // 8.3 RPF, the zone char is invalid, the frame index is invalid, or the
  // data series is not in the scale table.
  bool GetFrameProperties(const std::string& path,
                          CadrgFrameProperties* props);
};

// The frame's own coverage corners, read from its RPF coverage section (the
// authoritative in-file georeferencing). Used to validate the filename-
// derived bounds from GetFrameProperties; the Windows product computes bounds
// from the filename, so that path is the one FvKit uses, but the file's
// coverage section is the ground truth to check it against.
struct CadrgFileCoverage {
  double nw_lat = 0, nw_lon = 0, sw_lat = 0, sw_lon = 0;
  double ne_lat = 0, ne_lon = 0, se_lat = 0, se_lon = 0;
  double ns_resolution = 0, ew_resolution = 0;  // METERS per pixel (~754 for GNC)
};

// Opens and parses the RPF frame (handles the NITF wrapper) and returns its
// embedded coverage section. Returns false if the file can't be read/parsed.
bool ReadCadrgFileCoverage(const std::string& path, CadrgFileCoverage* cov);

}  // namespace fv
