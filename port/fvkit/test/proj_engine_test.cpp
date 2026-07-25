// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// L3a MapProjection + L3b MapEngine tests. Projection math is checked
// against MapScaleUtil directly (the plan's requirement); engine renders are
// pinned by hash after visual verification, like the canvas goldens.

#include "fvkit/engine.h"
#include "fvkit/proj.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "fv_map_enums.h"
#include "fv_map_scale_util.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/registry.h"
#include "fvkit/tools/png_write.h"

namespace fs = std::filesystem;

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? d : "";
}

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int i = 0; i < b.Width() * 4; ++i) {
      h ^= row[i];
      h *= 1099511628211ull;
    }
  }
  return h;
}

// ---------------------------------------------------------------------------
// MapProjection (always runs)
// ---------------------------------------------------------------------------

TEST(MapProjection, DppMatchesMapScaleUtil) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({33.75, -84.39}).ok());
  ASSERT_TRUE(p.SetScale(500000.0).ok());
  ASSERT_TRUE(p.Ready());

  double dlat = 0, dlon = 0;
  fv::MapScaleUtil util;
  ASSERT_EQ(util.GetDegreesPerPixel(33.75, 500000.0, MAP_SCALE_DENOMINATOR,
                                    &dlat, &dlon),
            0);
  EXPECT_DOUBLE_EQ(p.DegPerPixelLat(), dlat);
  EXPECT_DOUBLE_EQ(p.DegPerPixelLon(), dlon);
}

TEST(MapProjection, RoundTripAndBounds) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(801, 601).ok());
  ASSERT_TRUE(p.SetCenter({33.75, -84.39}).ok());
  ASSERT_TRUE(p.SetScale(500000.0).ok());

  // center maps to the surface midpoint
  double sx = 0, sy = 0;
  ASSERT_TRUE(p.GeoToSurface({33.75, -84.39}, &sx, &sy).ok());
  EXPECT_NEAR(sx, 400.0, 1e-9);
  EXPECT_NEAR(sy, 300.0, 1e-9);

  // round-trip an arbitrary surface point
  fv::GeoPoint g;
  ASSERT_TRUE(p.SurfaceToGeo(123.0, 456.0, &g).ok());
  ASSERT_TRUE(p.GeoToSurface(g, &sx, &sy).ok());
  EXPECT_NEAR(sx, 123.0, 1e-9);
  EXPECT_NEAR(sy, 456.0, 1e-9);

  // bounds contain the center and match dpp arithmetic
  fv::GeoRect b = p.VmapBounds();
  EXPECT_TRUE(b.Contains({33.75, -84.39}));
  EXPECT_NEAR(b.ur.lat - b.ll.lat, 601 * p.DegPerPixelLat(), 1e-9);
}

TEST(MapProjection, AntimeridianViewport) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(400, 300).ok());
  ASSERT_TRUE(p.SetCenter({-17.0, 179.9}).ok());  // Fiji-ish
  ASSERT_TRUE(p.SetScale(2000000.0).ok());

  fv::GeoRect b = p.VmapBounds();
  EXPECT_TRUE(b.CrossesAntimeridian());
  // a point just west of the dateline lands left of center; just east lands
  // right of center — through the wrap
  double sx_w, sy_w, sx_e, sy_e;
  ASSERT_TRUE(p.GeoToSurface({-17.0, 179.0}, &sx_w, &sy_w).ok());
  ASSERT_TRUE(p.GeoToSurface({-17.0, -179.5}, &sx_e, &sy_e).ok());
  EXPECT_LT(sx_w, 200.0);
  EXPECT_GT(sx_e, 200.0);
}

TEST(MapProjection, NotConfiguredErrors) {
  fv::MapProjection p;
  double sx, sy;
  EXPECT_EQ(p.GeoToSurface({0, 0}, &sx, &sy).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetScale(-5).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetSurfaceSize(0, 10).code, fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Physical-display scale (the native-scale / correct-aspect path)
// ---------------------------------------------------------------------------

// The user's worked example: 1:1,000,000 on a 0.25 mm/px screen puts ~10 km
// of ground under 1 cm (40 px) of screen at the center.
TEST(MapProjection, PhysicalScalePutsNativeGroundUnderTheRuler) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(800, 600).ok());
  ASSERT_TRUE(p.SetCenter({33.75, -84.39}).ok());
  ASSERT_TRUE(p.SetPhysicalScale(1000000.0, 0.25).ok());
  ASSERT_TRUE(p.Ready());
  EXPECT_DOUBLE_EQ(p.Scale(), 1000000.0);
  EXPECT_DOUBLE_EQ(p.MmPerPixel(), 0.25);

  // 40 px = 1 cm of screen. Measure the ground span north-south through the
  // center via surface_to_geo, then convert degrees of latitude to metres
  // (~111.32 km/deg). Expect ~10 km, generously toleranced for the ellipsoid.
  fv::GeoPoint a, b;
  ASSERT_TRUE(p.SurfaceToGeo(400, 300 - 20, &a).ok());
  ASSERT_TRUE(p.SurfaceToGeo(400, 300 + 20, &b).ok());
  const double ground_m = std::fabs(a.lat - b.lat) * 111320.0;
  EXPECT_NEAR(ground_m, 10000.0, 400.0);
}

// Correct aspect: one screen pixel must cover the SAME ground distance
// vertically and horizontally, at a latitude where dpp_lat != dpp_lon.
// This is the crux of the user's "aspect ratio is off" report: the ratio
// dpp_lon/dpp_lat must be ~1/cos(lat) (a degree of longitude is physically
// shorter away from the equator), NOT the ~2.1 that MapScaleUtil's Vincenty
// path produced.
TEST(MapProjection, PhysicalScaleHasSquareGroundPixels) {
  const double lat = 45.0;
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(512, 512).ok());
  ASSERT_TRUE(p.SetCenter({lat, 10.0}).ok());
  ASSERT_TRUE(p.SetPhysicalScale(2000000.0, 0.25).ok());

  // Convert each pixel's degrees to ground metres via the same WGS84 series
  // the projection uses; the two must be within a fraction of a percent.
  const double m_per_deg_lat = 111132.92 - 559.82 * std::cos(2 * lat * M_PI / 180);
  const double m_per_deg_lon = 111412.84 * std::cos(lat * M_PI / 180) -
                               93.5 * std::cos(3 * lat * M_PI / 180);
  const double ground_lat = p.DegPerPixelLat() * m_per_deg_lat;
  const double ground_lon = p.DegPerPixelLon() * m_per_deg_lon;
  // Square to ~1e-5 (the test drops one high-order term the projection keeps).
  EXPECT_NEAR(ground_lon, ground_lat, ground_lat * 1e-4);

  // Aspect ratio ~ 1/cos(45) = 1.414, and firmly NOT the ~1.99 the old
  // Vincenty path returned here.
  const double ratio = p.DegPerPixelLon() / p.DegPerPixelLat();
  EXPECT_NEAR(ratio, 1.0 / std::cos(lat * M_PI / 180), 0.01);
  EXPECT_LT(ratio, 1.6);
}

// mm_per_pixel is the zoom knob: doubling it doubles ground per pixel.
TEST(MapProjection, MmPerPixelZooms) {
  fv::MapProjection near_, far_;
  for (fv::MapProjection* p : {&near_, &far_}) {
    ASSERT_TRUE(p->SetSurfaceSize(256, 256).ok());
    ASSERT_TRUE(p->SetCenter({0.0, 0.0}).ok());
  }
  ASSERT_TRUE(near_.SetPhysicalScale(1000000.0, 0.25).ok());
  ASSERT_TRUE(far_.SetPhysicalScale(1000000.0, 0.50).ok());
  EXPECT_NEAR(far_.DegPerPixelLat(), 2.0 * near_.DegPerPixelLat(), 1e-12);
}

TEST(MapProjection, PhysicalScaleRejectsBadArgs) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(64, 64).ok());
  ASSERT_TRUE(p.SetCenter({0, 0}).ok());
  EXPECT_EQ(p.SetPhysicalScale(-1.0, 0.25).code, fv::kInvalidArg);
  EXPECT_EQ(p.SetPhysicalScale(1000000.0, 0.0).code, fv::kInvalidArg);
}

// Engine convenience: a cartographic series passes its denominator straight
// through; a metres-resolution series (imagery) lands at exactly 100% at the
// reference pitch — one source pixel per screen pixel.
TEST(MapEnginePhysical, ImageryRendersAtNativeResolution) {
  auto catalog = std::make_shared<fv::Catalog>();
  fv::MapEngine engine(catalog);
  ASSERT_TRUE(engine.SetSurfaceDimensions(512, 512).ok());
  ASSERT_TRUE(engine.SetCenter({40.0, -80.0}).ok());

  // 1 m/px imagery at the native pitch must render at 100%: one screen pixel
  // spans one ground metre north-south.
  ASSERT_TRUE(engine.SetPhysicalScale(1.0, MAP_SCALE_METERS,
                                      fv::kNativeDisplayMmPerPixel)
                  .ok());
  const double lat = 40.0;
  const double m_per_deg_lat = 111132.92 - 559.82 * std::cos(2 * lat * M_PI / 180);
  const double ground_m = engine.CurrentProj().DegPerPixelLat() * m_per_deg_lat;
  EXPECT_NEAR(ground_m, 1.0, 1e-3);

  // A cartographic series: denominator used directly.
  ASSERT_TRUE(engine.SetPhysicalScale(500000.0, MAP_SCALE_DENOMINATOR, 0.25).ok());
  EXPECT_DOUBLE_EQ(engine.CurrentProj().Scale(), 500000.0);
}

// ---------------------------------------------------------------------------
// MapEngine synthetic: a dateline-crossing frame composites on both sides
// ---------------------------------------------------------------------------

// Solid-color 100x100 "frame" spanning lon 175 -> -178 (width 7 deg),
// lat -20 -> -15, with linear equal-arc transforms.
class StubRaster : public fv::IRasterSource {
 public:
  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  fv::GeoRect Bounds() const override {
    return fv::GeoRect{{-20.0, 175.0}, {-15.0, -178.0}};
  }
  fv::Status Info(fv::ImageInfo* i) const override {
    i->size = {100, 100};
    i->bounds = Bounds();
    return fv::Status::Ok();
  }
  fv::Status ReadBlock(const fv::PixelRect& r, fv::PixelBuffer* out) override {
    *out = fv::PixelBuffer(r.width, r.height);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        unsigned char* p = out->Row(y) + 4 * x;
        p[0] = 200; p[1] = 40; p[2] = 40; p[3] = 255;
      }
    return fv::Status::Ok();
  }
  fv::Status PixelToGeo(double px, double py, fv::GeoPoint* p) const override {
    p->lon = fv::NormalizeLon(175.0 + px * (7.0 / 100.0));
    p->lat = -15.0 - py * (5.0 / 100.0);
    return fv::Status::Ok();
  }
  fv::Status GeoToPixel(const fv::GeoPoint& p, double* px,
                        double* py) const override {
    *px = (fv::UnwrapLonNear(p.lon, 178.5) - 175.0) / (7.0 / 100.0);
    *py = (-15.0 - p.lat) / (5.0 / 100.0);
    return fv::Status::Ok();
  }
};

class StubEnum : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    done_ = false;
    dir_ = dir;
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* f) override {
    if (done_) return false;
    done_ = true;
    f->path = dir_ + "/stub.frame";
    f->bounds = fv::GeoRect{{-20.0, 175.0}, {-15.0, -178.0}};
    f->series_key = "STUB";
    f->scale = 1000000.0;
    f->scale_units = 0;
    f->size_bytes = 1;
    return true;
  }

 private:
  std::string dir_;
  bool done_ = true;
};

TEST(EngineSynthetic, DatelineFrameCompositesBothSides) {
  fv::ClearFormatRegistryForTest();
  fv::FormatFactories f;
  f.format_key = "stubr";
  f.make_enumerator = [] { return std::make_shared<StubEnum>(); };
  f.make_raster_source = [] { return std::make_shared<StubRaster>(); };
  ASSERT_TRUE(fv::RegisterFormat(f).ok());

  auto cat = std::make_shared<fv::Catalog>();
  ASSERT_TRUE(cat->Open(":memory:").ok());
  int64_t id = 0;
  int n = 0;
  ASSERT_TRUE(cat->AddDataSource("/stub", "stubr", 0, &id).ok());
  ASSERT_TRUE(cat->Scan(id, &n).ok());
  ASSERT_EQ(n, 1);

  fv::MapEngine engine(cat);
  ASSERT_TRUE(engine.SetSurfaceDimensions(200, 150).ok());
  ASSERT_TRUE(engine.SetCenter({-17.5, 178.5}).ok());  // frame center, on AM
  // 1:40M -> viewport ~11 deg wide, wider than the 7-deg frame, so both
  // frame edges are on-surface
  ASSERT_TRUE(engine.SetScale(40000000.0).ok());

  fv::CpuCanvas canvas(200, 150);
  canvas.Clear(fv::FvColor{0, 0, 0, 255});
  int drawn = 0;
  fv::Status s = engine.RenderBaseMap(canvas, 0, {}, &drawn);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(drawn, 1);

  auto sample = [&](double lon, const char* what) -> int {
    double sx = 0, sy = 0;
    EXPECT_TRUE(
        engine.CurrentProj().GeoToSurface({-17.5, lon}, &sx, &sy).ok());
    EXPECT_GE((int)sx, 0) << what;
    EXPECT_LT((int)sx, 200) << what;  // probe must be on-surface
    EXPECT_GE((int)sy, 0) << what;
    EXPECT_LT((int)sy, 150) << what;
    return canvas.Buffer().Row((int)sy)[4 * (int)sx];
  };
  // the frame paints pixels BOTH west and east of the dateline column
  EXPECT_EQ(sample(176.0, "west of AM"), 200);
  EXPECT_EQ(sample(-179.0, "east of AM"), 200);
  // inside the viewport but beyond the frame's east edge (182 = -178):
  // untouched background
  EXPECT_EQ(sample(-177.0, "beyond east edge"), 0);
  fv::ClearFormatRegistryForTest();
}

// ---------------------------------------------------------------------------
// MapEngine (real data)
// ---------------------------------------------------------------------------

// Pinned 2026-07-17 after visually verifying fvrender_atlanta.png (LFC
// chart over Atlanta: city label, Hartsfield-Jackson, seamless frames,
// correct resample). 0 = probe mode.
constexpr uint64_t kHashAtlanta = 0xafffcd30a1aee1aaull;

class EngineReal : public ::testing::Test {
 protected:
  void SetUp() override {
    td_ = TestDataDir();
    if (td_.empty() || !fs::exists(td_ + "/rpf")) GTEST_SKIP();
    fv::ClearFormatRegistryForTest();
    fv::RegisterBuiltinFormats();
    catalog_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(catalog_->Open(":memory:").ok());
    int64_t id = 0;
    int n = 0;
    ASSERT_TRUE(catalog_->AddDataSource(td_ + "/rpf", "cadrg", 0, &id).ok());
    ASSERT_TRUE(catalog_->Scan(id, &n).ok());
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  std::string td_;
  std::shared_ptr<fv::Catalog> catalog_;
};

TEST_F(EngineReal, RendersAtlantaLfc) {
  fv::MapEngine engine(catalog_);
  ASSERT_TRUE(engine.SetSurfaceDimensions(400, 300).ok());
  ASSERT_TRUE(engine.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(engine.SetScale(500000.0).ok());

  // restrict to LFC so the render is deterministic across series
  int64_t lfc = 0;
  std::vector<fv::SeriesRow> series;
  ASSERT_TRUE(catalog_->Series(&series).ok());
  for (const auto& s : series)
    if (s.series_key == "LFC") lfc = s.id;
  ASSERT_NE(lfc, 0);

  fv::CpuCanvas canvas(400, 300);
  canvas.Clear(fv::FvColor{24, 24, 24, 255});
  int drawn = 0;
  fv::Status s = engine.RenderBaseMap(canvas, lfc, {}, &drawn);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_GE(drawn, 1);

  // content sanity: mostly covered, many colors
  long inked = 0;
  for (int y = 0; y < 300; ++y) {
    const unsigned char* row = canvas.Buffer().Row(y);
    for (int x = 0; x < 400; ++x)
      if (row[4 * x] != 24 || row[4 * x + 1] != 24) ++inked;
  }
  EXPECT_GT(inked, 300 * 400 * 0.9) << "viewport should be nearly covered";

  uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashAtlanta == 0) {
    printf("PROBE atlanta hash: 0x%llxull\n", (unsigned long long)h);
    fv::WritePng(canvas.Buffer(), "fvrender_atlanta.png");
  } else {
    EXPECT_EQ(h, kHashAtlanta);
  }
}

TEST_F(EngineReal, InterruptStopsRender) {
  fv::MapEngine engine(catalog_);
  ASSERT_TRUE(engine.SetSurfaceDimensions(400, 300).ok());
  ASSERT_TRUE(engine.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(engine.SetScale(500000.0).ok());
  fv::CpuCanvas canvas(400, 300);
  fv::Status s = engine.RenderBaseMap(canvas, 0, [] { return true; }, nullptr);
  EXPECT_EQ(s.code, fv::kInterrupted);
}

TEST_F(EngineReal, ElevationThroughEngine) {
  if (!fs::exists(td_ + "/dted")) GTEST_SKIP();
  fv::MapEngine engine(catalog_);
  float elev = 0;
  EXPECT_EQ(engine.GetElevation({31.5, -81.5}, &elev).code, fv::kNotFound);
  engine.SetElevationSource(
      std::make_shared<fv::DtedElevationSource>(td_ + "/dted"));
  ASSERT_TRUE(engine.GetElevation({31.5, -81.5}, &elev).ok());
  EXPECT_FLOAT_EQ(elev, 9.0f);  // pinned since the DTED adapter session
}

TEST_F(EngineReal, UnconfiguredEngineRejectsRender) {
  fv::MapEngine engine(catalog_);
  fv::CpuCanvas canvas(64, 64);
  EXPECT_EQ(engine.RenderBaseMap(canvas).code, fv::kInvalidArg);
}

}  // namespace
