// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_search.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fv_desk_vector_map.h"
#include "fv_road_graph_overlay.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/overlay/vector_map_overlay.h"
#include "geo_tool.h"  // GEO_string_to_lat_lon

// geo_tool.h defines the Win16 `near`/`far` keywords away; `near` is a field here.
#undef near
#undef far

namespace fv {
namespace desk {
namespace {

constexpr double kMetersPerDegree = 111320.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// How near a base-map road must be to a graph road of the same name to be
/// the same street (`DedupeSearchRows`, as Pippin's `search.duplicate_radius_m`).
constexpr double kDuplicateRadiusM = 250.0;

bool HasArea(const GeoRect& r) { return r.ur.lat > r.ll.lat || r.ur.lon > r.ll.lon; }

DeskSearchRow RowFrom(const app::SearchResult& r, SearchGroup group, app::SearchSource source,
                      const std::string& source_name, const std::optional<GeoPoint>& near) {
  DeskSearchRow row;
  row.group = group;
  row.title = r.title;
  row.detail = r.detail;
  row.source = source_name;
  row.kind = app::ClassifySearchResult(source, r.detail);
  row.position = r.position;
  row.bounds = r.bounds;
  row.distance_m = near ? app::SearchDistanceMeters(*near, r.position) : 0.0;
  return row;
}

app::SearchRow DedupeKey(const DeskSearchRow& r) {
  app::SearchRow k;
  k.title = r.title;
  k.kind = r.kind;
  k.position = r.position;
  k.bounds = r.bounds;
  return k;
}

}  // namespace

const char* SearchGroupTitle(SearchGroup g) {
  switch (g) {
    case SearchGroup::kLocation:
      return "Location";
    case SearchGroup::kOverlays:
      return "Overlays";
    case SearchGroup::kRoads:
      return "Roads";
    case SearchGroup::kMap:
      return "Map";
  }
  return "";
}

bool ParseSearchLocation(const std::string& text, GeoPoint* out) {
  if (text.find_first_not_of(" \t") == std::string::npos) return false;
  degrees_t lat = 0, lon = 0;
  if (GEO_string_to_lat_lon(text.c_str(), "WGE", &lat, &lon) != 0) return false;
  if (!(lat >= -90.0 && lat <= 90.0) || !(lon >= -180.0 && lon <= 180.0)) return false;
  *out = GeoPoint{lat, lon};
  return true;
}

std::string FormatSearchLocation(const GeoPoint& p) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.5f\xC2\xB0 %c, %.5f\xC2\xB0 %c", std::fabs(p.lat),
                p.lat < 0 ? 'S' : 'N', std::fabs(p.lon), p.lon < 0 ? 'W' : 'E');
  return buf;
}

std::string FormatSearchDistance(double meters) {
  char buf[32];
  if (meters < 1000) std::snprintf(buf, sizeof buf, "%d m", static_cast<int>(std::lround(meters)));
  else std::snprintf(buf, sizeof buf, "%.1f km", meters / 1000);
  return buf;
}

double SearchFitScale(const GeoRect& b, double width_pt, double height_pt, double mm_per_point,
                      double min_denom) {
  if (!HasArea(b) || width_pt <= 0 || height_pt <= 0 || mm_per_point <= 0) return 0.0;
  const double mid_lat = 0.5 * (b.ll.lat + b.ur.lat);
  const double ground_w = (b.ur.lon - b.ll.lon) * kMetersPerDegree * std::cos(mid_lat * kDegToRad);
  const double ground_h = (b.ur.lat - b.ll.lat) * kMetersPerDegree;
  const double screen_w = width_pt * mm_per_point / 1000.0;
  const double screen_h = height_pt * mm_per_point / 1000.0;
  const double denom = 1.2 * std::max(ground_w / screen_w, ground_h / screen_h);
  return std::max(denom, min_denom);
}

std::string SearchStatusLine(const std::vector<DeskSearchRow>& rows, const DeskSearchNotes& notes,
                             const std::string& text, bool map_view_only) {
  if (text.find_first_not_of(" \t") == std::string::npos) return std::string();
  if (!rows.empty() && rows.front().group == SearchGroup::kLocation) return "Coordinate";
  std::string line = rows.empty() ? "Nothing found"
                                  : std::to_string(rows.size()) + (rows.size() == 1 ? " result" : " results");
  if (!notes.roads_error.empty()) line += " \xC2\xB7 road network: " + notes.roads_error;
  if (!notes.map_error.empty()) line += " \xC2\xB7 map: " + notes.map_error;
  if (notes.map_is_raster) line += " \xC2\xB7 the map on screen is not a vector map";
  if (!map_view_only && notes.map_searched && !notes.map_used_index)
    line += " \xC2\xB7 the map has no name index (fvnames build); search this view only";
  return line;
}

DeskSearch::DeskSearch() = default;
DeskSearch::~DeskSearch() = default;

std::vector<DeskSearchRow> DeskSearch::Search(const DeskSearchInput& in) {
  notes_ = DeskSearchNotes();
  std::vector<DeskSearchRow> out;
  if (in.text.find_first_not_of(" \t") == std::string::npos) return out;

  GeoPoint at;
  if (ParseSearchLocation(in.text, &at)) {
    DeskSearchRow row;
    row.group = SearchGroup::kLocation;
    row.title = FormatSearchLocation(at);
    row.detail = "Coordinate";
    row.kind = app::SearchKind::kPoint;
    row.position = at;
    row.bounds = GeoRect{at, at};
    row.distance_m = in.near ? app::SearchDistanceMeters(*in.near, at) : 0.0;
    out.push_back(row);
    return out;
  }

  app::SearchQuery q;
  q.text = in.text;
  q.near = in.near;
  q.max_results = in.max_results;

  bool user_has_roads = false;
  if (in.overlays != nullptr) {
    for (const app::SearchResult& r : app::SearchSession(*in.overlays).Search(q)) {
      const bool is_roads = dynamic_cast<const RoadGraphOverlay*>(r.overlay) != nullptr;
      user_has_roads = user_has_roads || is_roads;
      out.push_back(RowFrom(r, is_roads ? SearchGroup::kRoads : SearchGroup::kOverlays,
                            is_roads ? app::SearchSource::kRoadGraph : app::SearchSource::kPoints,
                            r.overlay != nullptr ? r.overlay->Name() : std::string(), in.near));
    }
  }
  if (!user_has_roads) SearchRoads(in, q, &out);
  SearchMap(in, q, &out);

  // Rows are already in group order. A base-map row that repeats a graph road
  // or an earlier base-map row (same kind, name and place) is dropped: a long
  // road comes back from the tiles as several pieces.
  std::vector<DeskSearchRow> kept;
  kept.reserve(out.size());
  for (DeskSearchRow& row : out) {
    bool dup = false;
    if (row.group == SearchGroup::kMap) {
      const app::SearchRow k = DedupeKey(row);
      for (const DeskSearchRow& prev : kept)
        if ((prev.group == SearchGroup::kRoads || prev.group == SearchGroup::kMap) &&
            app::SearchRowsAreDuplicates(DedupeKey(prev), k, kDuplicateRadiusM)) {
          dup = true;
          break;
        }
    }
    if (!dup) kept.push_back(std::move(row));
  }
  return kept;
}

void DeskSearch::SearchRoads(const DeskSearchInput& in, const app::SearchQuery& q,
                             std::vector<DeskSearchRow>* out) {
  if (in.planner == nullptr) return;
  if (in.planner != roads_planner_) {
    // The Desk replaces its planner when the routing settings change.
    roads_planner_ = in.planner;
    roads_failed_.clear();
    if (!roads_) {
      roads_ = std::make_shared<RoadGraphOverlay>("Road network");
      roads_->SetVisible(false);
      road_stack_.Add(roads_);
    }
    roads_->SetPlanner(in.planner);
    const Status s = roads_->EnsureGraph();
    if (!s.ok()) roads_failed_ = s.message.empty() ? "the road network did not load" : s.message;
  }
  if (!roads_failed_.empty()) {
    notes_.roads_error = roads_failed_;
    return;
  }
  for (const app::SearchResult& r : app::SearchSession(road_stack_).Search(q))
    out->push_back(RowFrom(r, SearchGroup::kRoads, app::SearchSource::kRoadGraph,
                           roads_->Name(), in.near));
}

void DeskSearch::SearchMap(const DeskSearchInput& in, const app::SearchQuery& q,
                           std::vector<DeskSearchRow>* out) {
  if (!in.catalog || !in.product) return;
  if (!VectorBaseMap::CanDraw(in.product->format)) {
    notes_.map_is_raster = true;
    return;
  }
  if (in.catalog.get() != map_catalog_ || in.product->series_id != map_series_) {
    map_catalog_ = in.catalog.get();
    map_series_ = in.product->series_id;
    map_failed_.clear();
    VectorSourcePtr source;
    const Status s = OpenVectorProductSource(*in.catalog, *in.product, in.enc_dir, &source);
    if (!s.ok() || !source) {
      map_failed_ = s.message.empty() ? "the base map could not be opened" : s.message;
      source = nullptr;
    }
    if (!map_) {
      map_ = std::make_shared<VectorMapOverlay>("Map", source);
      map_->SetVisible(false);
      map_stack_.Add(map_);
    } else {
      map_->SetSource(source);
    }
  }
  if (!map_failed_.empty()) {
    notes_.map_error = map_failed_;
    return;
  }
  std::optional<GeoRect> area;
  if (in.map_view_only && in.view && HasArea(*in.view)) area = in.view;
  map_->SetSearchFallbackArea(area);
  notes_.map_searched = true;
  const std::string name = in.product->series_key.empty() ? in.product->format
                                                          : in.product->series_key;
  for (const app::SearchResult& r : app::SearchSession(map_stack_).Search(q))
    out->push_back(RowFrom(r, SearchGroup::kMap, app::SearchSource::kChart, name, in.near));
  notes_.map_used_index = map_->last_search_used_index();
}

}  // namespace desk
}  // namespace fv
