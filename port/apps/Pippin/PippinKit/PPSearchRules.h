// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPSearchRules.h — what a search result is called, and which of two true
// answers about one road survives.
//
// Pure C++ so the mac tests it: both decisions here are arithmetic and string
// work with no phone in them, and both are invisible in a screenshot — a
// missing duplicate looks exactly like a search that did not find it twice.
//
// 1. The word. The desktop's Find box shows the provider's own `detail`
//    ("transportation_name · residential", "point · restaurant"), which suits
//    a user looking at a chart schema. A rider is not, so a row reads
//    `Name — Word` with three words in the vocabulary:
//
//      Point  a place in the rider's own `.fvpoints` document;
//      Road   anything in the road network — the graph the router plans on,
//             and the chart's transportation layers, which are the same
//             streets seen from the other side;
//      POI    everything else the chart knows the name of.
//
//    Three rather than thirty because the list is read one-handed at the top
//    of a route dialog, by somebody who wants to know whether the row is
//    their own marker, a street, or a thing on the map. The chart's sub-type
//    is not lost, it is just not what this line is for.
//
// 2. The duplicate. Two providers legitimately answer "Ruddy Turnstone": the
//    chart's `transportation_name` layer and the `.fvroad` graph, being two
//    recordings of one street. A desktop debugging a pack wants both, tagged
//    by overlay; a phone shows one, because two identical rows on a list a
//    thumb is scrolling looks broken.
//
//    The survivor is whichever ranked higher, never "the chart's", and that
//    is what makes the rule safe with `search.chart_in_view_only` off: a pack
//    with no name index answers a global text query with nothing from the
//    chart, so globally the graph's row is the answer, and preferring the
//    chart by name would delete the only result.
//
// Same name is not enough to call two rows the same thing. Kiawah has one
// Ruddy Turnstone and the world has ten thousand Main Streets, so a
// fold-and-compare would merge two real streets two counties apart. The test
// is name and place, and place is two tests because the two answers about one
// road do not share an anchor: a merged chart row's anchor is its biggest
// piece's label point and the graph's is its own, kilometres apart on a long
// road. So the boxes are asked first, since two recordings of one street
// overlap along its length, and the centre distance is the fallback for the
// degenerate boxes a point answers with.

#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace pippin {

// The three words. Not the provider's `detail`, and not an enum over layers.
enum class SearchKind {
  kPoint,
  kRoad,
  kPoi,
};

// Who answered, as the bridge knows it: the overlay pointer compared against
// the ones it created. The classifier takes this rather than a string because
// whether something is the user's own point document is a fact about
// identity, and re-deriving it from a `detail` beginning "point" is a guess a
// chart layer called `poi` could break.
enum class SearchSource {
  kPoints,     // fv::PointOverlay — the rider's own .fvpoints
  kRoadGraph,  // fv::RoadGraphOverlay — the .fvroad the router plans on
  kChart,      // fv::VectorMapOverlay — the tile pyramid
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

// Whether a chart `detail` names a transportation layer.
//
// A prefix rather than a list, because OpenMapTiles carries the streets in
// two layers with one stem — `transportation` is the geometry and
// `transportation_name` the labelled half a text search finds — and a product
// adding `transportation_link` is still the road network.
// `VectorMapOverlay::DetailFor` composes the string as `layer` or
// `layer · style_key`, so the layer is always the head and this never parses
// the middle dot.
//
// Everything else the chart names is a POI by construction. A catch-all
// rather than a table, because a table would be a second copy of a schema
// this app does not own.
inline bool ChartDetailIsRoad(const std::string& detail) {
  static const char kPrefix[] = "transportation";
  const size_t n = sizeof(kPrefix) - 1;
  return detail.size() >= n && detail.compare(0, n, kPrefix) == 0;
}

inline SearchKind ClassifySearchResult(SearchSource source,
                                       const std::string& detail) {
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

// ---------------------------------------------------------------------------
// The duplicate
// ---------------------------------------------------------------------------

// ASCII case fold plus whitespace normalisation, the same stated limit
// `fv::app::TextMatchQuality` works under: a correct Unicode fold needs ICU,
// which this port does not carry, and the usual fallback corrupts multi-byte
// sequences. Bytes above 0x7F compare exactly, so a name still matches itself
// in any script, which is all this comparison needs.
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

// One row, as the bridge holds it between the session's answer and the phone.
// `kind` is the vocabulary word and `bounds` is what framing uses. Both are
// filled before the dedupe, because both are what the dedupe reads.
struct SearchRow {
  std::string title;
  SearchKind kind = SearchKind::kPoi;
  fv::GeoPoint position;
  fv::GeoRect bounds;
  double distance_m = 0.0;
};

// Metres between two positions on a tangent plane through `a`. The same
// WGS-84 mean radius `PPCameraFit.h` uses, repeated rather than shared so
// this header reads on its own; the two agree to the millimetre.
inline double SearchRowDistanceMeters(const fv::GeoPoint& a,
                                      const fv::GeoPoint& b) {
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

// Whether two boxes touch. Axis-aligned and closed, so a shared edge counts:
// two halves of one street cut at a tile seam meet exactly there.
//
// The antimeridian is not handled and does not need to be. Both boxes come
// from one query's answers, a query is either an area, which cannot span the
// world, or a text search inside one pack, and a pack straddling 180 degrees
// would break the ranking long before this line.
inline bool SearchBoundsOverlap(const fv::GeoRect& a, const fv::GeoRect& b) {
  if (a.ur.lat < b.ll.lat || b.ur.lat < a.ll.lat) return false;
  if (a.ur.lon < b.ll.lon || b.ur.lon < a.ll.lon) return false;
  return true;
}

// Two rows are the same thing when they are the same kind, carry the same
// name, and are in the same place. See the header on why the last is two
// tests.
//
// Kind is part of it, so a `.fvpoints` marker the rider dropped on Ruddy
// Turnstone and named for it still appears beside the street: those are two
// different answers, and the rider made one of them.
inline bool SearchRowsAreDuplicates(const SearchRow& a, const SearchRow& b,
                                    double radius_m) {
  if (a.kind != b.kind) return false;
  if (FoldSearchTitle(a.title) != FoldSearchTitle(b.title)) return false;
  if (SearchBoundsOverlap(a.bounds, b.bounds)) return true;
  return radius_m > 0.0 &&
         SearchRowDistanceMeters(a.position, b.position) <= radius_m;
}

// The ranked list with the second recording of each thing removed, in order
// and keeping the first, which honours the session's ranking rather than
// re-litigating it. Quadratic in the rows returned, which a search box caps:
// fifty rows is at worst twelve hundred string folds, done in the gap between
// two keystrokes.
inline std::vector<SearchRow> DedupeSearchRows(const std::vector<SearchRow>& in,
                                               double radius_m) {
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

}  // namespace pippin
