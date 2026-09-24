// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/** @file
 * AdaptiveWarp against the per-pixel ExactWarp, and MapEngine's non-affine
 * raster path against a brute-force render of the same frame. The engine
 * tests force the path under equal-arc and make it non-affine through the
 * source: its GeoToPixel is a sine in latitude.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"
#include "fvkit/proj.h"
#include "fvkit/raster_warp.h"

namespace {

using fv::WarpIndex;
using fv::kWarpUnmapped;

/// Largest per-axis index difference between two warps over pixels both
/// mapped; `coverage_diffs` counts pixels mapped in one and not the other.
int MaxIndexDiff(const WarpIndex& a, const WarpIndex& b, int* coverage_diffs) {
  int worst = 0;
  *coverage_diffs = 0;
  for (size_t k = 0; k < a.sx.size(); ++k) {
    const bool am = a.sx[k] != kWarpUnmapped, bm = b.sx[k] != kWarpUnmapped;
    if (am != bm) {
      ++*coverage_diffs;
      continue;
    }
    if (!am) continue;
    worst = std::max(worst, std::abs(a.sx[k] - b.sx[k]));
    worst = std::max(worst, std::abs(a.sy[k] - b.sy[k]));
  }
  return worst;
}

TEST(AdaptiveWarp, AffineMapIsFilledFromThirteenSamples) {
  int calls = 0;
  fv::WarpMap map = [&](int x, int y, double* sx, double* sy) {
    ++calls;
    *sx = 3.0 + 0.75 * x - 0.25 * y;
    *sy = -2.0 + 0.5 * x + 1.25 * y;
    return true;
  };
  WarpIndex adaptive, exact;
  fv::AdaptiveWarp(200, 120, map, &adaptive);
  // Four corners, the centre, four quarter points and four edge midpoints.
  EXPECT_EQ(calls, 13);
  fv::ExactWarp(200, 120, map, &exact);
  int cov = 0;
  EXPECT_LE(MaxIndexDiff(adaptive, exact, &cov), 1);
  EXPECT_EQ(cov, 0);
}

// Windows rounds with DOUBLE2INT under _RC_NEAR: ties go to even. Both the
// interpolated fill (16 wide) and the per-pixel fill (7 wide) must agree.
TEST(AdaptiveWarp, TiesRoundToEven) {
  fv::WarpMap map = [](int x, int y, double* sx, double* sy) {
    *sx = x - 1.5;  // -1.5, -0.5, 0.5, 1.5, 2.5, ...
    *sy = y + 0.5;
    return true;
  };
  for (int w : {16, 7}) {
    WarpIndex idx;
    fv::AdaptiveWarp(w, 8, map, &idx);
    EXPECT_EQ(idx.sx[0], -2) << w;  // -1.5 away from zero, to even
    EXPECT_EQ(idx.sx[1], 0) << w;   // -0.5 toward zero
    EXPECT_EQ(idx.sx[2], 0) << w;   // 0.5 down
    EXPECT_EQ(idx.sx[3], 2) << w;   // 1.5 up
    EXPECT_EQ(idx.sx[4], 2) << w;   // 2.5 down
    EXPECT_EQ(idx.sy[0], 0) << w;
    EXPECT_EQ(idx.sy[(size_t)1 * w], 2) << w;
  }
}

TEST(AdaptiveWarp, NonAffineMapStaysWithinOnePixelOfExact) {
  int calls = 0;
  fv::WarpMap map = [&](int x, int y, double* sx, double* sy) {
    ++calls;
    *sx = x + 0.002 * x * y + 6.0 * std::sin(y / 25.0);
    *sy = 0.8 * y + 0.0015 * x * x;
    return true;
  };
  constexpr int kW = 320, kH = 240;
  WarpIndex adaptive, exact;
  fv::AdaptiveWarp(kW, kH, map, &adaptive);
  const int adaptive_calls = calls;
  fv::ExactWarp(kW, kH, map, &exact);
  int cov = 0;
  EXPECT_LE(MaxIndexDiff(adaptive, exact, &cov), 1);
  EXPECT_EQ(cov, 0);
  // It subdivided, and still sampled far fewer pixels than it filled.
  EXPECT_GT(adaptive_calls, 5);
  EXPECT_LT(adaptive_calls, kW * kH / 4);
}

// A disc of mappable pixels, as the visible hemisphere of an orthographic
// globe is. The disc is convex, so an interpolated rectangle (all corners
// inside) is wholly inside, and coverage must match exactly.
TEST(AdaptiveWarp, UnmappableRegionMatchesExactCoverage) {
  fv::WarpMap map = [](int x, int y, double* sx, double* sy) {
    const double dx = x - 100.0, dy = y - 90.0;
    if (dx * dx + dy * dy > 80.0 * 80.0) return false;
    *sx = 2.0 * x;
    *sy = 2.0 * y;
    return true;
  };
  WarpIndex adaptive, exact;
  fv::AdaptiveWarp(210, 180, map, &adaptive);
  fv::ExactWarp(210, 180, map, &exact);
  int cov = 0;
  EXPECT_LE(MaxIndexDiff(adaptive, exact, &cov), 1);
  EXPECT_EQ(cov, 0);
  EXPECT_EQ(adaptive.sx[0], kWarpUnmapped);
  EXPECT_NE(adaptive.sx[(size_t)90 * 210 + 100], kWarpUnmapped);
}

// Windows checks linearity at the centre only. A distortion odd about the
// centre (sin across its inflection, as Mercator's latitude is about the
// equator) interpolates exactly there, and Windows would fill the whole
// rectangle from its corners: 75 at row 50 where the exact index is 71. The
// quarter points catch it.
TEST(AdaptiveWarp, OddDistortionIsSubdivided) {
  int calls = 0;
  fv::WarpMap map = [&](int x, int y, double* sx, double* sy) {
    ++calls;
    *sx = x;
    *sy = 100.0 + 60.0 * std::sin((y - 100) / 100.0);
    return true;
  };
  WarpIndex adaptive, exact;
  fv::AdaptiveWarp(64, 201, map, &adaptive);
  EXPECT_GT(calls, 9);
  fv::ExactWarp(64, 201, map, &exact);
  int cov = 0;
  EXPECT_LE(MaxIndexDiff(adaptive, exact, &cov), 1);
  EXPECT_EQ(cov, 0);
  EXPECT_EQ(exact.sy[(size_t)50 * 64], 71);
  EXPECT_LE(std::abs(adaptive.sy[(size_t)50 * 64] - 71), 1);
}

TEST(AdaptiveWarp, EmptyTargetIsEmpty) {
  WarpIndex idx;
  fv::AdaptiveWarp(0, 10, [](int, int, double*, double*) { return true; },
                   &idx);
  EXPECT_TRUE(idx.sx.empty());
}

// ---------------------------------------------------------------------------
// The engine path.

constexpr int kSrcW = 240, kSrcH = 240;
constexpr unsigned char kBg = 24;
constexpr double kSouth = -60.0, kNorth = 60.0, kWest = -60.0, kEast = 60.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// One frame, lat and lon -60..60. Red is the source column, blue
/// the row, so a composited pixel says which source pixel it came from. Rows
/// are evenly spaced in sin(lat) when `curved`, which makes the source
/// non-affine; otherwise they are evenly spaced in latitude. The frame is
/// symmetric about the equator, the case a centre-only linearity test
/// misses (see OddDistortionIsSubdivided).
class WarpRaster : public fv::IRasterSource {
 public:
  explicit WarpRaster(bool curved) : curved_(curved) {}
  fv::Status Open(const std::string&) override { return fv::Status::Ok(); }
  fv::GeoRect Bounds() const override { return {{kSouth, kWest}, {kNorth, kEast}}; }
  fv::Status Info(fv::ImageInfo* i) const override {
    i->size = {kSrcW, kSrcH};
    i->bounds = Bounds();
    return fv::Status::Ok();
  }
  fv::Status ReadBlock(const fv::PixelRect& r, fv::PixelBuffer* out) override {
    *out = fv::PixelBuffer(r.width, r.height);
    for (int y = 0; y < r.height; ++y)
      for (int x = 0; x < r.width; ++x) {
        unsigned char* p = out->Row(y) + 4 * x;
        p[0] = (unsigned char)(r.x + x);
        p[1] = 7;
        p[2] = (unsigned char)(r.y + y);
        p[3] = 255;
      }
    return fv::Status::Ok();
  }
  fv::Status PixelToGeo(double px, double py, fv::GeoPoint* p) const override {
    p->lon = kWest + px * ((kEast - kWest) / kSrcW);
    const double t = py / kSrcH;  // 0 top, 1 bottom
    p->lat = curved_ ? std::asin(Sin(kNorth) - t * (Sin(kNorth) - Sin(kSouth))) /
                           kDegToRad
                     : kNorth - t * (kNorth - kSouth);
    return fv::Status::Ok();
  }
  fv::Status GeoToPixel(const fv::GeoPoint& p, double* px,
                        double* py) const override {
    *px = (p.lon - kWest) * (kSrcW / (kEast - kWest));
    const double t = curved_ ? (Sin(kNorth) - Sin(p.lat)) /
                                   (Sin(kNorth) - Sin(kSouth))
                             : (kNorth - p.lat) / (kNorth - kSouth);
    *py = t * kSrcH;
    return fv::Status::Ok();
  }

 private:
  static double Sin(double deg) { return std::sin(deg * kDegToRad); }
  bool curved_;
};

class WarpEnum : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string& dir) override {
    dir_ = dir;
    done_ = false;
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo* f) override {
    if (done_) return false;
    done_ = true;
    f->path = dir_ + "/warp.frame";
    f->bounds = {{kSouth, kWest}, {kNorth, kEast}};
    f->series_key = "WARP";
    f->scale = 50000000.0;
    f->scale_units = 0;
    f->size_bytes = 1;
    return true;
  }

 private:
  std::string dir_;
  bool done_ = true;
};

class EngineProjected : public ::testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    const bool curved = GetParam();
    fv::FormatFactories f;
    f.format_key = "warp";
    f.make_enumerator = [] { return std::make_shared<WarpEnum>(); };
    f.make_raster_source = [curved] {
      return std::make_shared<WarpRaster>(curved);
    };
    ASSERT_TRUE(fv::RegisterFormat(f).ok());
    cat_ = std::make_shared<fv::Catalog>();
    ASSERT_TRUE(cat_->Open(":memory:").ok());
    int64_t id = 0;
    int n = 0;
    ASSERT_TRUE(cat_->AddDataSource("/warp", "warp", 0, &id).ok());
    ASSERT_TRUE(cat_->Scan(id, &n).ok());
    ASSERT_EQ(n, 1);
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  fv::PixelBuffer Render(bool forced, double rot, fv::MapProjection* proj) {
    fv::MapEngine engine(cat_);
    EXPECT_TRUE(engine.SetSurfaceDimensions(kW, kH).ok());
    EXPECT_TRUE(engine.SetCenter({4.0, -3.0}).ok());
    EXPECT_TRUE(engine.SetResolution(0.45, 0.5).ok());
    EXPECT_TRUE(engine.SetRotation(rot).ok());
    engine.ForceProjectedPathForTest(forced);
    fv::CpuCanvas canvas(kW, kH);
    canvas.Clear(fv::FvColor{kBg, kBg, kBg, 255});
    fv::Status s = engine.RenderBaseMap(canvas);
    EXPECT_TRUE(s.ok()) << s.message;
    if (proj != nullptr) *proj = engine.CurrentProj();
    return canvas.Buffer();
  }

  /// The same view drawn in a non-affine projection. Nothing is forced: the
  /// engine takes the projected path because the projection is not affine.
  fv::PixelBuffer RenderIn(fv::ProjectionType type, double rot,
                           fv::MapProjection* proj, double center_lat = 4.0,
                           double dpp = 0.0) {
    fv::MapEngine engine(cat_);
    EXPECT_TRUE(engine.SetSurfaceDimensions(kW, kH).ok());
    EXPECT_TRUE(engine.SetCenter({center_lat, -3.0}).ok());
    if (dpp > 0)
      EXPECT_TRUE(engine.SetResolution(dpp, dpp).ok());
    else
      EXPECT_TRUE(engine.SetResolution(0.45, 0.5).ok());
    EXPECT_TRUE(engine.SetRotation(rot).ok());
    EXPECT_TRUE(engine.SetProjectionType(type).ok());
    EXPECT_FALSE(engine.CurrentProj().IsAffine());
    fv::CpuCanvas canvas(kW, kH);
    canvas.Clear(fv::FvColor{kBg, kBg, kBg, 255});
    fv::Status s = engine.RenderBaseMap(canvas);
    EXPECT_TRUE(s.ok()) << s.message;
    if (proj != nullptr) *proj = engine.CurrentProj();
    return canvas.Buffer();
  }

  static constexpr int kW = 320, kH = 300;
  std::shared_ptr<fv::Catalog> cat_;
};

// Every pixel of the forced path against SurfaceToGeo -> GeoToPixel ->
// round, done per pixel: within one source pixel where both draw the frame,
// and disagreeing on coverage only along the frame's own edge.
TEST_P(EngineProjected, MatchesBruteForceRender) {
  const WarpRaster src(GetParam());
  for (double rot : {0.0, 30.0}) {
    fv::MapProjection proj;
    const fv::PixelBuffer got = Render(true, rot, &proj);
    int frame_px = 0, edge_cov = 0;
    for (int y = 0; y < kH; ++y)
      for (int x = 0; x < kW; ++x) {
        const unsigned char* p = got.Row(y) + 4 * x;
        const bool drawn = p[1] == 7;
        fv::GeoPoint g;
        ASSERT_TRUE(proj.SurfaceToGeo(x, y, &g).ok());
        double fx = 0, fy = 0;
        ASSERT_TRUE(src.GeoToPixel(g, &fx, &fy).ok());
        const int ex = (int)std::nearbyint(fx), ey = (int)std::nearbyint(fy);
        const bool inside = ex >= 0 && ey >= 0 && ex < kSrcW && ey < kSrcH;
        if (drawn != inside) {
          // Only a pixel whose exact index is next to the image edge.
          EXPECT_TRUE(ex >= -1 && ey >= -1 && ex <= kSrcW && ey <= kSrcH)
              << "rot " << rot << " at " << x << "," << y;
          ++edge_cov;
          continue;
        }
        if (!drawn) {
          EXPECT_EQ(p[0], kBg) << "rot " << rot << " at " << x << "," << y;
          continue;
        }
        ++frame_px;
        EXPECT_LE(std::abs(p[0] - ex), 1) << "rot " << rot << " at " << x << "," << y;
        EXPECT_LE(std::abs(p[2] - ey), 1) << "rot " << rot << " at " << x << "," << y;
      }
    EXPECT_GT(frame_px, kW * kH / 2) << rot;
    EXPECT_LT(edge_cov, 2 * (kW + kH)) << rot;
  }
}

// Under an affine source the forced path and the affine paths fit the same
// transform. Inside the frame they disagree only where a coordinate sits on
// a rounding tie, which lround and nearbyint break differently. At the edge
// the straight path keeps pixels whose centres are inside the frame and the
// forced path keeps pixels whose rounded index is, so coverage may differ by
// one pixel along the frame's outline.
TEST_P(EngineProjected, AgreesWithAffinePathsOnAffineSource) {
  if (GetParam()) GTEST_SKIP() << "affine source only";
  for (double rot : {0.0, 30.0}) {
    const fv::PixelBuffer forced = Render(true, rot, nullptr);
    const fv::PixelBuffer affine = Render(false, rot, nullptr);
    int both = 0, coverage_diffs = 0;
    for (int y = 0; y < kH; ++y)
      for (int x = 0; x < kW; ++x) {
        const unsigned char* a = forced.Row(y) + 4 * x;
        const unsigned char* b = affine.Row(y) + 4 * x;
        if ((a[1] == 7) != (b[1] == 7)) {
          ++coverage_diffs;
        } else if (a[1] == 7) {
          ++both;
          if (std::abs(a[0] - b[0]) > 1 || std::abs(a[2] - b[2]) > 1)
            ADD_FAILURE() << "rot " << rot << " at " << x << "," << y;
        }
      }
    EXPECT_GT(both, kW * kH / 2) << rot;
    EXPECT_LT(coverage_diffs, 2 * (kW + kH)) << rot;
  }
}

// PJ2's raster acceptance: a real non-affine display projection over the
// adaptive warp, against the per-pixel exact render. Same error budget as the
// forced-path test above, and the same edge-coverage rule.
TEST_P(EngineProjected, MercatorMatchesBruteForceRender) {
  const WarpRaster src(GetParam());
  for (double rot : {0.0, 30.0}) {
    fv::MapProjection proj;
    const fv::PixelBuffer got = RenderIn(fv::ProjectionType::kMercator, rot,
                                         &proj);
    int frame_px = 0, edge_cov = 0;
    for (int y = 0; y < kH; ++y)
      for (int x = 0; x < kW; ++x) {
        const unsigned char* p = got.Row(y) + 4 * x;
        const bool drawn = p[1] == 7;
        fv::GeoPoint g;
        ASSERT_TRUE(proj.SurfaceToGeo(x, y, &g).ok());
        double fx = 0, fy = 0;
        ASSERT_TRUE(src.GeoToPixel(g, &fx, &fy).ok());
        const int ex = (int)std::nearbyint(fx), ey = (int)std::nearbyint(fy);
        const bool inside = ex >= 0 && ey >= 0 && ex < kSrcW && ey < kSrcH;
        if (drawn != inside) {
          EXPECT_TRUE(ex >= -1 && ey >= -1 && ex <= kSrcW && ey <= kSrcH)
              << "rot " << rot << " at " << x << "," << y;
          ++edge_cov;
          continue;
        }
        if (!drawn) continue;
        ++frame_px;
        EXPECT_LE(std::abs(p[0] - ex), 1) << "rot " << rot << " at " << x << "," << y;
        EXPECT_LE(std::abs(p[2] - ey), 1) << "rot " << rot << " at " << x << "," << y;
      }
    EXPECT_GT(frame_px, kW * kH / 4) << rot;
    EXPECT_LT(edge_cov, 2 * (kW + kH)) << rot;
  }
}

// Mercator stretches north-south away from the standard parallel, so the same
// view holds fewer degrees of latitude than equal arc does: the frame's top
// and bottom edges move outwards on the surface.
TEST_P(EngineProjected, MercatorStretchesAwayFromTheStandardParallel) {
  fv::MapProjection merc, equal;
  RenderIn(fv::ProjectionType::kMercator, 0.0, &merc);
  Render(false, 0.0, &equal);
  fv::GeoPoint m_top, e_top;
  ASSERT_TRUE(merc.SurfaceToGeo(kW / 2.0, 0, &m_top).ok());
  ASSERT_TRUE(equal.SurfaceToGeo(kW / 2.0, 0, &e_top).ok());
  EXPECT_LT(m_top.lat, e_top.lat);
  EXPECT_NEAR(m_top.lon, e_top.lon, 1.0);
}

// PJ3's raster acceptance: the conic over the adaptive warp, against the
// per-pixel exact render. Same error budget and edge-coverage rule as PJ1 and
// PJ2. The view is centred away from the equator so the cone is a real cone
// and not the near-equator Mercator fallback, and its resolution is finer
// than the other tests' so 300 rows of conic stay short of the pole.
TEST_P(EngineProjected, LambertMatchesBruteForceRender) {
  const WarpRaster src(GetParam());
  for (double rot : {0.0, 30.0}) {
    fv::MapProjection proj;
    const fv::PixelBuffer got =
        RenderIn(fv::ProjectionType::kLambert, rot, &proj, 30.0, 0.15);
    int frame_px = 0, edge_cov = 0;
    for (int y = 0; y < kH; ++y)
      for (int x = 0; x < kW; ++x) {
        const unsigned char* p = got.Row(y) + 4 * x;
        const bool drawn = p[1] == 7;
        fv::GeoPoint g;
        ASSERT_TRUE(proj.SurfaceToGeo(x, y, &g).ok());
        double fx = 0, fy = 0;
        ASSERT_TRUE(src.GeoToPixel(g, &fx, &fy).ok());
        const int ex = (int)std::nearbyint(fx), ey = (int)std::nearbyint(fy);
        const bool inside = ex >= 0 && ey >= 0 && ex < kSrcW && ey < kSrcH;
        if (drawn != inside) {
          EXPECT_TRUE(ex >= -1 && ey >= -1 && ex <= kSrcW && ey <= kSrcH)
              << "rot " << rot << " at " << x << "," << y;
          ++edge_cov;
          continue;
        }
        if (!drawn) continue;
        ++frame_px;
        EXPECT_LE(std::abs(p[0] - ex), 1) << "rot " << rot << " at " << x << "," << y;
        EXPECT_LE(std::abs(p[2] - ey), 1) << "rot " << rot << " at " << x << "," << y;
      }
    EXPECT_GT(frame_px, kW * kH / 8) << rot;
    EXPECT_LT(edge_cov, 4 * (kW + kH)) << rot;
  }
}

// The conic bends a meridian on the surface: a column of constant longitude
// away from the centre meridian is not a surface column, which is what makes
// PJ1's subdivision, rather than the affine path, necessary here.
TEST_P(EngineProjected, LambertBendsTheMeridians) {
  fv::MapProjection lam;
  RenderIn(fv::ProjectionType::kLambert, 0.0, &lam, 30.0, 0.15);
  fv::GeoPoint top, bottom;
  ASSERT_TRUE(lam.SurfaceToGeo(20, 0, &top).ok());
  ASSERT_TRUE(lam.SurfaceToGeo(20, kH - 1, &bottom).ok());
  EXPECT_GT(std::fabs(top.lon - bottom.lon), 0.5);
  // The centre column stays a meridian.
  fv::GeoPoint c_top, c_bottom;
  ASSERT_TRUE(lam.SurfaceToGeo((kW - 1) / 2.0, 0, &c_top).ok());
  ASSERT_TRUE(lam.SurfaceToGeo((kW - 1) / 2.0, kH - 1, &c_bottom).ok());
  EXPECT_NEAR(c_top.lon, c_bottom.lon, 1e-9);
}

// PJ4's raster acceptance: the two azimuthal projections over the adaptive
// warp, against the per-pixel exact render. They are the first projections
// where part of the surface is off the earth, so the rule there is checked
// too: an unprojectable pixel must be left at the background.
TEST_P(EngineProjected, AzimuthalMatchesBruteForceRender) {
  const WarpRaster src(GetParam());
  for (auto type : {fv::ProjectionType::kAzimuthalEquidistant,
                    fv::ProjectionType::kOrthographic}) {
    for (double rot : {0.0, 30.0}) {
      fv::MapProjection proj;
      const fv::PixelBuffer got = RenderIn(type, rot, &proj);
      int frame_px = 0, edge_cov = 0, off_earth = 0;
      for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
          const unsigned char* p = got.Row(y) + 4 * x;
          const bool drawn = p[1] == 7;
          fv::GeoPoint g;
          if (!proj.SurfaceToGeo(x, y, &g).ok()) {
            ++off_earth;
            EXPECT_FALSE(drawn) << "off the earth at " << x << "," << y;
            continue;
          }
          double fx = 0, fy = 0;
          ASSERT_TRUE(src.GeoToPixel(g, &fx, &fy).ok());
          const int ex = (int)std::nearbyint(fx), ey = (int)std::nearbyint(fy);
          const bool inside = ex >= 0 && ey >= 0 && ex < kSrcW && ey < kSrcH;
          if (drawn != inside) {
            EXPECT_TRUE(ex >= -1 && ey >= -1 && ex <= kSrcW && ey <= kSrcH)
                << "rot " << rot << " at " << x << "," << y;
            ++edge_cov;
            continue;
          }
          if (!drawn) continue;
          ++frame_px;
          EXPECT_LE(std::abs(p[0] - ex), 1)
              << "type " << static_cast<int>(type) << " rot " << rot << " at "
              << x << "," << y;
          EXPECT_LE(std::abs(p[2] - ey), 1)
              << "type " << static_cast<int>(type) << " rot " << rot << " at "
              << x << "," << y;
        }
      EXPECT_GT(frame_px, kW * kH / 16) << rot;
      EXPECT_LT(edge_cov, 4 * (kW + kH)) << rot;
      // Orthographic runs out of earth on this view; Azimuthal Equidistant,
      // whose disc is pi times wider, does not.
      if (type == fv::ProjectionType::kOrthographic)
        EXPECT_GT(off_earth, 0) << rot;
      else
        EXPECT_EQ(off_earth, 0) << rot;
    }
  }
}

// Both azimuthal projections turn a parallel into a curve, and Orthographic
// crowds the far edge of the disc: a degree of longitude at the rim is worth
// far fewer pixels than one at the centre.
TEST_P(EngineProjected, OrthoCrowdsTheLimb) {
  fv::MapProjection orth;
  RenderIn(fv::ProjectionType::kOrthographic, 0.0, &orth);
  double cx = 0, cy = 0, c1x = 0, c1y = 0;
  ASSERT_TRUE(orth.GeoToSurface({4.0, -3.0}, &cx, &cy).ok());
  ASSERT_TRUE(orth.GeoToSurface({4.0, -2.0}, &c1x, &c1y).ok());
  double ex = 0, ey = 0, e1x = 0, e1y = 0;
  ASSERT_TRUE(orth.GeoToSurface({4.0, 57.0}, &ex, &ey).ok());
  ASSERT_TRUE(orth.GeoToSurface({4.0, 58.0}, &e1x, &e1y).ok());
  EXPECT_LT(std::hypot(e1x - ex, e1y - ey),
            0.7 * std::hypot(c1x - cx, c1y - cy));
}

INSTANTIATE_TEST_SUITE_P(Source, EngineProjected, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& i) {
                           return i.param ? "Curved" : "Linear";
                         });

}  // namespace
