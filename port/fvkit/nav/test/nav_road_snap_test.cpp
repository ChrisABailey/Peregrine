// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Snap-to-road, the layer's own tests (nav plan MM5).
//
// Every road here is a straight line laid out in METRES from one anchor point,
// so an assertion reads as the picture it came from: "a road 12 metres north
// of another", "a fix 4 metres off it". The network is a fake — the whole
// point of MM5's seam is that the snapper never sees a RoadGraph — and the
// real graph is exercised in port/Routing/test/road_snap_test.cpp, over
// Kiawah, which is where a tagging shape nobody thought of would show up.

#include "fvkit/nav/road_snap.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

using fv::GeoPoint;
using fv::PositionFix;
using fv::RoadCandidate;
using fv::RoadSnapper;
using fv::SegmentProjection;
using fv::SnappedFix;

constexpr double kMetersPerDegLat = 6371008.8 * 3.14159265358979323846 / 180.0;

// Somewhere on Kiawah, so the cos(lat) the code applies is a real one rather
// than 1.0 — a bug that dropped the longitude scaling would pass at the
// equator.
const GeoPoint kAnchor{32.6000000, -80.0800000};

GeoPoint Offset(const GeoPoint& p, double east_m, double north_m) {
  const double cos_lat = std::cos(p.lat * 3.14159265358979323846 / 180.0);
  return GeoPoint{p.lat + north_m / kMetersPerDegLat,
                  p.lon + east_m / (kMetersPerDegLat * cos_lat)};
}

// ---------------------------------------------------------------------------
// A network of straight roads
// ---------------------------------------------------------------------------

struct FakeRoad {
  uint64_t id = 0;
  GeoPoint a;
  GeoPoint b;
  uint64_t node_a = 0;
  uint64_t node_b = 0;
  bool one_way = false;
  std::string name;
};

class FakeNetwork : public fv::IRoadNetwork {
 public:
  void Add(const FakeRoad& r) { roads_.push_back(r); }

  void QueryNear(const GeoPoint& p, double radius_m,
                 std::vector<RoadCandidate>* out) const override {
    for (const FakeRoad& r : roads_) {
      SegmentProjection sp;
      if (!fv::ProjectOntoSegment(p, r.a, r.b, &sp)) continue;
      if (sp.distance_m > radius_m) continue;
      RoadCandidate c;
      c.arc = r.id;
      c.from_node = r.node_a;
      c.to_node = r.node_b;
      c.point = sp.point;
      c.distance_m = sp.distance_m;
      c.along_m = sp.along_m;
      c.length_m = sp.length_m;
      c.bearing_deg = sp.bearing_deg;
      c.one_way = r.one_way;
      c.name = r.name;
      out->push_back(c);
    }
  }

 private:
  std::vector<FakeRoad> roads_;
};

// Two roads running east, `apart_m` apart, ids 1 (south) and 2 (north). They
// share no node: parallel carriageways, or a road and the cycleway beside it,
// which is the pair a nearest-edge snapper flaps between.
std::shared_ptr<FakeNetwork> ParallelRoads(double apart_m) {
  auto net = std::make_shared<FakeNetwork>();
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 0.0), Offset(kAnchor, 200.0, 0.0),
                    10, 11, false, "South Road"});
  net->Add(FakeRoad{2, Offset(kAnchor, -200.0, apart_m),
                    Offset(kAnchor, 200.0, apart_m), 20, 21, false, "North Road"});
  return net;
}

PositionFix FixAt(const GeoPoint& p) {
  PositionFix f;
  f.SetPosition(p.lat, p.lon);
  return f;
}

PositionFix MovingFixAt(const GeoPoint& p, double speed_mps) {
  PositionFix f = FixAt(p);
  f.speed_mps = speed_mps;
  f.has_speed = true;
  return f;
}

// ---------------------------------------------------------------------------
// ProjectOntoSegment
// ---------------------------------------------------------------------------

TEST(RoadSnapGeometry, DropsAPerpendicularOntoTheSegment) {
  const GeoPoint a = Offset(kAnchor, -100.0, 0.0);
  const GeoPoint b = Offset(kAnchor, 100.0, 0.0);
  SegmentProjection sp;
  ASSERT_TRUE(fv::ProjectOntoSegment(Offset(kAnchor, 0.0, 10.0), a, b, &sp));

  EXPECT_NEAR(sp.distance_m, 10.0, 0.05);
  EXPECT_NEAR(sp.length_m, 200.0, 0.05);
  EXPECT_NEAR(sp.along_m, 100.0, 0.05);
  EXPECT_NEAR(sp.bearing_deg, 90.0, 0.01);  // the segment runs EAST
  // The foot of the perpendicular is on the segment, not at the fix.
  EXPECT_NEAR(sp.point.lat, kAnchor.lat, 1e-9);
}

TEST(RoadSnapGeometry, ClampsToTheEndOfTheSegmentRatherThanExtendingIt) {
  const GeoPoint a = Offset(kAnchor, -100.0, 0.0);
  const GeoPoint b = Offset(kAnchor, 100.0, 0.0);
  SegmentProjection sp;
  // 30 m past the east end, and 40 m north of the line: the nearest point of
  // the SEGMENT is its endpoint, 50 m away.
  ASSERT_TRUE(fv::ProjectOntoSegment(Offset(kAnchor, 130.0, 40.0), a, b, &sp));
  EXPECT_NEAR(sp.distance_m, 50.0, 0.1);
  EXPECT_NEAR(sp.along_m, sp.length_m, 1e-6);
}

TEST(RoadSnapGeometry, HasADistanceButNoBearingForARepeatedVertex) {
  const GeoPoint a = Offset(kAnchor, 0.0, 0.0);
  SegmentProjection sp;
  EXPECT_FALSE(fv::ProjectOntoSegment(Offset(kAnchor, 0.0, 25.0), a, a, &sp));
  EXPECT_NEAR(sp.distance_m, 25.0, 0.05);
  EXPECT_EQ(sp.length_m, 0.0);
}

TEST(RoadSnapGeometry, ReadsBearingClockwiseFromNorth) {
  struct Case { double east, north, bearing; };
  const Case cases[] = {{0.0, 100.0, 0.0},    {100.0, 100.0, 45.0},
                        {100.0, 0.0, 90.0},   {100.0, -100.0, 135.0},
                        {0.0, -100.0, 180.0}, {-100.0, -100.0, 225.0},
                        {-100.0, 0.0, 270.0}, {-100.0, 100.0, 315.0}};
  for (const Case& c : cases) {
    SegmentProjection sp;
    ASSERT_TRUE(fv::ProjectOntoSegment(kAnchor, kAnchor,
                                       Offset(kAnchor, c.east, c.north), &sp));
    EXPECT_NEAR(sp.bearing_deg, c.bearing, 0.05)
        << "east " << c.east << " north " << c.north;
  }
}

// ---------------------------------------------------------------------------
// The feature being off
// ---------------------------------------------------------------------------

TEST(RoadSnapper, PassesEveryFixThroughWithNoNetwork) {
  RoadSnapper snapper;
  EXPECT_FALSE(snapper.enabled());

  const PositionFix f = FixAt(Offset(kAnchor, 0.0, 8.0));
  const SnappedFix s = snapper.Snap(f);
  EXPECT_FALSE(s.snapped);
  EXPECT_EQ(s.arc, fv::kNoRoadArc);
  EXPECT_EQ(s.position.lat, f.lat);
  EXPECT_EQ(s.position.lon, f.lon);
  // Applied() is the raw fix, unmodified, which is what a shell draws.
  EXPECT_EQ(s.Applied().lat, f.lat);
  EXPECT_FALSE(s.Applied().has_true_heading);
}

TEST(RoadSnapper, DoesNotSnapAFixWithNoPosition) {
  RoadSnapper snapper(ParallelRoads(12.0));
  PositionFix f;
  f.speed_mps = 10.0;
  f.has_speed = true;
  EXPECT_FALSE(snapper.Snap(f).snapped);
}

TEST(RoadSnapper, ReportsNothingWhenTheShipIsOffTheNetwork) {
  RoadSnapper snapper(ParallelRoads(12.0));
  const SnappedFix s = snapper.Snap(FixAt(Offset(kAnchor, 0.0, 400.0)));
  EXPECT_FALSE(s.snapped);
  EXPECT_EQ(s.candidates, 0);
}

// ---------------------------------------------------------------------------
// Choosing a road
// ---------------------------------------------------------------------------

TEST(RoadSnapper, TakesTheNearerOfTwoRoadsAndProjectsOntoIt) {
  RoadSnapper snapper(ParallelRoads(12.0));
  // 4 m north of the south road, so 8 m south of the north one.
  const SnappedFix s = snapper.Snap(FixAt(Offset(kAnchor, 0.0, 4.0)));
  ASSERT_TRUE(s.snapped);
  EXPECT_EQ(s.arc, 1u);
  EXPECT_EQ(s.road_name, "South Road");
  EXPECT_EQ(s.candidates, 2);
  EXPECT_NEAR(s.offset_m, 4.0, 0.05);
  // The snapped position is ON the road, not where the receiver said.
  EXPECT_NEAR(s.position.lat, kAnchor.lat, 1e-9);
  // and the raw fix is still there.
  EXPECT_NEAR(s.raw.lat, Offset(kAnchor, 0.0, 4.0).lat, 1e-12);
}

TEST(RoadSnapper, PrefersTheRoadThatPointsTheWayTheShipIsGoing) {
  auto net = std::make_shared<FakeNetwork>();
  // An east-west road 6 m north of the fix, and a north-south one 4 m east.
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 6.0), Offset(kAnchor, 200.0, 6.0),
                    10, 11, false, "East West"});
  net->Add(FakeRoad{2, Offset(kAnchor, 4.0, -200.0), Offset(kAnchor, 4.0, 200.0),
                    20, 21, false, "North South"});

  // Nearest-edge alone takes the north-south road: it is 2 m closer.
  RoadSnapper plain(net);
  EXPECT_EQ(plain.Snap(FixAt(kAnchor)).arc, 2u);

  // Driving east, the perpendicular road costs half the heading penalty
  // (10 m of the 20), which is more than the 2 m it saves.
  RoadSnapper heading(net);
  PositionFix f = MovingFixAt(kAnchor, 12.0);
  f.true_heading_deg = 90.0;
  f.has_true_heading = true;
  const SnappedFix s = heading.Snap(f);
  EXPECT_EQ(s.arc, 1u);
  EXPECT_NEAR(s.bearing_deg, 90.0, 0.05);
}

TEST(RoadSnapper, TakesTheHeadingOfThePreviousFixWhenTheFixHasNone) {
  auto net = std::make_shared<FakeNetwork>();
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 6.0), Offset(kAnchor, 200.0, 6.0),
                    10, 11, false, "East West"});
  net->Add(FakeRoad{2, Offset(kAnchor, 4.0, -200.0), Offset(kAnchor, 4.0, 200.0),
                    20, 21, false, "North South"});
  RoadSnapper snapper(net);
  // Same geometry as above; the heading arrives as the caller's prior, which
  // is what MovingMapOverlay hands it.
  EXPECT_EQ(snapper.Snap(MovingFixAt(kAnchor, 12.0), 90.0, true).arc, 1u);
}

TEST(RoadSnapper, ReversesATwoWayRoadsBearingToMatchTheHeading) {
  RoadSnapper snapper(ParallelRoads(40.0));  // the north road is out of range
  PositionFix f = MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0);
  f.true_heading_deg = 270.0;  // driving WEST along a road stored west-to-east
  f.has_true_heading = true;
  const SnappedFix s = snapper.Snap(f);
  ASSERT_TRUE(s.snapped);
  EXPECT_TRUE(s.has_bearing);
  EXPECT_NEAR(s.bearing_deg, 270.0, 0.05);
  EXPECT_NEAR(s.Applied().true_heading_deg, 270.0, 0.05);
}

TEST(RoadSnapper, KeepsAOneWayRoadsOwnDirectionWhateverTheHeadingSays) {
  auto net = std::make_shared<FakeNetwork>();
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 0.0), Offset(kAnchor, 200.0, 0.0),
                    10, 11, true, "One Way East"});
  RoadSnapper snapper(net);
  PositionFix f = MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0);
  f.true_heading_deg = 270.0;
  f.has_true_heading = true;
  const SnappedFix s = snapper.Snap(f);
  ASSERT_TRUE(s.snapped);
  EXPECT_NEAR(s.bearing_deg, 90.0, 0.05);
}

TEST(RoadSnapper, ReportsNoBearingForATwoWayRoadWithNoHeadingToChooseFrom) {
  RoadSnapper snapper(ParallelRoads(40.0));
  const SnappedFix s = snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0));
  ASSERT_TRUE(s.snapped);
  EXPECT_FALSE(s.has_bearing);
  // ... and Applied() therefore invents no heading.
  EXPECT_FALSE(s.Applied().has_true_heading);
}

// ---------------------------------------------------------------------------
// Hysteresis — the reason MM5 is not one line of nearest-edge
// ---------------------------------------------------------------------------

TEST(RoadSnapper, DoesNotFlapBetweenTwoParallelRoads) {
  RoadSnapper snapper(ParallelRoads(12.0));
  // The first fix settles on the south road.
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, -50.0, 3.0), 10.0)).arc, 1u);

  // Now noise pushes the fix back and forth across the middle. Every second
  // one is NEARER the north road; a nearest-edge snapper changes road on each
  // of them, several times a minute, and the ownship jumps a lane.
  const double offsets[] = {8.0, 4.0, 9.0, 5.0, 8.5, 3.0};
  double east = -40.0;
  for (double north : offsets) {
    const SnappedFix s = snapper.Snap(MovingFixAt(Offset(kAnchor, east, north), 10.0));
    EXPECT_EQ(s.arc, 1u) << "at " << north << " m north";
    east += 10.0;
  }
}

TEST(RoadSnapper, StillChangesRoadWhenTheShipHasGenuinelyMoved) {
  RoadSnapper snapper(ParallelRoads(40.0));
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 2.0), 10.0)).arc, 1u);
  // 38 m north of the south road, 2 m from the north one: no bonus saves it.
  EXPECT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 10.0, 38.0), 10.0)).arc, 2u);
}

TEST(RoadSnapper, PrefersAConnectedRoadOverAStrangerAtTheSameDistance) {
  auto net = std::make_shared<FakeNetwork>();
  // A road running east into junction J (node 11) at the anchor...
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 0.0), kAnchor, 10, 11, false, "Main"});
  // ... a road leaving J to the north-east ...
  net->Add(FakeRoad{2, kAnchor, Offset(kAnchor, 140.0, 140.0), 11, 12, false, "Turn"});
  // ... and an unconnected road parallel to it and 2 m NEARER the fix below,
  // which is what makes this a test of connectivity rather than of geometry:
  // on distance alone the stranger wins, and it takes the connected bonus (5 m
  // for an arc sharing an end with the one we are on) to lose.
  net->Add(FakeRoad{3, Offset(kAnchor, 0.0, 2.8), Offset(kAnchor, 140.0, 142.8),
                    30, 31, false, "Stranger"});

  // The control: with no road behind it, the snapper takes the nearer one.
  RoadSnapper fresh(net);
  PositionFix f = MovingFixAt(Offset(kAnchor, 46.0, 50.0), 10.0);
  f.true_heading_deg = 45.0;  // aligned with both, so the heading cancels
  f.has_true_heading = true;
  EXPECT_EQ(fresh.Snap(f).arc, 3u);

  RoadSnapper snapper(net);
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, -100.0, 0.0), 10.0)).arc, 1u);

  // Turning off the end of Main: only one of the two is a road this ship could
  // have reached from the one it is on.
  const SnappedFix s = snapper.Snap(f);
  EXPECT_EQ(s.arc, 2u);
}

TEST(RoadSnapper, ForgetsThePreviousRoadOnReset) {
  RoadSnapper snapper(ParallelRoads(12.0));
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0)).arc, 1u);
  snapper.Reset();
  // The same fix that stayed on road 1 above now goes to the nearer road,
  // because there is no "the road I am on" any more.
  EXPECT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 10.0, 8.0), 10.0)).arc, 2u);
}

TEST(RoadSnapper, ForgetsThePreviousRoadAfterAGapOffTheNetwork) {
  RoadSnapper snapper(ParallelRoads(12.0));
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0)).arc, 1u);
  // Out of range of everything — a tunnel, a car park, the edge of the data.
  ASSERT_FALSE(snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 500.0), 10.0)).snapped);
  // Back on: the bonus must not survive the gap.
  EXPECT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 10.0, 8.0), 10.0)).arc, 2u);
}

// ---------------------------------------------------------------------------
// The standstill
// ---------------------------------------------------------------------------

TEST(RoadSnapper, HoldsTheRoadItIsOnBelowWalkingPace) {
  RoadSnapper snapper(ParallelRoads(12.0));
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0)).arc, 1u);

  // Stopped, and the scatter has put this fix ON the other road — far enough
  // that the ordinary stay bonus would not have saved it. The control says so:
  // the identical fix at speed changes road.
  RoadSnapper moving(ParallelRoads(12.0));
  ASSERT_EQ(moving.Snap(MovingFixAt(Offset(kAnchor, 0.0, 3.0), 10.0)).arc, 1u);
  EXPECT_EQ(moving.Snap(MovingFixAt(Offset(kAnchor, 30.0, 12.0), 10.0)).arc, 2u);

  // Held, and the position is re-projected onto the road it is on rather than
  // frozen at the last one.
  const SnappedFix s = snapper.Snap(MovingFixAt(Offset(kAnchor, 30.0, 12.0), 0.2));
  EXPECT_EQ(s.arc, 1u);
  EXPECT_TRUE(s.held);
  EXPECT_FALSE(s.has_bearing);  // a stationary ship's heading is not the road's
  EXPECT_NEAR(s.position.lat, kAnchor.lat, 1e-9);
  EXPECT_GT(s.position.lon, Offset(kAnchor, 20.0, 0.0).lon);
}

TEST(RoadSnapper, HasNothingToHoldOnTheFirstFix) {
  RoadSnapper snapper(ParallelRoads(12.0));
  const SnappedFix s = snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 9.0), 0.0));
  ASSERT_TRUE(s.snapped);
  EXPECT_FALSE(s.held);
  EXPECT_EQ(s.arc, 2u);  // the ordinary scoring, on the nearer road
}

TEST(RoadSnapper, LetsGoOfAHeldRoadOnceTheShipHasDriftedOffIt) {
  RoadSnapper snapper(ParallelRoads(40.0));
  ASSERT_EQ(snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 2.0), 10.0)).arc, 1u);
  // Still slow, but the south road is now 38 m away and out of the search
  // radius entirely: an infinite bonus on a candidate that was never offered
  // holds nothing.
  const SnappedFix s = snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 38.0), 0.1));
  EXPECT_EQ(s.arc, 2u);
  EXPECT_FALSE(s.held);
}

// ---------------------------------------------------------------------------
// The search radius, and how sure it is
// ---------------------------------------------------------------------------

TEST(RoadSnapper, SizesItsSearchRadiusFromTheFixsOwnHdop) {
  auto net = std::make_shared<FakeNetwork>();
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 40.0), Offset(kAnchor, 200.0, 40.0),
                    10, 11, false, "Far Road"});

  // A good fix: radius 15 m (the floor, since 1 x 5 is under it) and the road
  // is 40 m away. Nothing.
  RoadSnapper tight(net);
  PositionFix good = FixAt(kAnchor);
  good.hdop = 1.0;
  good.has_hdop = true;
  EXPECT_FALSE(tight.Snap(good).snapped);

  // A poor fix: radius 50 m, and the same road is now a plausible explanation
  // of where the ship is.
  RoadSnapper loose(net);
  PositionFix poor = FixAt(kAnchor);
  poor.hdop = 10.0;
  poor.has_hdop = true;
  const SnappedFix s = loose.Snap(poor);
  ASSERT_TRUE(s.snapped);
  // ... but it says so: 40 of a 50 m radius is barely inside the rim.
  EXPECT_LT(s.confidence, 0.3);
}

TEST(RoadSnapper, CapsTheRadiusAWildHdopWouldAskFor) {
  auto net = std::make_shared<FakeNetwork>();
  net->Add(FakeRoad{1, Offset(kAnchor, -200.0, 90.0), Offset(kAnchor, 200.0, 90.0),
                    10, 11, false, "Very Far Road"});
  RoadSnapper snapper(net);
  PositionFix f = FixAt(kAnchor);
  f.hdop = 99.0;  // 495 m unclamped; the cap is 60
  f.has_hdop = true;
  EXPECT_FALSE(snapper.Snap(f).snapped);
}

TEST(RoadSnapper, IsSureOnALoneRoadAndUnsureBetweenTwo) {
  RoadSnapper alone(ParallelRoads(200.0));  // the second road is far away
  const SnappedFix sure = alone.Snap(MovingFixAt(Offset(kAnchor, 0.0, 0.5), 10.0));
  ASSERT_TRUE(sure.snapped);
  EXPECT_GT(sure.confidence, 0.9);

  RoadSnapper between(ParallelRoads(12.0));
  const SnappedFix unsure =
      between.Snap(MovingFixAt(Offset(kAnchor, 0.0, 6.0), 10.0));
  ASSERT_TRUE(unsure.snapped);
  EXPECT_EQ(unsure.candidates, 2);
  EXPECT_LT(unsure.confidence, sure.confidence * 0.7);
}

TEST(RoadSnapper, OrdersTheCandidatesItConsideredBestFirst) {
  RoadSnapper snapper(ParallelRoads(12.0));
  const SnappedFix s = snapper.Snap(MovingFixAt(Offset(kAnchor, 0.0, 4.0), 10.0));
  ASSERT_EQ(snapper.last_candidates().size(), 2u);
  EXPECT_EQ(snapper.last_candidates()[0].arc, s.arc);
  EXPECT_EQ(snapper.last_candidates()[1].arc, 2u);
}

// ---------------------------------------------------------------------------
// What a consumer gets
// ---------------------------------------------------------------------------

TEST(SnappedFix, AppliedReplacesThePositionAndLeavesTheRawAlone) {
  RoadSnapper snapper(ParallelRoads(40.0));
  PositionFix f = MovingFixAt(Offset(kAnchor, 0.0, 5.0), 10.0);
  f.true_heading_deg = 80.0;  // a receiver's course, a few degrees off the road
  f.has_true_heading = true;
  f.altitude_msl_m = 3.0;
  f.has_altitude = true;

  const SnappedFix s = snapper.Snap(f);
  ASSERT_TRUE(s.snapped);
  const PositionFix applied = s.Applied();

  EXPECT_NEAR(applied.lat, kAnchor.lat, 1e-9);        // on the road
  EXPECT_NEAR(applied.true_heading_deg, 90.0, 0.05);  // the road's bearing
  EXPECT_TRUE(applied.has_true_heading);
  EXPECT_EQ(applied.altitude_msl_m, 3.0);  // everything else is carried
  EXPECT_NEAR(s.raw.lat, f.lat, 1e-12);    // and the raw fix is untouched
  EXPECT_NEAR(s.raw.true_heading_deg, 80.0, 1e-12);
}

}  // namespace
