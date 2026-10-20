// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_search.h — the Find dialog's query: a coordinate, else the
/// overlays, the road network and the base map on screen.
///
/// Text that parses as a location (decimal, DMS, MGRS) is the whole answer.
/// Otherwise three groups are searched, in this order: the user's overlay
/// stack, the routing `.fvroad` graph (whole network), and the vector base
/// map the view is drawing (confined to the view unless the pack has an
/// `fvnames` index). The road graph and the base map are held in private
/// stacks, so they never appear in the layers panel, and the base map's
/// source is opened separately from the render thread's.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fv_view_ladder.h"
#include "fvkit/app/search.h"
#include "fvkit/app/search_rules.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/manager.h"

namespace fv {
class Catalog;
class Overlay;
class RoadGraphOverlay;
class RoutePlanner;
class VectorMapOverlay;

namespace desk {

/// Where a row came from; rows are listed in this order.
enum class SearchGroup {
  kLocation = 0,  ///< the text parsed as a coordinate
  kOverlays = 1,  ///< the user's overlay stack
  kRoads = 2,     ///< the routing graph
  kMap = 3,       ///< the vector base map on screen
};

/// The heading a group is listed under ("Location", "Overlays", "Roads", "Map").
const char* SearchGroupTitle(SearchGroup g);

/// One result as the dialog lists it.
struct DeskSearchRow {
  SearchGroup group = SearchGroup::kOverlays;
  std::string title;
  std::string detail;  ///< the provider's own words, or the formatted coordinate
  std::string source;  ///< the overlay's name, or the base map's series
  app::SearchKind kind = app::SearchKind::kPoi;
  GeoPoint position;
  GeoRect bounds;      ///< degenerate for a point
  double distance_m = 0.0;  ///< from `near`, 0 without one
};

/// What one search is asked over. Pointers are borrowed for the call.
struct DeskSearchInput {
  std::string text;
  const OverlayManager* overlays = nullptr;
  const RoutePlanner* planner = nullptr;
  std::shared_ptr<Catalog> catalog;
  std::optional<view::LadderProduct> product;  ///< the base map drawn now
  std::optional<GeoPoint> near;                ///< ranks ties and measures distance
  std::optional<GeoRect> view;                 ///< the on-screen box
  bool map_view_only = true;
  size_t max_results = 50;  ///< per group
  std::string enc_dir;      ///< S-52 library, for an ENC base map
};

/// What the last search could not do, for the dialog's status line.
struct DeskSearchNotes {
  std::string roads_error;    ///< the road graph would not load
  std::string map_error;      ///< the base map source would not open
  bool map_searched = false;  ///< a vector base map was asked
  bool map_used_index = false;  ///< it answered from its name index
  bool map_is_raster = false;   ///< the map on screen has no features to search
};

/// Parses `text` as a location on WGS-84 (decimal degrees, DMS, MGRS).
/// False when it is not one. MGRS needs `MSPCCS_DATA` to name the GeoTrans
/// data directory.
bool ParseSearchLocation(const std::string& text, GeoPoint* out);

/// "33.74000° N, 84.39000° W".
std::string FormatSearchLocation(const GeoPoint& p);

/// The scale denominator that fits `bounds` in a `width_pt` × `height_pt`
/// view with a 20% margin, never larger-scale than `min_denom`. A degenerate
/// box returns 0 (keep the current scale).
double SearchFitScale(const GeoRect& bounds, double width_pt, double height_pt,
                      double mm_per_point, double min_denom);

/// The line under the Find list: the count, then whatever could not be
/// searched (a road network that would not load, a base map without a name
/// index searched outside the view). Empty for empty text.
std::string SearchStatusLine(const std::vector<DeskSearchRow>& rows, const DeskSearchNotes& notes,
                             const std::string& text, bool map_view_only);

/// Runs searches and keeps the road graph and base-map providers between them.
class DeskSearch {
 public:
  DeskSearch();
  ~DeskSearch();
  DeskSearch(const DeskSearch&) = delete;
  DeskSearch& operator=(const DeskSearch&) = delete;

  /// Rows grouped Location, Overlays, Roads, Map. Empty text finds nothing.
  /// A road found by both the graph and the base map is listed once, under
  /// Roads, and a base-map name in several touching pieces once.
  std::vector<DeskSearchRow> Search(const DeskSearchInput& in);

  const DeskSearchNotes& notes() const { return notes_; }

 private:
  void SearchRoads(const DeskSearchInput& in, const app::SearchQuery& q,
                   std::vector<DeskSearchRow>* out);
  void SearchMap(const DeskSearchInput& in, const app::SearchQuery& q,
                 std::vector<DeskSearchRow>* out);

  OverlayManager road_stack_;
  std::shared_ptr<RoadGraphOverlay> roads_;
  const RoutePlanner* roads_planner_ = nullptr;
  std::string roads_failed_;  ///< the load error for `roads_planner_`, kept so it is tried once

  OverlayManager map_stack_;
  std::shared_ptr<VectorMapOverlay> map_;
  const Catalog* map_catalog_ = nullptr;
  int64_t map_series_ = 0;
  std::string map_failed_;  ///< the open error for `map_series_`

  DeskSearchNotes notes_;
};

}  // namespace desk
}  // namespace fv
