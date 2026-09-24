// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * The equal-arc base map at world scale: frames as wide as a hemisphere,
 * views centred away from 0 degrees, and views wider than 360 degrees.
 * The fixture is two synthetic frames, west (-180..0) and east (0..180),
 * whose GeoToPixel does not unwrap longitude, as TIROS's does not.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"
#include "fvkit/proj.h"

namespace {

constexpr int kHemiW = 180, kHemiH = 90;  // one degree per source pixel
constexpr unsigned char kBg = 24;

bool IsWest(const std::string& path) {
  return path.find("west") != std::string::npos;
}

fv::GeoRect HemiBounds(bool west) {
  return west ? fv::GeoRect{{-45.0, -180.0}, {45.0, 0.0}}
              : fv::GeoRect{{-45.0, 0.0}, {45.0, 180.0}};
}

/// Red is the source column, green names the frame (1 west, 2 east), so a
/// sample says which frame and which column the engine read.
class HemiRaster : public fv::IRasterSource {
 public:
  fv::Status Open(const std::string& path) override {
    west_ = IsWest(path);
    return fv::Status::Ok();
  }
  fv::GeoRect Bounds() const override { return HemiBounds(west_); }
  fv::Status Info(fv::ImageInfo* i) const override {
    i->size = {kHemiW, kHemiH};
    i->bounds = Bounds();
    return fv::Status::Ok();
  }
  fv::Status ReadBlock(const fv::PixelRect& r, fv::PixelBuffer* out) override {
    *out = fv::PixelBuffer(r.width, r.height);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        unsigned char* p = out->Row(y) + 4 * x;
        p[0] = (unsigned char)(r.x + x);
        p[1] = west_ ? 1 : 2;
        p[2] = 0;
        p[3] = 255;
      }
    return fv::Status::Ok();
  }
  fv::Status PixelToGeo(double px, double py, fv::GeoPoint* p) const override {
    p->lon = Bounds().ll.lon + px;
    p->lat = 45.0 - py;
    return fv::Status::Ok();
  }
  fv::Status GeoToPixel(const fv::GeoPoint& p, double* px,
                        double* py) const override {
    *px = p.lon - Bounds().ll.lon;
    *py = 45.0 - p.lat;
    return fv::Status::Ok();
  }

 private:
  bool west_ = true;
};

class HemiEnum : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    dir_ = dir;
    next_ = 0;
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* f) override {
    if (next_ >= 2) return false;
    const bool west = next_++ == 0;
    f->path = dir_ + (west ? "/west.frame" : "/east.frame");
    f->bounds = HemiBounds(west);
    f->series_key = "HEMI";
    f->scale = 100000000.0;
    f->scale_units = 0;
    f->size_bytes = 1;
    return true;
  }

 private:
  std::string dir_;
  int next_ = 2;
};

class EngineWorld : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    fv::FormatFactories f;
    f.format_key = "hemi";
    f.make_enumerator = [] { return std::make_shared<HemiEnum>(); };
    f.make_raster_source = [] { return std::make_shared<HemiRaster>(); };
    ASSERT_TRUE(fv::RegisterFormat(f).ok());
    cat_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(cat_->Open(":memory:").ok());
    int64_t id = 0;
    int n = 0;
    ASSERT_TRUE(cat_->AddDataSource("/hemi", "hemi", 0, &id).ok());
    ASSERT_TRUE(cat_->Scan(id, &n).ok());
    ASSERT_EQ(n, 2);
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  /// Renders both frames and keeps the engine's projection for sampling.
  void Render(int w, int h, fv::GeoPoint centre, double dpp, double rot = 0,
              fv::ProjectionType type = fv::ProjectionType::kEqualArc) {
    fv::MapEngine engine(cat_);
    ASSERT_TRUE(engine.SetSurfaceDimensions(w, h).ok());
    ASSERT_TRUE(engine.SetCenter(centre).ok());
    ASSERT_TRUE(engine.SetResolution(dpp, dpp).ok());
    ASSERT_TRUE(engine.SetRotation(rot).ok());
    ASSERT_TRUE(engine.SetProjectionType(type).ok());
    fv::CpuCanvas canvas(w, h);
    canvas.Clear(fv::FvColor{kBg, kBg, kBg, 255});
    int drawn = 0;
    fv::Status s = engine.RenderBaseMap(canvas, 0, {}, &drawn);
    ASSERT_TRUE(s.ok()) << s.message;
    EXPECT_EQ(drawn, 2);
    buf_ = canvas.Buffer();
    proj_ = engine.CurrentProj();
  }

  /// The pixel at an unwrapped longitude (lat 0); lon may lie beyond 180 of
  /// the centre.
  const unsigned char* At(double lon) const {
    double sx = 0, sy = 0;
    EXPECT_TRUE(proj_.GeoToSurfaceUnwrapped({0.0, lon}, &sx, &sy).ok());
    const int x = (int)std::lround(sx), y = (int)std::lround(sy);
    EXPECT_GE(x, 0);
    EXPECT_LT(x, buf_.Width());
    EXPECT_GE(y, 0);
    EXPECT_LT(y, buf_.Height());
    return buf_.Row(y) + 4 * x;
  }

  /// Expects the frame and source column that own `lon`.
  void ExpectSource(double lon) const {
    const double l = fv::NormalizeLon(lon);
    const bool west = l <= 0.0;
    const unsigned char* p = At(lon);
    EXPECT_EQ(p[1], west ? 1 : 2) << "lon " << lon;
    EXPECT_NEAR(p[0], l - (west ? -180.0 : 0.0), 1.0) << "lon " << lon;
  }

  std::shared_ptr<fv::Catalog> cat_;
  fv::PixelBuffer buf_;
  fv::MapProjection proj_;
};

// Centred on 90E the view runs -85..265: the west frame shows at its own
// place (-85..0) and again past the antimeridian (180..265).
TEST_F(EngineWorld, AWideFrameIsDrawnWhereverTheViewShowsIt) {
  Render(700, 60, {0.0, 90.0}, 0.5);
  ExpectSource(-60.3);
  ExpectSource(-0.7);
  ExpectSource(45.2);
  ExpectSource(179.3);
  ExpectSource(200.4);  // -159.6, the west frame beyond the antimeridian
  ExpectSource(250.6);
}

// A centre west of 0 mirrors it: the east frame beyond the antimeridian.
TEST_F(EngineWorld, TheSameHoldsWestOfTheAntimeridian) {
  Render(700, 60, {0.0, -170.0}, 0.5);
  ExpectSource(-300.2);  // 59.8
  ExpectSource(-190.6);  // 169.4
  ExpectSource(-175.3);
  ExpectSource(-10.4);
}

// 500 degrees of view: every longitude inside the overlap shows twice.
TEST_F(EngineWorld, AViewWiderThanTheWorldRepeatsIt) {
  Render(500, 60, {0.0, 30.0}, 1.0);
  for (double lon : {-200.4, -150.6, 30.3, 159.6, 210.4}) {
    ExpectSource(lon);
    if (lon + 360.0 < 30.0 + 249.0) {
      const unsigned char* a = At(lon);
      const unsigned char* b = At(lon + 360.0);
      EXPECT_EQ(a[0], b[0]) << lon;
      EXPECT_EQ(a[1], b[1]) << lon;
    }
  }
}

// The turned path takes the same copies and the same source longitudes.
TEST_F(EngineWorld, TheTurnedPathWrapsToo) {
  Render(700, 700, {0.0, 90.0}, 0.5, 180.0);
  ExpectSource(-60.3);
  ExpectSource(45.2);
  ExpectSource(200.4);
}

// The plan's section 6 world-scale requirement for the projected path: a view
// wider than half the world in Mercator reads the same frames at the same
// source columns as equal arc does. The wrap lives in the engine's row
// compositor, so the projected branch has to carry it too.
TEST_F(EngineWorld, TheProjectedPathWrapsToo) {
  Render(700, 60, {0.0, 90.0}, 0.5, 0.0, fv::ProjectionType::kMercator);
  EXPECT_FALSE(proj_.IsAffine());
  ExpectSource(-60.3);
  ExpectSource(-0.7);
  ExpectSource(45.2);
  ExpectSource(179.3);
  ExpectSource(200.4);
  ExpectSource(250.6);
}

// A world Lambert centred on the equator is the near-equator fallback: the
// cone constant would be zero there, so the Mercator equations draw it, which
// is what Windows does at WORLD_OVERVIEW. It still wraps.
TEST_F(EngineWorld, AWorldLambertFallsBackToMercatorAndWrapsToo) {
  Render(700, 60, {0.0, 90.0}, 0.5, 0.0, fv::ProjectionType::kLambert);
  EXPECT_FALSE(proj_.IsAffine());
  ExpectSource(-60.3);
  ExpectSource(45.2);
  ExpectSource(200.4);
  ExpectSource(250.6);
  // No convergence anywhere: the fallback cone is flat.
  fv::MapProjection::LocalScale s;
  ASSERT_TRUE(proj_.LocalScaleAt({0.0, 150.0}, &s).ok());
  EXPECT_DOUBLE_EQ(s.convergence_deg, 0.0);
}

// The azimuthal pair at world scale: the earth appears ONCE, so there is
// nothing to wrap. What the §6 check asks of them instead is that the whole
// sphere is drawn without a seam or a repeat, and that the background stays
// where the earth is not.
TEST_F(EngineWorld, AWorldAzimuthalEquidistantDrawsTheSphereOnce) {
  Render(900, 900, {20.0, 0.0}, 0.45, 0.0,
         fv::ProjectionType::kAzimuthalEquidistant);
  EXPECT_FALSE(proj_.IsAffine());
  // Every meridian, on the equator, from the frame that owns it.
  for (double lon = -170.0; lon <= 170.0; lon += 20.0) ExpectSource(lon);
  // The rim is the antipode of the centre, so the corners are past the earth.
  fv::GeoPoint g;
  EXPECT_EQ(proj_.SurfaceToGeo(0, 0, &g).code, fv::kNotProjectable);
  const unsigned char* corner = buf_.Row(0);
  EXPECT_EQ(corner[0], kBg);
  EXPECT_EQ(corner[1], kBg);
  // Both poles are on the surface and the bounds say so.
  bool north = false, south = false;
  ASSERT_TRUE(proj_.PoleOnSurface(&north, &south).ok());
  EXPECT_TRUE(north);
  EXPECT_TRUE(south);
  const fv::GeoRect r = proj_.VmapBounds();
  EXPECT_DOUBLE_EQ(r.ll.lon, -180.0);
  EXPECT_DOUBLE_EQ(r.ur.lon, 180.0);
}

// Orthographic shows one hemisphere as a disc. The near side is drawn, the far
// side is absent rather than smeared, and the corners are background.
TEST_F(EngineWorld, AWorldOrthographicDrawsOneHemisphere) {
  Render(800, 800, {0.0, 0.0}, 0.16, 0.0, fv::ProjectionType::kOrthographic);
  EXPECT_FALSE(proj_.IsAffine());
  // Off the frame join at lon 0, where the two frames touch and either
  // answer is right.
  for (double lon = -80.5; lon <= 80.0; lon += 20.0) ExpectSource(lon);
  fv::GeoPoint g;
  EXPECT_EQ(proj_.SurfaceToGeo(0, 0, &g).code, fv::kNotProjectable);
  EXPECT_EQ(buf_.Row(0)[1], kBg);
  // The far hemisphere has no image at all.
  double sx = 0, sy = 0;
  EXPECT_EQ(proj_.GeoToSurface({0.0, 120.0}, &sx, &sy).code,
            fv::kNotProjectable);
}

TEST(MapProjectionWorld, UnwrappedTransformAgreesInsideHalfAWorld) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(400, 200).ok());
  ASSERT_TRUE(p.SetCenter({10.0, 170.0}).ok());
  ASSERT_TRUE(p.SetResolution(0.5, 0.5).ok());
  double ax, ay, bx, by;
  ASSERT_TRUE(p.GeoToSurface({12.0, 175.0}, &ax, &ay).ok());
  ASSERT_TRUE(p.GeoToSurfaceUnwrapped({12.0, 175.0}, &bx, &by).ok());
  EXPECT_EQ(ax, bx);
  EXPECT_EQ(ay, by);
  // 360 degrees further east lands 720 px further right, not back on 175.
  ASSERT_TRUE(p.GeoToSurfaceUnwrapped({12.0, 535.0}, &bx, &by).ok());
  EXPECT_DOUBLE_EQ(bx - ax, 720.0);
}

TEST(MapProjectionWorld, LonRangeKeepsWhatVmapBoundsLoses) {
  fv::MapProjection p;
  ASSERT_TRUE(p.SetSurfaceSize(500, 100).ok());
  ASSERT_TRUE(p.SetCenter({0.0, 30.0}).ok());
  ASSERT_TRUE(p.SetResolution(1.0, 1.0).ok());
  const fv::GeoRect b = p.VmapBounds();
  EXPECT_EQ(b.ll.lon, -180.0);
  EXPECT_EQ(b.ur.lon, 180.0);
  double west = 0, east = 0;
  ASSERT_TRUE(p.VmapLonRange(&west, &east).ok());
  EXPECT_DOUBLE_EQ(west, -220.0);
  EXPECT_DOUBLE_EQ(east, 280.0);
}

}  // namespace
