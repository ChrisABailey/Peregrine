// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The geographic path (fvkit/analysis/path.h, plan step AN1).
//
// The fixtures are all paths whose answers are known on paper — a degree of
// latitude is 60 nautical miles, a box at the equator is a known number of
// square metres — because the failure mode here is not a crash but a leg
// measured along the wrong line, and only an answer you can predict catches
// that.

#include "fvkit/analysis/path.h"

#include <cmath>

#include "gtest/gtest.h"

namespace {

using fv::GeoPoint;
using fv::analysis::GeoPath;
using fv::analysis::LineType;

// One minute of arc is one nautical mile is 1852 m, so a degree of latitude
// is 60 * 1852 m on the sphere. geo_tool measures on the ellipsoid, so this
// is right to a few parts in a thousand and the tolerances say so.
constexpr double kDegreeLatMeters = 60.0 * 1852.0;

TEST(AnalysisPath, EmptyAndSinglePoint) {
  GeoPath empty;
  EXPECT_EQ(empty.PointCount(), 0u);
  EXPECT_EQ(empty.LegCount(), 0u);
  EXPECT_EQ(empty.TotalLength(), 0.0);
  EXPECT_FALSE(empty.Measured());
  GeoPoint p;
  EXPECT_FALSE(empty.PointAtDistance(0.0, &p));

  GeoPath one({GeoPoint{34.0, -84.0}}, LineType::kGreatCircle);
  EXPECT_EQ(one.PointCount(), 1u);
  EXPECT_EQ(one.LegCount(), 0u);
  EXPECT_EQ(one.TotalLength(), 0.0);
  EXPECT_TRUE(one.Measured());
  ASSERT_TRUE(one.PointAtDistance(1000.0, &p));
  EXPECT_DOUBLE_EQ(p.lat, 34.0);
}

TEST(AnalysisPath, MeridianLegIsOneDegreeOfLatitude) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}},
               LineType::kGreatCircle);
  ASSERT_EQ(path.LegCount(), 1u);
  EXPECT_TRUE(path.Leg(0).ok);
  EXPECT_NEAR(path.Leg(0).range_m, kDegreeLatMeters, kDegreeLatMeters * 0.005);
  EXPECT_NEAR(path.Leg(0).bearing_deg, 0.0, 1e-6);
  EXPECT_DOUBLE_EQ(path.CumulativeAt(0), 0.0);
  EXPECT_DOUBLE_EQ(path.CumulativeAt(1), path.TotalLength());
}

TEST(AnalysisPath, CumulativeHasOneEntryPerVertex) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{34.5, -84.0},
                GeoPoint{35.0, -84.0}, GeoPoint{35.0, -83.0}},
               LineType::kGreatCircle);
  ASSERT_EQ(path.PointCount(), 4u);
  ASSERT_EQ(path.LegCount(), 3u);
  for (size_t i = 0; i + 1 < path.PointCount(); ++i) {
    EXPECT_LT(path.CumulativeAt(i), path.CumulativeAt(i + 1)) << "leg " << i;
    EXPECT_NEAR(path.CumulativeAt(i + 1) - path.CumulativeAt(i),
                path.Leg(i).range_m, 1e-9);
  }
}

TEST(AnalysisPath, PointAtDistanceMidpointOfAMeridian) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}},
               LineType::kGreatCircle);
  GeoPoint mid;
  ASSERT_TRUE(path.PointAtDistance(path.TotalLength() / 2.0, &mid));
  // Half a degree of latitude up the same meridian.
  EXPECT_NEAR(mid.lat, 34.5, 1e-4);
  EXPECT_NEAR(mid.lon, -84.0, 1e-9);
}

TEST(AnalysisPath, PointAtDistanceClampsAtBothEnds) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}},
               LineType::kGreatCircle);
  GeoPoint p;
  ASSERT_TRUE(path.PointAtDistance(-1000.0, &p));
  EXPECT_DOUBLE_EQ(p.lat, 34.0);
  ASSERT_TRUE(path.PointAtDistance(path.TotalLength() * 2.0, &p));
  EXPECT_DOUBLE_EQ(p.lat, 35.0);
}

// The reason PointAtDistance interpolates along the LEG'S OWN line type: at
// this latitude and over this span the two lines are far enough apart to see.
TEST(AnalysisPath, GreatCircleAndRhumbMidpointsDiffer) {
  const std::vector<GeoPoint> ends{GeoPoint{60.0, -100.0}, GeoPoint{60.0, 0.0}};
  GeoPath gc(ends, LineType::kGreatCircle);
  GeoPath rl(ends, LineType::kRhumb);

  EXPECT_LT(gc.TotalLength(), rl.TotalLength());  // the great circle is shorter

  GeoPoint gc_mid;
  GeoPoint rl_mid;
  ASSERT_TRUE(gc.PointAtDistance(gc.TotalLength() / 2.0, &gc_mid));
  ASSERT_TRUE(rl.PointAtDistance(rl.TotalLength() / 2.0, &rl_mid));
  // The rhumb line holds the parallel; the great circle bulges poleward.
  EXPECT_NEAR(rl_mid.lat, 60.0, 1e-6);
  EXPECT_GT(gc_mid.lat, 62.0);
}

TEST(AnalysisPath, AntimeridianLegIsShort) {
  GeoPath path({GeoPoint{0.0, 179.5}, GeoPoint{0.0, -179.5}},
               LineType::kGreatCircle);
  ASSERT_TRUE(path.Leg(0).ok);
  // One degree of longitude at the equator, NOT 359 of them.
  EXPECT_NEAR(path.TotalLength(), kDegreeLatMeters, kDegreeLatMeters * 0.01);
}

TEST(AnalysisPath, DensifyKeepsEveryVertexExactly) {
  const std::vector<GeoPoint> verts{GeoPoint{34.0, -84.0},
                                    GeoPoint{34.5, -84.0},
                                    GeoPoint{34.5, -83.0}};
  GeoPath path(verts, LineType::kGreatCircle);
  const std::vector<GeoPoint> dense = path.Densify(5000.0);

  ASSERT_GT(dense.size(), verts.size());
  // Every vertex appears, to the last digit, and in order.
  size_t v = 0;
  for (const GeoPoint& p : dense) {
    if (v < verts.size() && p.lat == verts[v].lat && p.lon == verts[v].lon) {
      ++v;
    }
  }
  EXPECT_EQ(v, verts.size());

  // No gap wider than the step, allowing for the leg's own rounding up.
  for (size_t i = 0; i + 1 < dense.size(); ++i) {
    GeoPath seg({dense[i], dense[i + 1]}, LineType::kGreatCircle);
    EXPECT_LE(seg.TotalLength(), 5000.0 + 1.0) << "gap " << i;
  }
}

TEST(AnalysisPath, DensifyWidensTheStepToFitTheCap) {
  GeoPath path({GeoPoint{0.0, 0.0}, GeoPoint{10.0, 0.0}},
               LineType::kGreatCircle);
  const std::vector<GeoPoint> dense = path.Densify(100.0, 50);
  EXPECT_LE(dense.size(), 50u);
  EXPECT_GT(dense.size(), 2u);
  // The path is never truncated: the ends survive.
  EXPECT_DOUBLE_EQ(dense.front().lat, 0.0);
  EXPECT_DOUBLE_EQ(dense.back().lat, 10.0);
}

TEST(AnalysisPath, DensifyWithNoStepIsTheVerticesUnchanged) {
  const std::vector<GeoPoint> verts{GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}};
  GeoPath path(verts, LineType::kGreatCircle);
  EXPECT_EQ(path.Densify(0.0).size(), verts.size());
}

TEST(AnalysisPath, ResampleIsEvenlySpacedAlongASingleLeg) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -83.0}},
               LineType::kGreatCircle);
  const std::vector<GeoPoint> s = path.Resample(9);
  ASSERT_EQ(s.size(), 9u);

  const double want = path.TotalLength() / 8.0;
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    GeoPath seg({s[i], s[i + 1]}, LineType::kGreatCircle);
    EXPECT_NEAR(seg.TotalLength(), want, want * 0.01) << "gap " << i;
  }
}

// Resample spaces samples evenly ALONG THE PATH, which on a polyline is not
// evenly in a straight line: two samples either side of a turning point are
// closer together as the crow flies than the step. That is the correct
// answer, and the test that would have said otherwise was the wrong test.
TEST(AnalysisPath, ResampleOnAPolylineHasExactEndsAndCutsTheCorner) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{34.5, -84.0},
                GeoPoint{35.0, -83.0}},
               LineType::kGreatCircle);
  const std::vector<GeoPoint> s = path.Resample(9);
  ASSERT_EQ(s.size(), 9u);
  EXPECT_DOUBLE_EQ(s.front().lat, 34.0);
  EXPECT_DOUBLE_EQ(s.front().lon, -84.0);
  EXPECT_DOUBLE_EQ(s.back().lat, 35.0);
  EXPECT_DOUBLE_EQ(s.back().lon, -83.0);

  const double step = path.TotalLength() / 8.0;
  double straight_total = 0.0;
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    GeoPath seg({s[i], s[i + 1]}, LineType::kGreatCircle);
    // No gap is LONGER than the step; the one spanning the corner is shorter.
    EXPECT_LE(seg.TotalLength(), step * 1.01) << "gap " << i;
    straight_total += seg.TotalLength();
  }
  EXPECT_LT(straight_total, path.TotalLength());
}

TEST(AnalysisPath, ResampleDegenerateCounts) {
  GeoPath path({GeoPoint{34.0, -84.0}, GeoPoint{35.0, -84.0}},
               LineType::kGreatCircle);
  EXPECT_TRUE(path.Resample(0).empty());
  ASSERT_EQ(path.Resample(1).size(), 1u);
  EXPECT_DOUBLE_EQ(path.Resample(1).front().lat, 34.0);
}

TEST(AnalysisPath, AreaOfADegreeBoxAtTheEquator) {
  // A 1 degree x 1 degree box at the equator: about (60 nm)^2.
  GeoPath box({GeoPoint{0.0, 0.0}, GeoPoint{0.0, 1.0}, GeoPoint{1.0, 1.0},
               GeoPoint{1.0, 0.0}},
              LineType::kGreatCircle);
  const double want = kDegreeLatMeters * kDegreeLatMeters;
  EXPECT_NEAR(box.AreaSquareMeters(), want, want * 0.02);
}

TEST(AnalysisPath, AreaIsIndependentOfWindingDirection) {
  const std::vector<GeoPoint> cw{GeoPoint{0.0, 0.0}, GeoPoint{0.0, 1.0},
                                 GeoPoint{1.0, 1.0}, GeoPoint{1.0, 0.0}};
  std::vector<GeoPoint> ccw(cw.rbegin(), cw.rend());
  GeoPath a(cw, LineType::kGreatCircle);
  GeoPath b(ccw, LineType::kGreatCircle);
  EXPECT_NEAR(a.AreaSquareMeters(), b.AreaSquareMeters(),
              a.AreaSquareMeters() * 1e-6);
}

TEST(AnalysisPath, AreaOfFewerThanThreeVerticesIsZero) {
  GeoPath two({GeoPoint{0.0, 0.0}, GeoPoint{0.0, 1.0}}, LineType::kGreatCircle);
  EXPECT_DOUBLE_EQ(two.AreaSquareMeters(), 0.0);
}

TEST(AnalysisPath, SetLineTypeRemeasures) {
  GeoPath path({GeoPoint{60.0, -100.0}, GeoPoint{60.0, 0.0}},
               LineType::kGreatCircle);
  const double gc = path.TotalLength();
  path.SetLineType(LineType::kRhumb);
  EXPECT_GT(path.TotalLength(), gc);
}

}  // namespace
