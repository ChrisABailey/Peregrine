// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// RemoteFeatureLayer and the tile grid (port/web-features-plan.md WF1),
/// driven by an in-memory service.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fvkit/detail/sqlite.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/feature/web/tiles.h"

namespace fv {
namespace web {
namespace {

namespace fs = std::filesystem;

/// A service over a fixed list of features, paged `page_size` at a time by offset.
class FakeService : public IFeatureService {
 public:
  explicit FakeService(std::vector<Feature> world, size_t page_size = 100)
      : world_(std::move(world)), page_size_(page_size) {}

  Status ListLayers(std::vector<RemoteLayerInfo>* out, const std::atomic<bool>*) override {
    out->push_back(Info());
    return Status::Ok();
  }
  Status Describe(const std::string&, RemoteLayerInfo* out, const std::atomic<bool>*) override {
    *out = Info();
    return Status::Ok();
  }
  Status FetchPage(const std::string&, const GeoRect& box, const std::string& cursor,
                   std::vector<Feature>* out, std::string* next,
                   const std::atomic<bool>* cancel) override {
    while (block_.load() && !cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (cancel->load()) return Status::Error(kInterrupted, "cancelled");
    if (fail_.load()) return Status::Error(kIoError, "HTTP 503");
    std::vector<Feature> hits;
    {
      std::lock_guard<std::mutex> lock(mu_);
      ++pages_;
      for (const Feature& f : world_) {
        GeoRect env;
        if (f.geometry.Envelope(&env) && env.ll.lat <= box.ur.lat && box.ll.lat <= env.ur.lat &&
            env.ll.lon <= box.ur.lon && box.ll.lon <= env.ur.lon)
          hits.push_back(f);
      }
    }
    size_t start = cursor.empty() ? 0 : std::stoul(cursor);
    size_t end = std::min(hits.size(), start + page_size_);
    out->assign(hits.begin() + start, hits.begin() + end);
    *next = end < hits.size() ? std::to_string(end) : "";
    return Status::Ok();
  }

  int pages() {
    std::lock_guard<std::mutex> lock(mu_);
    return pages_;
  }
  void SetWorld(std::vector<Feature> world) {
    std::lock_guard<std::mutex> lock(mu_);
    world_ = std::move(world);
  }

  std::atomic<bool> block_{false};
  std::atomic<bool> fail_{false};

  static RemoteLayerInfo Info() {
    RemoteLayerInfo info;
    info.id = "sites";
    info.title = "Sites";
    info.schema.geometry_type = GeometryType::kPoint;
    FieldDef name;
    name.name = "name";
    name.type = FieldType::kText;
    info.schema.fields.push_back(name);
    info.extent = GeoRect{{32.0, -81.0}, {33.0, -79.0}};
    info.has_extent = true;
    return info;
  }

 private:
  std::mutex mu_;
  std::vector<Feature> world_;
  size_t page_size_;
  int pages_ = 0;
};

Feature PointAt(int64_t id, double lat, double lon) {
  Feature f;
  f.id = id;
  f.geometry = Geometry::Point(GeoPoint{lat, lon});
  f.values.push_back(std::string("site ") + std::to_string(id));
  return f;
}

/// Points on a 0.01-degree grid around Charleston.
std::vector<Feature> Grid(int n) {
  std::vector<Feature> out;
  for (int i = 0; i < n; ++i) out.push_back(PointAt(i + 1, 32.70 + 0.01 * static_cast<double>(i / 10), -80.00 + 0.01 * (i % 10)));
  return out;
}

const GeoRect kView{{32.6, -80.2}, {32.9, -79.8}};

class RemoteLayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = fs::temp_directory_path() / (std::string("fv_remote_") + info->name());
    fs::remove_all(dir_);
    path_ = WebCachePath(dir_.string(), "https://example.test/api");
  }
  void TearDown() override { fs::remove_all(dir_); }

  std::shared_ptr<RemoteFeatureLayer> Open(std::shared_ptr<FakeService> service,
                                           RemoteLayerOptions options = {}) {
    std::shared_ptr<RemoteFeatureLayer> layer;
    Status s = RemoteFeatureLayer::Open(service, FakeService::Info(), path_, std::move(options), &layer);
    EXPECT_TRUE(s.ok()) << s.message;
    return layer;
  }

  fs::path dir_;
  std::string path_;
};

TEST(WebTiles, ZoomFollowsTheBoxWidthWithinTheClamp) {
  EXPECT_EQ(TileZoomForBox(GeoRect{{0, 0}, {1, 1.40625}}, 4, 14), 9);  // 720/1.40625 = 2^9
  EXPECT_EQ(TileZoomForBox(GeoRect::World(), 4, 14), 4);
  EXPECT_EQ(TileZoomForBox(GeoRect{{1, 1}, {1, 1}}, 4, 14), 14);
}

TEST(WebTiles, CoveringTilesSpanTheBoxAndBothSidesOfTheAntimeridian) {
  for (const TileKey& t : TilesCovering(kView, 8)) {
    GeoRect b = TileBounds(t);
    EXPECT_LE(b.ll.lat, kView.ur.lat);
    EXPECT_GE(b.ur.lat, kView.ll.lat);
  }
  GeoRect crossing{{-10, 179}, {10, -179}};
  std::vector<TileKey> tiles = TilesCovering(crossing, 3);
  ASSERT_FALSE(tiles.empty());
  EXPECT_EQ(tiles.front().x, 0);
  EXPECT_EQ(tiles.back().x, 7);
  TileKey t{9, 141, 205};
  for (const TileKey& c : TileChildren(t)) EXPECT_EQ(TileParent(c), t);
}

TEST_F(RemoteLayerTest, AQueryAnswersFromTheCacheAndFetchesTheRest) {
  auto service = std::make_shared<FakeService>(Grid(30));
  auto layer = Open(service);
  uint64_t before = 0, after = 0;
  layer->Revision(&before);
  std::vector<Feature> got;
  ASSERT_TRUE(layer->Query(kView, &got).ok());
  EXPECT_TRUE(got.empty());
  ASSERT_TRUE(layer->WaitIdle(10));
  layer->Revision(&after);
  EXPECT_NE(before, after);
  got.clear();
  ASSERT_TRUE(layer->Query(kView, &got).ok());
  EXPECT_EQ(got.size(), 30u);
  EXPECT_EQ(layer->Count(), 30u);
  Feature f;
  ASSERT_TRUE(layer->Get(7, &f).ok());
  EXPECT_EQ(std::get<std::string>(f.values[0]), "site 7");
  EXPECT_EQ(layer->Schema().geometry_type, GeometryType::kPoint);
}

TEST_F(RemoteLayerTest, AFeatureOnTwoTilesIsStoredOnce) {
  Feature line;
  line.id = 42;
  line.geometry = Geometry::LineString({GeoPoint{32.75, -80.6}, GeoPoint{32.75, -79.4}});
  line.values.push_back(std::string("long"));
  auto service = std::make_shared<FakeService>(std::vector<Feature>{line});
  auto layer = Open(service);
  std::vector<Feature> got;
  layer->Query(GeoRect{{32.5, -80.7}, {33.0, -79.3}}, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_GT(service->pages(), 1);
  EXPECT_EQ(layer->Count(), 1u);
}

TEST_F(RemoteLayerTest, ATileFetchedFartherOutCoversTheTilesInsideIt) {
  auto service = std::make_shared<FakeService>(Grid(30));
  auto layer = Open(service);
  std::vector<Feature> got;
  layer->Query(GeoRect{{30.0, -84.0}, {35.0, -76.0}}, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  int pages = service->pages();
  layer->Query(GeoRect{{32.74, -79.97}, {32.76, -79.95}}, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(service->pages(), pages);
}

TEST_F(RemoteLayerTest, ReopeningTheCacheFetchesNothingAgain) {
  auto service = std::make_shared<FakeService>(Grid(30));
  {
    auto layer = Open(service);
    std::vector<Feature> got;
    layer->Query(kView, &got);
    ASSERT_TRUE(layer->WaitIdle(10));
  }
  int pages = service->pages();
  auto layer = Open(service);
  EXPECT_EQ(layer->Count(), 30u);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(got.size(), 30u);
  EXPECT_EQ(service->pages(), pages);
}

TEST_F(RemoteLayerTest, AChangedSchemaGetsAFreshTableBesideTheOldOne) {
  auto service = std::make_shared<FakeService>(Grid(30));
  {
    auto layer = Open(service);
    std::vector<Feature> got;
    layer->Query(kView, &got);
    ASSERT_TRUE(layer->WaitIdle(10));
  }
  const int pages = service->pages();
  RemoteLayerInfo info = FakeService::Info();
  FieldDef depth;  // the service published another field
  depth.name = "depth";
  depth.type = FieldType::kReal;
  info.schema.fields.push_back(depth);
  std::shared_ptr<RemoteFeatureLayer> layer;
  const Status s = RemoteFeatureLayer::Open(service, info, path_, {}, &layer);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(layer->Name(), "Sites");
  EXPECT_EQ(layer->Count(), 0u);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_GT(service->pages(), pages);
  EXPECT_EQ(layer->Progress().last_error, "");
  EXPECT_EQ(layer->Count(), 30u);
}

TEST_F(RemoteLayerTest, ClosingTheLayerCancelsAFetchInProgress) {
  auto service = std::make_shared<FakeService>(Grid(30));
  service->block_.store(true);
  auto layer = Open(service);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(layer->Progress().tiles_loading, 1u);
  auto start = std::chrono::steady_clock::now();
  layer.reset();
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
}

TEST_F(RemoteLayerTest, TheFeatureCapStopsFetchingAndSaysSo) {
  auto service = std::make_shared<FakeService>(Grid(30));
  RemoteLayerOptions options;
  options.max_features = 10;
  auto layer = Open(service, options);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_TRUE(layer->Progress().capped);
  EXPECT_LE(layer->Count(), 10u);
}

TEST_F(RemoteLayerTest, ATileNeedingTooManyPagesIsSplitIntoItsChildren) {
  auto service = std::make_shared<FakeService>(Grid(30), /*page_size=*/2);
  RemoteLayerOptions options;
  options.max_pages_per_tile = 3;
  auto layer = Open(service, options);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(layer->Count(), 30u);

  detail::SqliteDb db;
  ASSERT_TRUE(db.OpenReadOnly(path_ + "-tiles.sqlite").ok());
  detail::SqliteStmt st;
  ASSERT_TRUE(st.Prepare(db, "SELECT COUNT(*) FROM wf_tiles WHERE state = 2").ok());
  ASSERT_TRUE(st.Step());
  EXPECT_GT(st.ColInt64(0), 0);
}

TEST_F(RemoteLayerTest, AStaleTileIsFetchedAgainAndDropsWhatTheServiceRemoved) {
  auto service = std::make_shared<FakeService>(Grid(30));
  std::atomic<int64_t> now{1000000};
  RemoteLayerOptions options;
  options.refresh_minutes = 5;
  options.now = [&] { return now.load(); };
  auto layer = Open(service, options);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  ASSERT_EQ(layer->Count(), 30u);

  service->SetWorld(Grid(20));
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(layer->Count(), 30u);  // still fresh

  now += 5 * 60;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(layer->Count(), 20u);
  Feature f;
  EXPECT_EQ(layer->Get(25, &f).code, kNotFound);
}

TEST_F(RemoteLayerTest, AFailedTileWaitsBeforeItIsTriedAgain) {
  auto service = std::make_shared<FakeService>(Grid(30));
  service->fail_.store(true);
  std::atomic<int64_t> now{1000000};
  RemoteLayerOptions options;
  options.now = [&] { return now.load(); };
  auto layer = Open(service, options);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(layer->Progress().last_error, "HTTP 503");
  service->fail_.store(false);
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(layer->Count(), 0u);
  now += options.retry_seconds;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_EQ(layer->Count(), 30u);
  EXPECT_TRUE(layer->Progress().last_error.empty());
}

TEST_F(RemoteLayerTest, RefreshEmptiesTheCacheAndTheNextQueryRefetches) {
  auto service = std::make_shared<FakeService>(Grid(30));
  auto layer = Open(service);
  std::vector<Feature> got;
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  ASSERT_TRUE(layer->Refresh().ok());
  EXPECT_EQ(layer->Count(), 0u);
  int pages = service->pages();
  layer->Query(kView, &got);
  ASSERT_TRUE(layer->WaitIdle(10));
  EXPECT_GT(service->pages(), pages);
  EXPECT_EQ(layer->Progress().last_error, "");
  EXPECT_EQ(layer->Count(), 30u);
}

}  // namespace
}  // namespace web
}  // namespace fv
