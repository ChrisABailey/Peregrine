// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/app/search.h"

#include <algorithm>
#include <cmath>

#include "fvkit/nav/road_snap.h"  // ProjectOntoSegment — the port's metre
#include "fvkit/overlay/manager.h"

namespace fv {
namespace app {

const char* ToString(SearchOrder o) {
  switch (o) {
    case SearchOrder::kAuto:
      return "auto";
    case SearchOrder::kBestMatch:
      return "best-match";
    case SearchOrder::kNearest:
      return "nearest";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Distance
// ---------------------------------------------------------------------------

double SearchDistanceMeters(const GeoPoint& a, const GeoPoint& b) {
  // A DEGENERATE SEGMENT, on purpose. road_snap.h's projection is the port's
  // one definition of "how far is that" — the tangent plane through the query
  // point, in the WGS-84 mean-radius metre the road graph's arc lengths are
  // in — and its header explicitly says `distance_m` is filled even when the
  // segment has no direction. Writing the four lines out again here would be
  // a second copy of what a metre is, which is exactly what that header
  // forbids.
  SegmentProjection sp;
  ProjectOntoSegment(a, b, b, &sp);
  return sp.distance_m;
}

namespace {

// Metres per degree of latitude, for turning a radius into a box. The one
// number SearchDistanceMeters could not borrow, because road_snap.h keeps it
// inside a function; it is the same constant (6371008.8 * pi/180), and the
// test pins the two against each other so they cannot drift apart.
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kMetersPerDegLat = 6371008.8 * kDegToRad;

char FoldAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

std::string Fold(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(FoldAscii(c));
  return out;
}

// Whitespace-separated runs. Punctuation stays attached to its word, which is
// the right call for the names this searches: "St. Helena" tokenises to
// {"st.", "helena"} and "st hel" still finds it, because a token match is a
// PREFIX match and "st" is a prefix of "st.".
std::vector<std::string> Tokens(const std::string& folded) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < folded.size()) {
    while (i < folded.size() && IsSpace(folded[i])) ++i;
    const size_t start = i;
    while (i < folded.size() && !IsSpace(folded[i])) ++i;
    if (i > start) out.push_back(folded.substr(start, i - start));
  }
  return out;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}

// The centre of a box, antimeridian included — a crossing rect's centre is on
// the far side of 180 and not, as the naive average says, in the middle of the
// Atlantic.
GeoPoint CenterOf(const GeoRect& r) {
  GeoPoint c;
  c.lat = (r.ll.lat + r.ur.lat) / 2.0;
  double span = r.ur.lon - r.ll.lon;
  if (r.CrossesAntimeridian()) span += 360.0;
  c.lon = NormalizeLon(r.ll.lon + span / 2.0);
  return c;
}

// The box a circle fits in. Longitude opens up with the cosine, so a radius
// that would wrap the world (near a pole, or simply a very large one) gives
// the whole longitude range rather than a box that has folded through itself.
GeoRect BoxAround(const GeoPoint& p, double radius_m) {
  const double dlat = radius_m / kMetersPerDegLat;
  double lat_lo = p.lat - dlat, lat_hi = p.lat + dlat;
  if (lat_lo < -90.0) lat_lo = -90.0;
  if (lat_hi > 90.0) lat_hi = 90.0;

  const double cos_lat = std::cos(p.lat * kDegToRad);
  const double meters_per_deg_lon = kMetersPerDegLat * cos_lat;
  if (meters_per_deg_lon <= 1e-6) {
    return GeoRect{{lat_lo, -180.0}, {lat_hi, 180.0}};
  }
  const double dlon = radius_m / meters_per_deg_lon;
  if (dlon >= 180.0) return GeoRect{{lat_lo, -180.0}, {lat_hi, 180.0}};
  return GeoRect{{lat_lo, NormalizeLon(p.lon - dlon)},
                 {lat_hi, NormalizeLon(p.lon + dlon)}};
}

// The coarse box a provider is given, from whatever spatial filters the query
// carries. A SUPERSET is always a legal answer here — the exact circle is cut
// afterwards by the session — so the antimeridian case does not need clever
// arithmetic: when either box wraps, the caller's own area is handed down
// unchanged and the radius does its work in the cut.
std::optional<GeoRect> CoarseArea(const SearchQuery& q) {
  const bool has_circle = q.near.has_value() && q.radius_m > 0.0;
  if (!has_circle) return q.area;
  const GeoRect circle = BoxAround(*q.near, q.radius_m);
  if (!q.area) return circle;
  if (q.area->CrossesAntimeridian() || circle.CrossesAntimeridian()) {
    return q.area;
  }
  GeoRect cut;
  cut.ll.lat = std::max(q.area->ll.lat, circle.ll.lat);
  cut.ll.lon = std::max(q.area->ll.lon, circle.ll.lon);
  cut.ur.lat = std::min(q.area->ur.lat, circle.ur.lat);
  cut.ur.lon = std::min(q.area->ur.lon, circle.ur.lon);
  // Disjoint: hand back the empty box rather than an inverted one, so a
  // provider's Contains() answers false for everything instead of true.
  if (cut.ll.lat > cut.ur.lat || cut.ll.lon > cut.ur.lon) {
    return GeoRect{*q.near, *q.near};
  }
  return cut;
}

const std::atomic<bool>& NeverCancelled() {
  static const std::atomic<bool> flag{false};
  return flag;
}

}  // namespace

// ---------------------------------------------------------------------------
// The shared match rule
// ---------------------------------------------------------------------------

int TextMatchQuality(const std::string& query, const std::string& candidate) {
  const std::string q = Fold(query);
  const std::vector<std::string> qt = Tokens(q);
  if (qt.empty()) return 0;  // an empty query matches everything, exactly

  const std::string c = Fold(candidate);
  // Compared against the query with its outer whitespace gone, so a trailing
  // space typed into a search box does not demote an exact hit to a prefix.
  const std::string q_trim = [&] {
    std::string s;
    for (size_t i = 0; i < qt.size(); ++i) {
      if (i) s.push_back(' ');
      s += qt[i];
    }
    return s;
  }();
  if (c == q_trim) return 0;
  if (StartsWith(c, q_trim)) return 1;

  const std::vector<std::string> ct = Tokens(c);
  for (const std::string& t : qt) {
    bool found = false;
    for (const std::string& candidate_token : ct) {
      if (StartsWith(candidate_token, t)) {
        found = true;
        break;
      }
    }
    // EVERY query token must land. "ruddy bufflehead" is not a hit on Ruddy
    // Turnstone: a user who typed two words meant both of them, and an
    // any-token rule turns a two-word search into a longer list than a
    // one-word search, which is the opposite of what typing more should do.
    if (!found) return -1;
  }
  return 2;
}

bool SearchTextAccepts(const SearchQuery& q, const std::string& label,
                       int* out_quality) {
  const int quality = TextMatchQuality(q.text, label);
  if (quality < 0) return false;
  if (out_quality != nullptr) *out_quality = quality;
  return true;
}

bool SearchAreaAccepts(const SearchQuery& q, const GeoPoint& p) {
  if (!q.area) return true;
  return q.area->Contains(p);
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

SearchSession::SearchSession(const OverlayManager& manager)
    : manager_(manager) {}

std::vector<SearchResult> SearchSession::Search(const SearchQuery& q) const {
  return Search(q, NeverCancelled());
}

std::vector<SearchResult> SearchSession::Search(
    const SearchQuery& q, const std::atomic<bool>& cancel) const {
  // What the providers are actually asked. The circle became a box here and
  // exactly once, so no provider ever implements one.
  SearchQuery ask = q;
  ask.area = CoarseArea(q);

  // The walk. Topmost first in both cases, so the stack's own order is the
  // stable tie-break the ordering rules lean on.
  std::vector<Overlay*> walk;
  if (q.visible_only) {
    const std::vector<Overlay*> order = manager_.DrawOrder();
    walk.assign(order.rbegin(), order.rend());
  } else {
    const std::vector<std::shared_ptr<Overlay>>& stack = manager_.Overlays();
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      walk.push_back(it->get());
    }
  }

  std::vector<SearchResult> results;
  std::vector<int> ranks;
  int rank = 0;
  for (Overlay* overlay : walk) {
    if (cancel.load()) break;
    SearchProvider* provider = overlay->AsSearch();
    ++rank;
    if (provider == nullptr) continue;

    const size_t before = results.size();
    provider->Search(ask, cancel, results);
    for (size_t i = before; i < results.size(); ++i) {
      if (results[i].overlay == nullptr) results[i].overlay = overlay;
      ranks.push_back(rank - 1);
    }
    // A provider that ignores max_results is trimmed to it rather than
    // trusted: the cap is what stops one overlay's ten thousand rows from
    // burying every other overlay's answer before the ranking ever runs.
    if (q.max_results > 0 && results.size() - before > q.max_results) {
      results.resize(before + q.max_results);
      ranks.resize(results.size());
    }
  }

  // Ordering. kAuto is resolved HERE and nowhere else — before the distances,
  // because it is what decides whether there is anything to measure from: a
  // text query ranks by how well it matched, a spatial one has nothing to rank
  // by but distance.
  SearchOrder order = q.order;
  if (order == SearchOrder::kAuto) {
    order = q.text.empty() ? SearchOrder::kNearest : SearchOrder::kBestMatch;
  }

  // `near` is the origin whenever it is set, whatever the ordering — it is
  // still the second key of kBestMatch. Only kNearest falls back to the area's
  // centre, because only kNearest has to produce SOME order and would
  // otherwise be stack order under a different name.
  bool have_origin = false;
  GeoPoint origin;
  if (q.near) {
    origin = *q.near;
    have_origin = true;
  } else if (order == SearchOrder::kNearest && q.area) {
    origin = CenterOf(*q.area);
    have_origin = true;
  }

  std::vector<double> distance(results.size(), 0.0);
  if (have_origin) {
    for (size_t i = 0; i < results.size(); ++i) {
      distance[i] = SearchDistanceMeters(origin, results[i].position);
    }
  }

  // THE EXACT CUT, which the box could only approximate — the corners of a
  // bounding box are 41% further out than its inscribed circle, and a "within
  // 500 m" that returned something 700 m away would be a wrong answer rather
  // than a generous one.
  if (q.near && q.radius_m > 0.0) {
    size_t keep = 0;
    for (size_t i = 0; i < results.size(); ++i) {
      if (distance[i] > q.radius_m) continue;
      // Guarded, and it is not defensive noise: `keep == i` for every result
      // until the first one is dropped, and self-move-assigning a std::string
      // is undefined behaviour that in practice EMPTIES it -- a title that
      // vanished on the way through a filter that kept the row.
      if (keep != i) {
        results[keep] = std::move(results[i]);
        distance[keep] = distance[i];
        ranks[keep] = ranks[i];
      }
      ++keep;
    }
    results.resize(keep);
    distance.resize(keep);
    ranks.resize(keep);
  }

  std::vector<size_t> idx(results.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  const bool nearest_first = order == SearchOrder::kNearest;
  std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
    if (!nearest_first && results[a].match_quality != results[b].match_quality)
      return results[a].match_quality < results[b].match_quality;
    if (have_origin && distance[a] != distance[b])
      return distance[a] < distance[b];
    return ranks[a] < ranks[b];
  });

  std::vector<SearchResult> sorted;
  sorted.reserve(idx.size());
  for (size_t i : idx) sorted.push_back(std::move(results[i]));
  if (q.max_results > 0 && sorted.size() > q.max_results) {
    sorted.resize(q.max_results);
  }
  return sorted;
}

}  // namespace app
}  // namespace fv
