// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The Find dialog's search: a coordinate first and alone, then the overlay
// stack, the routing graph and the vector base map, in that order. The road
// graph and OSM cases use testdata/OSM/kiawah.* and skip without them.

#include "fv_desk_search.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "fv_desk_fake.h"
#include "fv_osm_format.h"
#include "fv_route_planner.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "scratch.h"

using fv::GeoPoint;
using fv::GeoRect;
using fv::desk::DeskSearch;
using fv::desk::DeskSearchInput;
using fv::desk::DeskSearchRow;
using fv::desk::SearchGroup;

namespace {

std::string TestData(const std::string& rel) {
  const char* root = std::getenv("FVW_TESTDATA_DIR");
  if (root == nullptr) return std::string();
  const std::string p = std::string(root) + "/" + rel;
  return std::filesystem::exists(p) ? p : std::string();
}

/// A search provider that answers any text with one named point.
class FakePlaces : public fv::Overlay, public fv::app::SearchProvider {
 public:
  FakePlaces(std::string name, std::string place, GeoPoint at)
      : fv::Overlay(std::move(name)), place_(std::move(place)), at_(at) {}
  fv::app::SearchProvider* AsSearch() override { return this; }
  fv::Status OnDraw(const fv::MapProjection&, fv::ICanvas&) override { return fv::Status::Ok(); }
  void Search(const fv::app::SearchQuery& q, const std::atomic<bool>&,
              std::vector<fv::app::SearchResult>& out) override {
    fv::app::SearchResult r;
    if (!fv::app::SearchTextAccepts(q, place_, &r.match_quality)) return;
    r.title = place_;
    r.detail = "point";
    r.position = at_;
    r.bounds = GeoRect{at_, at_};
    r.overlay = this;
    out.push_back(r);
  }

 private:
  std::string place_;
  GeoPoint at_;
};

/// kiawah.mbtiles catalogued, and its OSM series as the product on screen.
struct OsmFixture {
  fv::desk::test::Scratch scratch;
  std::shared_ptr<fv::Catalog> catalog;
  fv::view::LadderProduct product;

  void Load(const std::string& file) {
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    fv::RegisterOsmFormat();
    catalog = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(catalog->Open(scratch.Path("catalog.sqlite")).ok());
    int64_t src = 0;
    int added = 0;
    ASSERT_TRUE(catalog->AddDataSource(file, "osm", 0, &src).ok());
    ASSERT_TRUE(catalog->Scan(src, &added).ok());
    std::vector<fv::SeriesRow> series;
    ASSERT_TRUE(catalog->Series(&series).ok());
    ASSERT_FALSE(series.empty());
    product.series_id = series.front().id;
    product.format = series.front().format;
    product.series_key = series.front().series_key;
    product.scale_denom = 25000;
  }
  ~OsmFixture() { fv::ClearFormatRegistryForTest(); }
};

// The middle of Kiawah Island.
const GeoPoint kKiawah{32.607, -80.08};
const GeoRect kKiawahView{{32.58, -80.14}, {32.64, -80.02}};

bool GroupsInOrder(const std::vector<DeskSearchRow>& rows) {
  for (size_t i = 1; i < rows.size(); ++i)
    if (static_cast<int>(rows[i].group) < static_cast<int>(rows[i - 1].group)) return false;
  return true;
}

TEST(SearchLocation, ParsesDecimalAndDms) {
  GeoPoint p;
  ASSERT_TRUE(fv::desk::ParseSearchLocation("33.74 -84.39", &p));
  EXPECT_NEAR(p.lat, 33.74, 1e-9);
  EXPECT_NEAR(p.lon, -84.39, 1e-9);
  ASSERT_TRUE(fv::desk::ParseSearchLocation("33 44 55.7 N 84 23 17.5 W", &p));
  EXPECT_NEAR(p.lat, 33.748806, 1e-5);
  EXPECT_NEAR(p.lon, -84.388194, 1e-5);
  ASSERT_TRUE(fv::desk::ParseSearchLocation("33.74N 84.39W", &p));
  EXPECT_NEAR(p.lon, -84.39, 1e-9);
}

TEST(SearchLocation, ParsesMgrsWithTheGeoTransData) {
  if (std::getenv("MSPCCS_DATA") == nullptr) GTEST_SKIP() << "MSPCCS_DATA not set";
  GeoPoint p;
  ASSERT_TRUE(fv::desk::ParseSearchLocation("16SGC3929137220", &p));
  EXPECT_NEAR(p.lat, 33.74806, 1e-4);
  EXPECT_NEAR(p.lon, -84.41675, 1e-4);
}

TEST(SearchLocation, NamesAndNumbersAreNotLocations) {
  GeoPoint p;
  for (const char* s : {"", "  ", "34", "34 N", "Main", "Peachtree St", "I 75", "US 41",
                        "E 12th St", "17S", "Highway 9", "Kiawah"})
    EXPECT_FALSE(fv::desk::ParseSearchLocation(s, &p)) << s;
}

TEST(SearchLocation, FormatsHemispheres) {
  EXPECT_EQ(fv::desk::FormatSearchLocation({33.74, -84.39}), "33.74000\xC2\xB0 N, 84.39000\xC2\xB0 W");
  EXPECT_EQ(fv::desk::FormatSearchLocation({-12.5, 130.25}), "12.50000\xC2\xB0 S, 130.25000\xC2\xB0 E");
}

TEST(DeskSearch, ACoordinateIsTheWholeAnswer) {
  fv::OverlayManager stack;
  // A provider that would answer anything, to show it is never asked.
  ASSERT_TRUE(stack.Add(std::make_shared<FakePlaces>("Points", "33.74 -84.39", GeoPoint{1, 1})).ok());
  DeskSearch search;
  DeskSearchInput in;
  in.text = "33.74 -84.39";
  in.overlays = &stack;
  in.near = GeoPoint{33.0, -84.0};
  const std::vector<DeskSearchRow> rows = search.Search(in);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].group, SearchGroup::kLocation);
  EXPECT_NEAR(rows[0].position.lat, 33.74, 1e-9);
  EXPECT_GT(rows[0].distance_m, 80000.0);
  EXPECT_EQ(fv::desk::SearchStatusLine(rows, search.notes(), in.text, true), "Coordinate");
}

TEST(DeskSearch, EmptyTextFindsNothing) {
  fv::OverlayManager stack;
  ASSERT_TRUE(stack.Add(std::make_shared<FakePlaces>("Points", "Anything", kKiawah)).ok());
  DeskSearch search;
  DeskSearchInput in;
  in.text = "   ";
  in.overlays = &stack;
  EXPECT_TRUE(search.Search(in).empty());
  EXPECT_EQ(fv::desk::SearchStatusLine({}, search.notes(), in.text, true), "");
}

TEST(DeskSearch, OverlaysAreNamedBySourceAndGrouped) {
  fv::OverlayManager stack;
  ASSERT_TRUE(stack.Add(std::make_shared<FakePlaces>("My Points", "Ruddy Turnstone", kKiawah)).ok());
  DeskSearch search;
  DeskSearchInput in;
  in.text = "ruddy";
  in.overlays = &stack;
  const std::vector<DeskSearchRow> rows = search.Search(in);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].group, SearchGroup::kOverlays);
  EXPECT_EQ(rows[0].source, "My Points");
  EXPECT_EQ(rows[0].kind, fv::app::SearchKind::kPoint);
  EXPECT_EQ(fv::desk::SearchStatusLine(rows, search.notes(), in.text, true), "1 result");
}

TEST(DeskSearch, TheRoadNetworkIsSearchedWhole) {
  const std::string graph = TestData("OSM/kiawah.fvroad");
  if (graph.empty()) GTEST_SKIP() << "no testdata/OSM/kiawah.fvroad";
  fv::RoutePlanner planner(graph);
  fv::OverlayManager stack;
  DeskSearch search;
  DeskSearchInput in;
  in.text = "Ruddy Turnstone";
  in.overlays = &stack;
  in.planner = &planner;
  in.near = kKiawah;
  // No view at all: the graph does not need one.
  const std::vector<DeskSearchRow> rows = search.Search(in);
  ASSERT_FALSE(rows.empty());
  EXPECT_EQ(rows[0].group, SearchGroup::kRoads);
  EXPECT_EQ(rows[0].kind, fv::app::SearchKind::kRoad);
  EXPECT_EQ(rows[0].title, "Ruddy Turnstone");
  EXPECT_TRUE(search.notes().roads_error.empty());
}

TEST(DeskSearch, AGraphThatWillNotLoadIsReportedOnce) {
  fv::RoutePlanner planner("/nonexistent/kiawah.fvroad");
  fv::OverlayManager stack;
  DeskSearch search;
  DeskSearchInput in;
  in.text = "anything";
  in.overlays = &stack;
  in.planner = &planner;
  EXPECT_TRUE(search.Search(in).empty());
  EXPECT_FALSE(search.notes().roads_error.empty());
  const std::string line = fv::desk::SearchStatusLine({}, search.notes(), in.text, true);
  EXPECT_EQ(line.rfind("Nothing found \xC2\xB7 road network: ", 0), 0u) << line;
  // The second search does not retry, but still says so.
  EXPECT_TRUE(search.Search(in).empty());
  EXPECT_FALSE(search.notes().roads_error.empty());
}

TEST(DeskSearch, TheBaseMapAnswersInTheViewAfterTheRoads) {
  const std::string tiles = TestData("OSM/kiawah.mbtiles");
  const std::string graph = TestData("OSM/kiawah.fvroad");
  if (tiles.empty() || graph.empty()) GTEST_SKIP() << "no testdata/OSM/kiawah.*";
  OsmFixture f;
  ASSERT_NO_FATAL_FAILURE(f.Load(tiles));
  fv::RoutePlanner planner(graph);
  fv::OverlayManager stack;
  ASSERT_TRUE(stack.Add(std::make_shared<FakePlaces>("My Points", "Ruddy Turnstone", kKiawah)).ok());
  DeskSearch search;
  DeskSearchInput in;
  in.text = "Turnstone";
  in.overlays = &stack;
  in.planner = &planner;
  in.catalog = f.catalog;
  in.product = f.product;
  in.near = kKiawah;
  in.view = kKiawahView;
  const std::vector<DeskSearchRow> rows = search.Search(in);
  EXPECT_TRUE(search.notes().map_searched);
  EXPECT_TRUE(search.notes().map_error.empty()) << search.notes().map_error;
  EXPECT_TRUE(GroupsInOrder(rows));
  int overlays = 0, roads = 0, map = 0;
  for (const DeskSearchRow& r : rows) {
    overlays += r.group == SearchGroup::kOverlays;
    roads += r.group == SearchGroup::kRoads;
    map += r.group == SearchGroup::kMap;
  }
  EXPECT_EQ(overlays, 1);
  EXPECT_GT(roads, 0);
  // The street the graph found is not listed again from the tiles, and no
  // name is listed twice from the tiles in one place.
  for (size_t i = 0; i < rows.size(); ++i) {
    const DeskSearchRow& m = rows[i];
    if (m.group != SearchGroup::kMap) continue;
    for (size_t j = 0; j < i; ++j) {
      const DeskSearchRow& r = rows[j];
      if (r.group != SearchGroup::kRoads && r.group != SearchGroup::kMap) continue;
      fv::app::SearchRow a, b;
      a.title = r.title, a.kind = r.kind, a.position = r.position, a.bounds = r.bounds;
      b.title = m.title, b.kind = m.kind, b.position = m.position, b.bounds = m.bounds;
      EXPECT_FALSE(fv::app::SearchRowsAreDuplicates(a, b, 250.0)) << m.title;
    }
  }
  // Something on the island has a name only the chart knows.
  in.text = "Kiawah";
  const std::vector<DeskSearchRow> named = search.Search(in);
  map = 0;
  for (const DeskSearchRow& r : named) map += r.group == SearchGroup::kMap;
  EXPECT_GT(map, 0);
  for (const DeskSearchRow& r : named)
    if (r.group == SearchGroup::kMap) EXPECT_EQ(r.source, f.product.series_key);
}

TEST(DeskSearch, TheBaseMapOutsideTheViewNeedsANameIndex) {
  const std::string tiles = TestData("OSM/kiawah.mbtiles");
  if (tiles.empty()) GTEST_SKIP() << "no testdata/OSM/kiawah.mbtiles";
  OsmFixture f;
  ASSERT_NO_FATAL_FAILURE(f.Load(tiles));
  fv::OverlayManager stack;
  DeskSearch search;
  DeskSearchInput in;
  in.text = "Kiawah";
  in.overlays = &stack;
  in.catalog = f.catalog;
  in.product = f.product;
  in.view = kKiawahView;
  in.map_view_only = false;
  const std::vector<DeskSearchRow> rows = search.Search(in);
  ASSERT_TRUE(search.notes().map_searched);
  const std::string line = fv::desk::SearchStatusLine(rows, search.notes(), in.text, false);
  if (search.notes().map_used_index)
    EXPECT_EQ(line.find("name index"), std::string::npos) << line;
  else
    EXPECT_NE(line.find("no name index"), std::string::npos) << line;
}

TEST(DeskSearch, ARasterMapIsSaidToBeUnsearchable) {
  fv::OverlayManager stack;
  DeskSearch search;
  DeskSearchInput in;
  in.text = "Gainesville";
  in.overlays = &stack;
  in.catalog = std::make_shared<fv::Catalog>();
  in.product = fv::view::LadderProduct{1, "cadrg", "LFC", 500000};
  EXPECT_TRUE(search.Search(in).empty());
  EXPECT_TRUE(search.notes().map_is_raster);
  EXPECT_EQ(fv::desk::SearchStatusLine({}, search.notes(), in.text, true),
            "Nothing found \xC2\xB7 the map on screen is not a vector map");
}

TEST(SearchFitScale, FitsTheBoxWithAMarginAndAFloor) {
  // 0.01° of latitude is 1113 m; a 100 mm tall view at 1:13,358 shows 1336 m.
  const GeoRect tall{{32.60, -80.08}, {32.61, -80.08}};
  EXPECT_NEAR(fv::desk::SearchFitScale(tall, 1000, 400, 0.25, 0), 13358.4, 1.0);
  // A short street would be fitted at 1:300; the floor holds it at 1:2,500.
  const GeoRect tiny{{32.6000, -80.0800}, {32.6001, -80.0800}};
  EXPECT_DOUBLE_EQ(fv::desk::SearchFitScale(tiny, 1000, 400, 0.25, 2500), 2500);
  // A point has no extent: keep the scale.
  EXPECT_EQ(fv::desk::SearchFitScale(GeoRect{kKiawah, kKiawah}, 1000, 400, 0.25, 2500), 0.0);
}

TEST(DeskSearch, TheGoToCommandAsksTheShellForFind) {
  fv::desk::FakeDesk fd;
  ASSERT_TRUE(fd.Run({"map.goto"}).ok());
  EXPECT_EQ(fd.shell().find_shown, 1);
}

}  // namespace
