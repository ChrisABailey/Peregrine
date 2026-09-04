// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The terrain profile (fvkit/analysis/profile.h, plan step AN2).
//
// Every source here is a surface whose elevation is a closed-form function of
// the coordinate, so the profile can be checked term by term. The Windows
// original could not be tested this way at all: it asked a COM map server for
// a block of DTED and read a diagonal out of it, and the diagonal is only the
// line at 45 degrees — which is precisely the bug a synthetic ramp exposes.

#include "fvkit/analysis/profile.h"

#include <cmath>
#include <limits>

#include "gtest/gtest.h"

namespace {

using fv::GeoPoint;
using fv::GeoRect;
using fv::Status;
using fv::analysis::GeoPath;
using fv::analysis::LineType;
using fv::analysis::ProfileOptions;
using fv::analysis::ProfileResult;
using fv::analysis::SampleTerrainProfile;

// Elevation is 1000 m per degree of latitude, so a profile up a meridian is a
// straight ramp whose value at every sample is arithmetic.
class RampElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    ++queries;
    *out = static_cast<float>((p.lat - 34.0) * 1000.0);
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0 / 1200.0;  // DTED-1, 3 arcseconds
    if (lon) *lon = 1.0 / 1200.0;
    return true;
  }
  int queries = 0;
};

// A ridge across the middle of a leg: flat at 100 m except a 2000 m wall in a
// narrow band of latitude. Sampling only the endpoints misses it entirely,
// which is exactly what the Windows multi-point profile does.
class RidgeElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    *out = std::fabs(p.lat - 34.5) < 0.02 ? 2000.0f : 100.0f;
    return Status::Ok();
  }
};

// Flat ground with a void band across it — a hole INSIDE the coverage.
class HoleyElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint& p, float* out) override {
    *out = std::fabs(p.lat - 34.5) < 0.05
               ? std::numeric_limits<float>::quiet_NaN()
               : 500.0f;
    return Status::Ok();
  }
};

// No coverage anywhere: every read refused.
class EmptyElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint&, float*) override {
    return Status::Error(fv::kOutOfCoverage, "no data here");
  }
};

// A source with 1-degree posts: coarse enough that a metre-scale step must be
// widened to it.
class CoarseElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{30.0, -90.0}, {40.0, -80.0}}; }
  Status GetElevation(const GeoPoint&, float* out) override {
    *out = 42.0f;
    return Status::Ok();
  }
  bool PostSpacing(const GeoPoint&, double* lat, double* lon) override {
    if (lat) *lat = 1.0;
    if (lon) *lon = 1.0;
    return true;
  }
};

GeoPath Meridian() {
  return GeoPath({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}},
                 LineType::kGreatCircle);
}

TEST(AnalysisProfile, RejectsAnUnusableRequest) {
  RampElevation src;
  ProfileResult r;
  ProfileOptions opts;
  opts.sample_count = 5;

  EXPECT_FALSE(SampleTerrainProfile(src, Meridian(), opts, nullptr).ok());
  EXPECT_FALSE(SampleTerrainProfile(src, GeoPath(), opts, &r).ok());

  ProfileOptions neither;  // no count, no step
  EXPECT_FALSE(SampleTerrainProfile(src, Meridian(), neither, &r).ok());
}

TEST(AnalysisProfile, SampleCountWalksTheLineNotItsBoundingBoxDiagonal) {
  RampElevation src;
  ProfileOptions opts;
  opts.sample_count = 11;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());

  ASSERT_EQ(r.points.size(), 11u);
  EXPECT_EQ(r.no_data_count, 0u);
  // N samples cost N reads. The Windows path cost N^2, which is why its
  // segment box was capped at 500.
  EXPECT_EQ(src.queries, 11);

  for (size_t i = 0; i < r.points.size(); ++i) {
    const double frac = static_cast<double>(i) / 10.0;
    EXPECT_TRUE(r.points[i].has_data) << i;
    EXPECT_NEAR(r.points[i].at.lat, 34.0 + frac, 1e-3) << i;
    EXPECT_NEAR(r.points[i].at.lon, -84.0, 1e-9) << i;
    EXPECT_NEAR(r.points[i].elevation_m, frac * 1000.0, 5.0) << i;
    EXPECT_NEAR(r.points[i].distance_m, r.total_distance_m * frac, 1e-6) << i;
  }
  EXPECT_NEAR(r.min_m, 0.0, 5.0);
  EXPECT_NEAR(r.max_m, 1000.0, 5.0);
  EXPECT_NEAR(r.gain_m, 1000.0, 10.0);
  EXPECT_NEAR(r.loss_m, 0.0, 1e-6);
}

// The bug the diagonal-of-a-block reader has and this one does not: an
// EAST-WEST line has a degenerate bounding box, so there is no diagonal to
// read at all. Here it is just a line like any other.
TEST(AnalysisProfile, AnEastWestLineIsSampledAlongItself) {
  RampElevation src;
  GeoPath path({GeoPoint{34.25, -84.0}, GeoPoint{34.25, -83.0}},
               LineType::kGreatCircle);
  ProfileOptions opts;
  opts.sample_count = 7;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, path, opts, &r).ok());

  ASSERT_EQ(r.points.size(), 7u);
  for (const auto& p : r.points) {
    EXPECT_TRUE(p.has_data);
    // The ramp only varies with latitude, and this line holds one.
    EXPECT_NEAR(p.elevation_m, 250.0, 5.0);
  }
  EXPECT_NEAR(r.max_m - r.min_m, 0.0, 5.0);
}

TEST(AnalysisProfile, EndpointsAreTheVerticesExactly) {
  RampElevation src;
  ProfileOptions opts;
  opts.sample_count = 5;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());
  EXPECT_DOUBLE_EQ(r.points.front().at.lat, 34.0);
  EXPECT_DOUBLE_EQ(r.points.front().at.lon, -84.0);
  EXPECT_DOUBLE_EQ(r.points.back().at.lat, 35.0);
  EXPECT_DOUBLE_EQ(r.points.back().at.lon, -84.0);
  EXPECT_DOUBLE_EQ(r.points.front().distance_m, 0.0);
  EXPECT_DOUBLE_EQ(r.points.back().distance_m, r.total_distance_m);
}

// The multi-point case. Windows samples the turning points and nothing else,
// so this ridge is invisible to it; stepping along the path finds it.
TEST(AnalysisProfile, TurningPointsAreNotTheWholeProfile) {
  RidgeElevation src;
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0},
                GeoPoint{35.0, -83.0}},
               LineType::kGreatCircle);

  ProfileOptions vertices_only;
  vertices_only.sample_count = 3;
  ProfileResult coarse;
  ASSERT_TRUE(SampleTerrainProfile(src, path, vertices_only, &coarse).ok());
  EXPECT_NEAR(coarse.max_m, 100.0, 1e-6);  // the ridge is missed, as upstream

  ProfileOptions stepped;
  stepped.step_m = 500.0;
  ProfileResult fine;
  ASSERT_TRUE(SampleTerrainProfile(src, path, stepped, &fine).ok());
  EXPECT_NEAR(fine.max_m, 2000.0, 1e-6);
}

TEST(AnalysisProfile, SteppedSamplingKeepsEveryTurningPoint) {
  RampElevation src;
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{34.5, -84.0},
                GeoPoint{34.5, -83.5}},
               LineType::kGreatCircle);
  ProfileOptions opts;
  opts.step_m = 4000.0;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, path, opts, &r).ok());

  size_t vertices = 0;
  for (const auto& p : r.points) {
    if (p.is_vertex) ++vertices;
  }
  EXPECT_EQ(vertices, 3u);

  // And they are the vertices, exactly.
  for (size_t v = 0; v < path.PointCount(); ++v) {
    bool found = false;
    for (const auto& p : r.points) {
      if (p.is_vertex && std::fabs(p.distance_m - path.CumulativeAt(v)) < 1e-6) {
        EXPECT_DOUBLE_EQ(p.at.lat, path.points()[v].lat);
        EXPECT_DOUBLE_EQ(p.at.lon, path.points()[v].lon);
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "vertex " << v;
  }

  // No gap wider than the step.
  for (size_t i = 0; i + 1 < r.points.size(); ++i) {
    EXPECT_LE(r.points[i + 1].distance_m - r.points[i].distance_m,
              opts.step_m + 1.0)
        << "gap " << i;
  }
}

TEST(AnalysisProfile, StepIsWidenedToThePostSpacing) {
  CoarseElevation src;  // 1-degree posts
  ProfileOptions opts;
  opts.step_m = 10.0;  // absurdly fine for those posts
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());

  EXPECT_TRUE(r.step_was_clamped);
  // A degree of LONGITUDE at 34.5 N — the FINER of the two axes, which is the
  // one the clamp takes (profile.cpp says why).
  EXPECT_NEAR(r.step_m, 60.0 * 1852.0 * std::cos(34.5 * 3.14159265358979323846 / 180.0),
              1000.0);
  EXPECT_LT(r.points.size(), 10u);
}

TEST(AnalysisProfile, ClampCanBeTurnedOff) {
  CoarseElevation src;
  ProfileOptions opts;
  opts.step_m = 10000.0;
  opts.clamp_to_post_spacing = false;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());
  EXPECT_FALSE(r.step_was_clamped);
  EXPECT_DOUBLE_EQ(r.step_m, 10000.0);
}

TEST(AnalysisProfile, MaxSamplesWidensTheStepRatherThanTruncating) {
  RampElevation src;
  ProfileOptions opts;
  opts.step_m = 10.0;
  opts.clamp_to_post_spacing = false;
  opts.max_samples = 40;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());

  EXPECT_TRUE(r.step_was_clamped);
  EXPECT_LE(r.points.size(), 41u);
  // The path still reaches its end.
  EXPECT_DOUBLE_EQ(r.points.back().at.lat, 35.0);
}

// A hole is a gap in the line, not a refusal to draw one. This is the one
// behavioural difference from the Windows dialog that a user would notice.
TEST(AnalysisProfile, AHoleIsReportedAndTheProfileStillReturns) {
  HoleyElevation src;
  ProfileOptions opts;
  opts.sample_count = 41;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());

  ASSERT_EQ(r.points.size(), 41u);
  EXPECT_GT(r.no_data_count, 0u);
  EXPECT_LT(r.no_data_count, r.points.size());
  EXPECT_NEAR(r.min_m, 500.0, 1e-6);
  EXPECT_NEAR(r.max_m, 500.0, 1e-6);

  size_t voids = 0;
  for (const auto& p : r.points) {
    if (!p.has_data) {
      EXPECT_TRUE(std::isnan(p.elevation_m));
      ++voids;
    }
  }
  EXPECT_EQ(voids, r.no_data_count);
}

TEST(AnalysisProfile, NoCoverageIsASuccessfulEmptyAnswer) {
  EmptyElevation src;
  ProfileOptions opts;
  opts.sample_count = 9;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, Meridian(), opts, &r).ok());

  ASSERT_EQ(r.points.size(), 9u);
  EXPECT_EQ(r.no_data_count, 9u);
  EXPECT_DOUBLE_EQ(r.min_m, 0.0);
  EXPECT_DOUBLE_EQ(r.max_m, 0.0);
  EXPECT_DOUBLE_EQ(r.gain_m, 0.0);
  for (const auto& p : r.points) EXPECT_FALSE(p.has_data);
}

TEST(AnalysisProfile, GainAndLossOverAValley) {
  // Down 1000 m and back up: the ramp read along a there-and-back path.
  RampElevation src;
  GeoPath path({GeoPoint{35.0, -84.0}, GeoPoint{34.0, -84.0},
                GeoPoint{35.0, -84.0}},
               LineType::kGreatCircle);
  ProfileOptions opts;
  opts.sample_count = 21;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, path, opts, &r).ok());
  EXPECT_NEAR(r.gain_m, 1000.0, 20.0);
  EXPECT_NEAR(r.loss_m, 1000.0, 20.0);
}

TEST(AnalysisProfile, ADegeneratePathIsOneSample) {
  RampElevation src;
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{34.0, -84.0}},
               LineType::kGreatCircle);
  ProfileOptions opts;
  opts.step_m = 100.0;
  ProfileResult r;
  ASSERT_TRUE(SampleTerrainProfile(src, path, opts, &r).ok());
  EXPECT_DOUBLE_EQ(r.total_distance_m, 0.0);
  ASSERT_EQ(r.points.size(), 1u);
  EXPECT_TRUE(r.points.front().has_data);
}

}  // namespace
