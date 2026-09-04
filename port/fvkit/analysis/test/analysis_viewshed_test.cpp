// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The XDraw viewshed (fvkit/analysis/viewshed.h, plan step AN3).
//
// The fixtures are surfaces whose line of sight can be worked out on paper.
// That is possible because the DUE-NORTH COLUMN of the lattice is a 1-D
// chain: post k is stepped straight up the meridian from the observer, its
// only parent is post k-1, and every distance in it is exact —
// StepRhumb(lat, lon, d, tcNorth) adds d to the latitude and leaves the
// longitude alone, and the haversine along a meridian is the latitude
// difference. So `d_k = k * step_deg * 60 * 1852` metres to the last digit,
// and the whole recurrence can be written out here and checked term by term.

#include "fvkit/analysis/viewshed.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "gtest/gtest.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::Status;
using fv::analysis::ComputeViewshed;
using fv::analysis::HeightMethod;
using fv::analysis::IViewshedProgress;
using fv::analysis::ViewshedRequest;
using fv::analysis::ViewshedResult;

// The algorithm's own constants, repeated here so the tests are checking the
// implementation rather than sharing its arithmetic.
constexpr double kR = 6378135.0;             // GeoPoint.equatorialRadiusMeters
constexpr double kMetersPerDegree = 60.0 * 1852.0;
constexpr double kObsLat = 34.0;
constexpr double kObsLon = -84.0;
constexpr double kStepDeg = 1.0 / 1200.0;    // DTED-1, 3 arcseconds
const double kStepM = kStepDeg * kMetersPerDegree;  // 92.6 m

double Drop(double d) { return d * d / (2.0 * kR); }

// Flat ground at sea level, everywhere.
class FlatElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint&, float* out) override {
    ++queries;
    *out = 0.0f;
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = kStepDeg;
    if (lon) *lon = kStepDeg;
    return true;
  }
  int queries = 0;
};

// Flat ground with a wall `height_m` high in ONE ring of latitude, `ring`
// steps north of the observer — so the north column is: flat, flat, ..., a
// wall, then flat again in its shadow.
class WallElevation : public fv::IElevationSource {
 public:
  WallElevation(int ring, double height_m) : ring_(ring), height_(height_m) {}
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    const double wall_lat = kObsLat + ring_ * kStepDeg;
    *out = std::fabs(p.lat - wall_lat) < kStepDeg / 4.0
               ? static_cast<float>(height_)
               : 0.0f;
    return Status::Ok();
  }

 private:
  int ring_;
  double height_;
};

// A wall as above, plus a VOID ring further out — a hole in the coverage
// sitting inside the wall's own shadow.
class WallWithHoleElevation : public fv::IElevationSource {
 public:
  WallWithHoleElevation(int wall_ring, double height_m, int hole_ring)
      : wall_(wall_ring), height_(height_m), hole_(hole_ring) {}
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    if (std::fabs(p.lat - (kObsLat + hole_ * kStepDeg)) < kStepDeg / 4.0) {
      *out = std::numeric_limits<float>::quiet_NaN();
      return Status::Ok();
    }
    *out = std::fabs(p.lat - (kObsLat + wall_ * kStepDeg)) < kStepDeg / 4.0
               ? static_cast<float>(height_)
               : 0.0f;
    return Status::Ok();
  }

 private:
  int wall_;
  double height_;
  int hole_;
};

// Terrain with relief in every direction, so the eight wall octants inherit
// genuinely DIFFERENT slopes from their two parents — which is the only
// situation in which the three height methods can disagree at all.
class RoughElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    const double x = (p.lon - kObsLon) * 3000.0;
    const double y = (p.lat - kObsLat) * 3000.0;
    *out = static_cast<float>(800.0 * std::sin(x * 1.7) * std::cos(y * 2.4) +
                              480.0 * std::sin((x + y) * 0.7) + 1000.0);
    return Status::Ok();
  }
};

// No coverage at all.
class EmptyElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint&, float*) override {
    return Status::Error(fv::kOutOfCoverage, "no data here");
  }
};

ViewshedRequest BaseRequest() {
  ViewshedRequest req;
  req.observer = GeoPoint{kObsLat, kObsLon};
  req.observer_height_m = 2.0;
  req.range_m = 10000.0;
  req.step_deg = kStepDeg;
  req.method = HeightMethod::kInterpolated;
  return req;
}

// The post `k` rings due north of the observer.
float North(const ViewshedResult& r, int k) {
  const int c = r.span / 2;
  return r.At(c - k, c);
}

class CancelAt : public IViewshedProgress {
 public:
  explicit CancelAt(int percent) : at_(percent) {}
  bool OnProgress(int percent) override {
    ++calls;
    last = percent;
    EXPECT_GT(percent, previous_) << "progress must only be reported when it MOVES";
    previous_ = percent;
    return percent < at_;
  }
  int calls = 0;
  int last = -1;

 private:
  int at_;
  int previous_ = -1;
};

// ---------------------------------------------------------------------------
// The request, and the two ways it can be refused
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, RejectsAnUnusableRequest) {
  FlatElevation src;
  ViewshedResult r;

  EXPECT_FALSE(ComputeViewshed(src, BaseRequest(), nullptr).ok());

  ViewshedRequest no_range = BaseRequest();
  no_range.range_m = 0.0;
  EXPECT_EQ(ComputeViewshed(src, no_range, &r).code, fv::kInvalidArg);

  ViewshedRequest no_step = BaseRequest();
  no_step.step_deg = 0.0;
  EXPECT_EQ(ComputeViewshed(src, no_step, &r).code, fv::kInvalidArg);
}

// FalconView asks this before it will even queue the work, as
// IsElevationDataAvailable: there is nothing to stand on.
TEST(AnalysisViewshed, AnObserverWithNoGroundIsRefused) {
  EmptyElevation src;
  ViewshedResult r;
  const Status s = ComputeViewshed(src, BaseRequest(), &r);
  EXPECT_EQ(s.code, fv::kOutOfCoverage);
  EXPECT_FALSE(r.Valid());
}

// ---------------------------------------------------------------------------
// The lattice
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, TheLatticeIsOddAndTheObserverIsAtItsCentre) {
  FlatElevation src;
  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, BaseRequest(), &r).ok());

  ASSERT_TRUE(r.Valid());
  EXPECT_EQ(r.span % 2, 1);
  // range / step, one ring per step, both sides plus the centre.
  const int steps = static_cast<int>(10000.0 / kStepM);
  EXPECT_EQ(r.span, steps * 2 + 1);
  EXPECT_DOUBLE_EQ(r.step_deg, kStepDeg);
  EXPECT_FALSE(r.step_was_widened);

  const int c = r.span / 2;
  EXPECT_FLOAT_EQ(r.At(c, c), 0.0f);  // you can see where you are standing
}

TEST(AnalysisViewshed, RowZeroIsNorthAndColumnZeroIsWest) {
  FlatElevation src;
  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, BaseRequest(), &r).ok());

  EXPECT_GT(r.bounds.ur.lat, r.bounds.ll.lat);
  EXPECT_GT(r.bounds.ur.lon, r.bounds.ll.lon);
  EXPECT_NEAR(r.bounds.ur.lat, kObsLat + (r.span / 2) * kStepDeg, 1e-9);
  EXPECT_NEAR(r.bounds.ll.lat, kObsLat - (r.span / 2) * kStepDeg, 1e-9);
  // The observer's own latitude is the middle of the two.
  EXPECT_NEAR((r.bounds.ur.lat + r.bounds.ll.lat) / 2.0, kObsLat, 1e-9);
  EXPECT_LT(r.bounds.ll.lon, kObsLon);
  EXPECT_GT(r.bounds.ur.lon, kObsLon);
}

// ---------------------------------------------------------------------------
// Flat ground: everything inside the horizon is visible, and the horizon is
// where the curvature says it is
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, FlatGroundInsideTheHorizonIsAllVisible) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 100.0;  // horizon at sqrt(2Rh) = 35.7 km
  req.range_m = 5000.0;           // corners at 7.1 km, well inside it
  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, req, &r).ok());

  EXPECT_EQ(r.no_data_count, 0u);
  for (float v : r.visible_height_m) EXPECT_FLOAT_EQ(v, 0.0f);
}

// The recurrence along the north column over flat ground reduces to
//
//     post k is visible  <=>  k(k-1) * drop(one step) < observer height
//
// which puts the last visible post at d ~ sqrt(2Rh) — the horizon. This is
// the one place the curvature model is observable on its own, so it is
// pinned exactly rather than approximately.
TEST(AnalysisViewshed, TheHorizonIsWhereTheCurvatureModelPutsIt) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 2.0;
  req.range_m = 10000.0;
  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, req, &r).ok());

  const double drop_one = Drop(kStepM);
  int first_hidden = 0;
  for (int k = 1;; ++k) {
    if (k * (k - 1) * drop_one >= req.observer_height_m) {
      first_hidden = k;
      break;
    }
  }
  ASSERT_GT(first_hidden, 1);
  ASSERT_LT(first_hidden, r.span / 2);

  // Textbook horizon for a 2 m eye, for scale: about 5.05 km.
  EXPECT_NEAR(first_hidden * kStepM, std::sqrt(2.0 * kR * 2.0), 200.0);

  for (int k = 1; k < first_hidden; ++k) {
    EXPECT_FLOAT_EQ(North(r, k), 0.0f) << "post " << k << " should be visible";
  }
  EXPECT_GT(North(r, first_hidden), 0.0f);

  // And beyond it the height needed to be seen only grows.
  for (int k = first_hidden; k + 1 < r.span / 2; ++k) {
    EXPECT_LT(North(r, k), North(r, k + 1)) << "post " << k;
  }
}

// ---------------------------------------------------------------------------
// A wall, and the shadow behind it
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, AWallCastsTheShadowTheGeometrySays) {
  const int kWallRing = 20;
  const double kWallHeight = 300.0;
  WallElevation src(kWallRing, kWallHeight);

  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 2.0;
  req.range_m = kStepM * 60.0;
  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, req, &r).ok());
  ASSERT_GT(r.span / 2, 40);

  // In front of the wall: flat ground well inside the horizon, all visible.
  for (int k = 1; k < kWallRing; ++k) {
    EXPECT_FLOAT_EQ(North(r, k), 0.0f) << "post " << k << " is in front of it";
  }
  // The wall itself is visible — it is what you are looking at.
  EXPECT_FLOAT_EQ(North(r, kWallRing), 0.0f);

  // Behind it the ray is the one grazing the wall top, so the height a thing
  // must reach at post k is
  //     los * d_k + drop(d_k) + h,   los = (W - drop(d_wall) - h) / d_wall
  const double d_wall = kWallRing * kStepM;
  const double los = (kWallHeight - Drop(d_wall) - req.observer_height_m) / d_wall;
  for (int k = kWallRing + 1; k < 40; ++k) {
    const double d = k * kStepM;
    const double want = los * d + Drop(d) + req.observer_height_m;
    EXPECT_NEAR(North(r, k), want, std::fabs(want) * 1e-4)
        << "post " << k << " in the shadow";
  }
}

// A VOID post takes no answer of its own and passes the ray THROUGH: the
// branch that would re-derive a slope from the missing ground is never taken,
// because `losRise < elevationChange` is false against a NaN. So the shadow
// behind a wall is the same shadow whether or not there is a hole in the
// coverage lying in it — which is the right answer, and is easy to break by
// "tidying" the NaN handling in DetermineVisibility.
TEST(AnalysisViewshed, AVoidPostIsUnansweredAndTheRayCarriesOnThroughIt) {
  const int kWallRing = 16;
  const int kHoleRing = 22;
  const double kWallHeight = 300.0;

  ViewshedRequest req = BaseRequest();
  req.range_m = kStepM * 45.0;

  WallElevation solid(kWallRing, kWallHeight);
  ViewshedResult without;
  ASSERT_TRUE(ComputeViewshed(solid, req, &without).ok());

  WallWithHoleElevation holed(kWallRing, kWallHeight, kHoleRing);
  ViewshedResult with;
  ASSERT_TRUE(ComputeViewshed(holed, req, &with).ok());

  // The hole itself has no answer, and it is not the only post in its ring
  // that lost one (the void is a band of latitude).
  EXPECT_TRUE(std::isnan(North(with, kHoleRing)));
  EXPECT_GT(with.no_data_count, 0u);
  EXPECT_EQ(without.no_data_count, 0u);

  // Everything behind it is untouched.
  for (int k = kHoleRing + 1; k < 40; ++k) {
    ASSERT_FALSE(std::isnan(North(with, k))) << "post " << k;
    EXPECT_FLOAT_EQ(North(with, k), North(without, k)) << "post " << k;
  }
}

// ---------------------------------------------------------------------------
// The three height methods
// ---------------------------------------------------------------------------

// min <= interpolated <= max, at every post. This is the invariant the three
// methods exist to provide — the two outer ones BRACKET the answer — and it
// holds because the interpolated slope is a convex combination of the two
// parents' own interpolated slopes: for a wall post at ring b and wall step
// s, theta works out to -s/b, so `base + (base - nonBase) * theta` moves
// from base toward nonBase by the fraction s/b and can never leave the pair.
// (Checked off-line across ~74 000 posts on six terrains, gentle to violent,
// with and without a sector crop and voids: zero violations.)
TEST(AnalysisViewshed, TheThreeMethodsBracketEachOther) {
  RoughElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 10.0;
  req.range_m = kStepM * 40.0;

  ViewshedResult lo;
  ViewshedResult mid;
  ViewshedResult hi;
  req.method = HeightMethod::kMin;
  ASSERT_TRUE(ComputeViewshed(src, req, &lo).ok());
  req.method = HeightMethod::kInterpolated;
  ASSERT_TRUE(ComputeViewshed(src, req, &mid).ok());
  req.method = HeightMethod::kMax;
  ASSERT_TRUE(ComputeViewshed(src, req, &hi).ok());

  ASSERT_EQ(lo.span, hi.span);
  ASSERT_EQ(lo.span, mid.span);
  ASSERT_GT(lo.visible_height_m.size(), 5000u);

  size_t differ = 0;
  for (size_t i = 0; i < lo.visible_height_m.size(); ++i) {
    const float a = lo.visible_height_m[i];
    const float b = mid.visible_height_m[i];
    const float c = hi.visible_height_m[i];
    ASSERT_FALSE(std::isnan(a)) << "post " << i;
    EXPECT_LE(a, b + 1e-3f) << "post " << i;
    EXPECT_LE(b, c + 1e-3f) << "post " << i;
    if (a != c) ++differ;
  }
  // The whole reason min and max are computed at all: they are not the same
  // answer. A fixture where they never differed would be pinning nothing.
  EXPECT_GT(differ, lo.visible_height_m.size() / 2);
}

// The three agree everywhere on a surface with nothing to occlude: with no
// wall, no parent slope is ever raised, so there is nothing to bracket.
TEST(AnalysisViewshed, TheThreeMethodsAgreeOverFlatGround) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.range_m = 3000.0;
  req.observer_height_m = 100.0;

  ViewshedResult lo;
  ViewshedResult hi;
  req.method = HeightMethod::kMin;
  ASSERT_TRUE(ComputeViewshed(src, req, &lo).ok());
  req.method = HeightMethod::kMax;
  ASSERT_TRUE(ComputeViewshed(src, req, &hi).ok());
  for (size_t i = 0; i < lo.visible_height_m.size(); ++i) {
    EXPECT_FLOAT_EQ(lo.visible_height_m[i], hi.visible_height_m[i]) << i;
  }
}

// ---------------------------------------------------------------------------
// The sector crop
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, ASectorLeavesTheGroundBehindYouUnanswered) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 100.0;
  req.range_m = 3000.0;
  req.has_sector = true;
  req.sector.bearing_deg = 0.0;
  req.sector.angle_deg = 90.0;

  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, req, &r).ok());

  const int c = r.span / 2;
  const int k = c / 2;
  EXPECT_FALSE(std::isnan(r.At(c - k, c))) << "due north is inside the sector";
  EXPECT_TRUE(std::isnan(r.At(c + k, c))) << "due south is behind you";
  EXPECT_GT(r.no_data_count, 0u);
  EXPECT_LT(r.no_data_count, r.visible_height_m.size());

  // The crop also SAVES the reads, which is most of the point of it.
  FlatElevation whole_src;
  ViewshedRequest whole = req;
  whole.has_sector = false;
  ViewshedResult all;
  ASSERT_TRUE(ComputeViewshed(whole_src, whole, &all).ok());
  EXPECT_EQ(all.no_data_count, 0u);
  EXPECT_LT(src.queries, whole_src.queries);
}

// 270 degrees or more is no crop at all — upstream's own rule, and the reason
// a "sector" object dragged out most of the way round behaves like a circle.
TEST(AnalysisViewshed, ASectorOf270DegreesIsNoCropAtAll) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 100.0;
  req.range_m = 2000.0;
  req.has_sector = true;
  req.sector.bearing_deg = 0.0;
  req.sector.angle_deg = 270.0;

  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, req, &r).ok());
  EXPECT_EQ(r.no_data_count, 0u);
}

// ---------------------------------------------------------------------------
// Progress and cancel
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, ProgressIsReportedAndCancelStops) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 100.0;
  req.range_m = 4000.0;

  CancelAt never(1000);
  ViewshedResult full;
  ASSERT_TRUE(ComputeViewshed(src, req, &full, &never).ok());
  EXPECT_GT(never.calls, 1);
  EXPECT_LE(never.calls, 101);  // one per whole percent at most

  CancelAt half(50);
  ViewshedResult stopped;
  const Status s = ComputeViewshed(src, req, &stopped, &half);
  EXPECT_EQ(s.code, fv::kInterrupted);
  EXPECT_GE(half.last, 50);
  // A cancelled viewshed hands back nothing rather than a half-filled grid
  // that a raster would happily draw.
  EXPECT_FALSE(stopped.Valid());
  EXPECT_TRUE(stopped.visible_height_m.empty());
}

// ---------------------------------------------------------------------------
// The post budget
// ---------------------------------------------------------------------------

TEST(AnalysisViewshed, TheBudgetWidensTheStepAndKeepsTheRange) {
  FlatElevation src;
  ViewshedRequest req = BaseRequest();
  req.observer_height_m = 100.0;
  req.range_m = 20000.0;
  req.max_posts = 2500;  // a 51 x 51 lattice at most

  ViewshedResult r;
  ASSERT_TRUE(ComputeViewshed(src, req, &r).ok());

  EXPECT_TRUE(r.step_was_widened);
  EXPECT_EQ(r.span % 2, 1);
  EXPECT_LE(r.span, 51);
  EXPECT_GT(r.step_deg, kStepDeg);

  // The range asked for is the range delivered: the northernmost post is
  // still the full radius away.
  const double half_span_deg = (r.span / 2) * r.step_deg;
  EXPECT_NEAR(half_span_deg * kMetersPerDegree, req.range_m, kStepM);
}

TEST(AnalysisViewshed, StepFromPostSpacingTakesTheCoarserAxis) {
  class Anisotropic : public fv::IElevationSource {
   public:
    GeoRect Bounds() const override { return GeoRect::World(); }
    Status GetElevation(const GeoPoint&, float* out) override {
      *out = 0.0f;
      return Status::Ok();
    }
    bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
      if (lat) *lat = 1.0 / 1200.0;  // 3 arcseconds
      if (lon) *lon = 1.0 / 400.0;   // 9 arcseconds, DTED-1 north of 50
      return true;
    }
  } src;

  // At 60 N the 9-arcsecond longitude posts are 9 * cos(60) = 4.5 arcseconds
  // of latitude apart on the ground — still the coarser of the two.
  const double step = fv::analysis::ViewshedStepFromPostSpacing(
      src, GeoPoint{60.0, 0.0});
  EXPECT_NEAR(step, (1.0 / 400.0) * std::cos(60.0 * M_PI / 180.0), 1e-12);
  EXPECT_GT(step, 1.0 / 1200.0);

  class Silent : public fv::IElevationSource {
   public:
    GeoRect Bounds() const override { return GeoRect::World(); }
    Status GetElevation(const GeoPoint&, float* out) override {
      *out = 0.0f;
      return Status::Ok();
    }
  } silent;
  EXPECT_DOUBLE_EQ(
      fv::analysis::ViewshedStepFromPostSpacing(silent, GeoPoint{0.0, 0.0}),
      0.0);
}

}  // namespace
