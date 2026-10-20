// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/app/search_rules.h — the kind word for a search result, and the rule
/// that drops a second answer about the same thing.
///
/// A road is often answered twice: once by the chart's `transportation_name`
/// layer and once by the `.fvroad` graph. Same kind and folded name is not
/// enough to call two rows one thing (every city has a Main Street), so place
/// is tested too: overlapping bounds first, since two recordings of one street
/// overlap along its length and their anchors can be kilometres apart, then
/// centre distance for the degenerate boxes a point answers with. The first
/// row of a duplicate pair is kept, so the session's ranking stands.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace app {

/// What a result is, in the vocabulary a search list shows.
enum class SearchKind {
  kPoint,  ///< a place in one of the user's own overlays
  kRoad,   ///< the road network, from the graph or the chart's transportation layers
  kPoi,    ///< anything else the chart names
};

/// Who answered, decided by overlay identity in the caller.
enum class SearchSource {
  kPoints,     ///< a user overlay (points, routes, …)
  kRoadGraph,  ///< `RoadGraphOverlay`
  kChart,      ///< `VectorMapOverlay` over a base map
};

inline const char* SearchKindWord(SearchKind k) {
  switch (k) {
    case SearchKind::kPoint:
      return "Point";
    case SearchKind::kRoad:
      return "Road";
    case SearchKind::kPoi:
      return "POI";
  }
  return "POI";
}

/// Whether a chart `detail` (`layer` or `layer · style_key`) names a
/// transportation layer. A prefix, so `transportation_name` and any
/// `transportation_*` layer count.
inline bool ChartDetailIsRoad(const std::string& detail) {
  static const char kPrefix[] = "transportation";
  const size_t n = sizeof(kPrefix) - 1;
  return detail.size() >= n && detail.compare(0, n, kPrefix) == 0;
}

inline SearchKind ClassifySearchResult(SearchSource source, const std::string& detail) {
  switch (source) {
    case SearchSource::kPoints:
      return SearchKind::kPoint;
    case SearchSource::kRoadGraph:
      return SearchKind::kRoad;
    case SearchSource::kChart:
      return ChartDetailIsRoad(detail) ? SearchKind::kRoad : SearchKind::kPoi;
  }
  return SearchKind::kPoi;
}

/// ASCII case fold with whitespace runs collapsed and trimmed. Bytes above
/// 0x7F compare exactly, the same limit `TextMatchQuality` works under.
inline std::string FoldSearchTitle(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pending_space = false;
  for (const char c : s) {
    const unsigned char u = (unsigned char)c;
    if (u == ' ' || u == '\t' || u == '\n' || u == '\r') {
      if (!out.empty()) pending_space = true;
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(u >= 'A' && u <= 'Z' ? (char)(u + ('a' - 'A')) : c);
  }
  return out;
}

/// One result as a search list holds it; `kind` and `bounds` are what the
/// dedupe reads.
struct SearchRow {
  std::string title;
  SearchKind kind = SearchKind::kPoi;
  GeoPoint position;
  GeoRect bounds;
  double distance_m = 0.0;
};

/// Metres between two positions on a tangent plane through `a` (WGS-84 mean radius).
inline double SearchRowDistanceMeters(const GeoPoint& a, const GeoPoint& b) {
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  constexpr double kMetersPerDegLat = 6371008.8 * kDegToRad;
  const double cos_lat = std::cos(a.lat * kDegToRad);
  double dlon = b.lon - a.lon;
  while (dlon > 180.0) dlon -= 360.0;
  while (dlon < -180.0) dlon += 360.0;
  const double dx = dlon * kMetersPerDegLat * (cos_lat > 1e-6 ? cos_lat : 1e-6);
  const double dy = (b.lat - a.lat) * kMetersPerDegLat;
  return std::sqrt(dx * dx + dy * dy);
}

/// Whether two boxes touch; closed, so a shared edge (a tile seam) counts.
/// The antimeridian is not handled: both boxes come from one query's answers.
inline bool SearchBoundsOverlap(const GeoRect& a, const GeoRect& b) {
  if (a.ur.lat < b.ll.lat || b.ur.lat < a.ll.lat) return false;
  if (a.ur.lon < b.ll.lon || b.ur.lon < a.ll.lon) return false;
  return true;
}

/// Same kind, same folded name, and the same place (overlapping bounds, or
/// centres within `radius_m` when it is positive).
inline bool SearchRowsAreDuplicates(const SearchRow& a, const SearchRow& b, double radius_m) {
  if (a.kind != b.kind) return false;
  if (FoldSearchTitle(a.title) != FoldSearchTitle(b.title)) return false;
  if (SearchBoundsOverlap(a.bounds, b.bounds)) return true;
  return radius_m > 0.0 && SearchRowDistanceMeters(a.position, b.position) <= radius_m;
}

/// `in` with each later duplicate removed, order kept. Quadratic in the rows,
/// which a search caps.
inline std::vector<SearchRow> DedupeSearchRows(const std::vector<SearchRow>& in, double radius_m) {
  std::vector<SearchRow> out;
  out.reserve(in.size());
  for (const SearchRow& row : in) {
    bool seen = false;
    for (const SearchRow& kept : out) {
      if (SearchRowsAreDuplicates(kept, row, radius_m)) {
        seen = true;
        break;
      }
    }
    if (!seen) out.push_back(row);
  }
  return out;
}

}  // namespace app
}  // namespace fv
