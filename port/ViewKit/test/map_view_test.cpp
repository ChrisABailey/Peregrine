// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MapView: input to camera, cursor-anchored ladder steps, and the ladder fed
// from a real catalog over stub formats.

#include "fv_view_map_view.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fv_map_enums.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::view::LadderKind;
using fv::view::LadderOutcome;
using fv::view::LadderProduct;
using fv::view::MapView;
using fv::view::PointF;
using fv::view::ProductsAt;
using fv::view::Viewport;

void ExpectSameGeo(const GeoPoint& a, const GeoPoint& b) {
  EXPECT_NEAR(a.lat, b.lat, 1e-8);
  EXPECT_NEAR(a.lon, b.lon, 1e-8);
}

// A product and where it has data.
struct Coverage {
  LadderProduct product;
  GeoRect rect;
};

ProductsAt FromCoverage(std::vector<Coverage> cov) {
  return [cov](const GeoPoint& p) {
    std::vector<LadderProduct> out;
    for (const Coverage& c : cov)
      if (c.rect.Contains(p)) out.push_back(c.product);
    return out;
  };
}

// ENC around Charleston: coastal over the whole window, harbour over the
// north-west quarter only.
const GeoRect kWide{{20.0, -100.0}, {50.0, -60.0}};
const GeoRect kHarbourBox{{32.78, -80.6}, {33.2, -79.93}};
const LadderProduct kCoastal{20, "enc", "Coastal", 350e3};
const LadderProduct kHarbour{21, "enc", "Harbour", 12e3};

MapView EncView() {
  const Viewport v = Viewport::Make(GeoPoint{32.78, -79.93}, 350e3)
                         .WithSurface(1200, 800, 2.0, 0.25);
  return MapView(v, LadderKind::kUniform,
                 FromCoverage({{kCoastal, kWide}, {kHarbour, kHarbourBox}}));
}

TEST(MapView, OpensOnTheNearestProduct) {
  MapView mv = EncView();
  ASSERT_TRUE(mv.HasProduct());
  EXPECT_EQ(mv.Product().series_key, "Coastal");
}

TEST(MapView, KeyStepIsAnchoredAtTheCursor) {
  MapView mv = EncView();
  const PointF cursor{50, 60};  // north-west of centre, inside the harbour box
  mv.Hover(cursor);
  const GeoPoint under = mv.View().GeoAt(cursor);
  ASSERT_TRUE(kHarbourBox.Contains(under));
  for (int i = 0; i < 3; ++i) {
    mv.Step(+1);
    ExpectSameGeo(mv.View().GeoAt(cursor), under);
  }
  EXPECT_DOUBLE_EQ(mv.View().ScaleDenom(), 350e3 / 8);
  EXPECT_EQ(mv.Product().series_key, "Harbour");
}

TEST(MapView, HarbourOnlyWhereItCoversTheCursor) {
  MapView mv = EncView();
  const PointF cursor{1150, 750};  // south-east: coastal only
  ASSERT_FALSE(kHarbourBox.Contains(mv.View().GeoAt(cursor)));
  for (int i = 0; i < 4; ++i) mv.StepAt(+1, cursor);
  EXPECT_EQ(mv.Product().series_key, "Coastal");
  EXPECT_DOUBLE_EQ(mv.View().ScaleDenom(), 350e3 / 16);
}

TEST(MapView, WithoutACursorKeysZoomAboutTheCentre) {
  MapView mv = EncView();
  mv.Hover(PointF{5, 5});
  mv.HoverExit();
  const GeoPoint c = mv.View().Center();
  mv.Step(+1);
  ExpectSameGeo(mv.View().GeoAt(mv.View().SurfaceCenter()), c);
}

TEST(MapView, DragPansAndBumpsTheGeneration) {
  MapView mv = EncView();
  const uint64_t g0 = mv.Generation();
  const GeoPoint g = mv.View().GeoAt(PointF{100, 100});
  mv.PointerDown(PointF{100, 100});
  EXPECT_TRUE(mv.InGesture());
  mv.PointerDrag(PointF{130, 90});
  mv.PointerUp(PointF{150, 80});
  EXPECT_FALSE(mv.InGesture());
  PointF q;
  ASSERT_TRUE(mv.View().PointFor(g, &q));
  EXPECT_NEAR(q.x, 150, 1e-6);
  EXPECT_NEAR(q.y, 80, 1e-6);
  EXPECT_GT(mv.Generation(), g0);
}

TEST(MapView, PreciseScrollPansNotchedScrollSteps) {
  MapView mv = EncView();
  const double den = mv.View().ScaleDenom();
  mv.Scroll(PointF{600, 400}, 10, 20, true);
  EXPECT_EQ(mv.View().ScaleDenom(), den);
  mv.Scroll(PointF{600, 400}, 0, 2, false);  // two notches in
  EXPECT_DOUBLE_EQ(mv.View().ScaleDenom(), den / 4);
  mv.Scroll(PointF{600, 400}, 0, -1, false);
  EXPECT_DOUBLE_EQ(mv.View().ScaleDenom(), den / 2);
}

TEST(MapView, PinchPreviewsLiveAndSettlesOnASeriesStep) {
  const LadderProduct gnc{2, "cadrg", "GNC", 5e6};
  const LadderProduct tif{3, "geotiff", "Color", 2e6};
  const LadderProduct onc{4, "cadrg", "ONC", 1e6};
  const Viewport v = Viewport::Make(GeoPoint{35, -80}, 5e6).WithSurface(1000, 700, 1, 0.25);
  MapView mv(v, LadderKind::kSeries,
             FromCoverage({{gnc, kWide}, {tif, kWide}, {onc, kWide}}));
  EXPECT_EQ(mv.Product().series_key, "GNC");
  const PointF at{700, 200};
  const GeoPoint under = mv.View().GeoAt(at);
  mv.MagnifyBegin(at);
  mv.Magnify(at, 1.5);
  mv.Magnify(at, 1.5);  // 1:2.22M, live
  EXPECT_NEAR(mv.View().ScaleDenom(), 5e6 / 2.25, 1);
  EXPECT_EQ(mv.Product().series_key, "GNC");
  mv.MagnifyEnd(at);
  EXPECT_EQ(mv.View().ScaleDenom(), 2e6);
  EXPECT_EQ(mv.Product().series_key, "Color");
  ExpectSameGeo(mv.View().GeoAt(at), under);
}

TEST(MapView, EndOfTheSeriesLadderStopsAndSaysSo) {
  const LadderProduct onc{4, "cadrg", "ONC", 1e6};
  const Viewport v = Viewport::Make(GeoPoint{35, -80}, 1e6).WithSurface(800, 600, 1, 0.25);
  MapView mv(v, LadderKind::kSeries, FromCoverage({{onc, kWide}}));
  const Viewport before = mv.View();
  const uint64_t gen = mv.Generation();
  const auto s = mv.Step(+1);
  EXPECT_EQ(s.outcome, LadderOutcome::kEndOfLadder);
  EXPECT_EQ(mv.LastOutcome(), LadderOutcome::kEndOfLadder);
  EXPECT_EQ(mv.View(), before);
  EXPECT_EQ(mv.Generation(), gen);
}

// MARK: The raster ladder through a real catalog

// Stub enumerators named after the real formats, each yielding the frames
// listed for its key. No file is opened by a scan.
std::map<std::string, std::vector<fv::FrameInfo>>& StubFrames() {
  static std::map<std::string, std::vector<fv::FrameInfo>> frames;
  return frames;
}

class StubEnumerator : public fv::IFrameEnumerator {
 public:
  explicit StubEnumerator(std::string key) : key_(std::move(key)) {}
  fv::Status Begin(const std::string&) override {
    next_ = 0;
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* info) override {
    const auto& f = StubFrames()[key_];
    if (next_ >= f.size()) return false;
    *info = f[next_++];
    return true;
  }

 private:
  std::string key_;
  size_t next_ = 0;
};

fv::FrameInfo Frame(const char* path, GeoRect r, const char* key, double scale,
                    int units) {
  fv::FrameInfo f;
  f.path = path;
  f.bounds = r;
  f.series_key = key;
  f.scale = scale;
  f.scale_units = units;
  f.size_bytes = 1;
  return f;
}

class CatalogLadder : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    const GeoRect world_ish{{20, -100}, {50, -60}};
    const GeoRect tile{{34, -81}, {36, -79}};
    const GeoRect elsewhere{{40, -75}, {41, -74}};
    StubFrames()["tiros"] = {Frame("t", world_ish, "TIROS", 1.0, MAP_SCALE_KILOMETER)};
    StubFrames()["cadrg"] = {Frame("gnc", world_ish, "GNC", 5e6, MAP_SCALE_DENOMINATOR),
                             Frame("onc", tile, "ONC", 1e6, MAP_SCALE_DENOMINATOR),
                             Frame("tpc", elsewhere, "TPC", 500e3, MAP_SCALE_DENOMINATOR)};
    StubFrames()["geotiff"] = {Frame("sec", tile, "Color", 2e6, MAP_SCALE_DENOMINATOR)};
    StubFrames()["dted"] = {Frame("d", world_ish, "DTED1", 0, 0)};
    ASSERT_TRUE(cat_.Open(":memory:").ok());
    for (const char* key : {"tiros", "cadrg", "geotiff", "dted"}) {
      fv::FormatFactories f;
      f.format_key = key;
      const std::string k = key;
      f.make_enumerator = [k] { return std::make_shared<StubEnumerator>(k); };
      ASSERT_TRUE(fv::RegisterFormat(f).ok());
      int64_t src = 0;
      int added = 0;
      ASSERT_TRUE(cat_.AddDataSource(std::string("/stub/") + key, key, 0, &src).ok());
      ASSERT_TRUE(cat_.Scan(src, &added).ok());
    }
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  fv::Catalog cat_;
};

TEST_F(CatalogLadder, ProductsAtListsScaledSeriesOfTheGroupFinestFirst) {
  const auto here = fv::view::CatalogProductsAt(cat_, GeoPoint{35, -80},
                                                {"tiros", "cadrg", "geotiff", "dted"});
  ASSERT_EQ(here.size(), 4u);  // DTED has no scale; the TPC is elsewhere
  EXPECT_EQ(here[0].series_key, "ONC");
  EXPECT_EQ(here[1].series_key, "Color");
  EXPECT_EQ(here[2].series_key, "GNC");
  EXPECT_EQ(here[3].series_key, "TIROS");
  EXPECT_GT(here[3].scale_denom, 5e6);
  EXPECT_TRUE(fv::view::CatalogProductsAt(cat_, GeoPoint{35, -80}, {"enc"}).empty());
}

TEST_F(CatalogLadder, TirosToCadrgToGeoTiffToCadrgUnderTheCursor) {
  const std::vector<std::string> raster = {"tiros", "cadrg", "geotiff"};
  const fv::Catalog& cat = cat_;
  ProductsAt at = [&cat, raster](const GeoPoint& p) {
    return fv::view::CatalogProductsAt(cat, p, raster);
  };
  const auto tiros = at(GeoPoint{35, -80}).back();
  ASSERT_EQ(tiros.series_key, "TIROS");
  // The window is centred off the 1:1M tile; the cursor sits on it.
  const Viewport v = Viewport::Make(GeoPoint{33, -83}, tiros.scale_denom)
                         .WithSurface(1000, 700, 1, 0.25);
  MapView mv(v, LadderKind::kSeries, at);
  EXPECT_EQ(mv.Product().series_key, "TIROS");
  PointF cursor;
  ASSERT_TRUE(mv.View().PointFor(GeoPoint{35, -80}, &cursor));
  mv.Hover(cursor);

  std::vector<std::string> seen;
  for (int i = 0; i < 4; ++i) {
    const auto s = mv.Step(+1);
    if (s.outcome != LadderOutcome::kStepped) break;
    seen.push_back(mv.Product().format + ":" + mv.Product().series_key);
    EXPECT_EQ(mv.View().ScaleDenom(), mv.Product().scale_denom);
    ExpectSameGeo(mv.View().GeoAt(cursor), GeoPoint{35, -80});
  }
  EXPECT_EQ(seen, (std::vector<std::string>{"cadrg:GNC", "geotiff:Color", "cadrg:ONC"}));
  EXPECT_EQ(mv.LastOutcome(), LadderOutcome::kEndOfLadder);

  // At the window centre, off the tile, GNC is the end of the ladder.
  mv.HoverExit();
  mv.SetViewport(v);
  mv.SetGroup(LadderKind::kSeries, at);
  mv.Step(+1);
  EXPECT_EQ(mv.Product().series_key, "GNC");
  EXPECT_EQ(mv.Step(+1).outcome, LadderOutcome::kEndOfLadder);
}

}  // namespace
