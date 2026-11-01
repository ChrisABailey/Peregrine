// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the AIS vessel table and CPA/TCPA, driven by scripted message
/// sequences on an explicit clock.

#include "fvkit/ais/vessel_table.h"

#include <gtest/gtest.h>

#include <vector>

namespace fv {
namespace ais {
namespace {

constexpr double kMetresPerDeg = 6371008.8 * 3.14159265358979323846 / 180.0;

/// A class A position report.
AisMessage Position(std::uint32_t mmsi, double lat, double lon, double sog_mps = 5.0,
                    double cog = 90.0, int type = 1) {
  AisMessage m;
  m.type = type;
  m.mmsi = mmsi;
  m.has_position = true;
  m.lat_deg = lat;
  m.lon_deg = lon;
  m.has_sog = true;
  m.sog_mps = sog_mps;
  m.has_cog = true;
  m.cog_deg = cog;
  m.has_heading = type != 27;
  m.heading_deg = static_cast<int>(cog);
  return m;
}

/// Feeds sentences through a decoder into `table` at `now_s`.
void FeedLines(VesselTable* table, std::initializer_list<const char*> lines, double now_s) {
  AisDecoder d;
  AisMessage m;
  for (const char* line : lines) {
    if (d.Feed(line, now_s, &m) == FeedResult::kMessage) table->Update(m, now_s);
  }
}

TEST(AisVesselTable, MergesDecodedStaticWithPosition) {
  VesselTable t;
  FeedLines(&t,
            {"!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C",
             "!AIVDM,2,2,1,A,88888888880,2*25"},
            10.0);
  t.Update(Position(351759000u, 32.75, -79.9), 12.0);
  const Vessel* v = t.Find(351759000u);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(v->name, "EVER DIADEM");
  EXPECT_TRUE(v->has_static);
  EXPECT_TRUE(v->has_position);
  EXPECT_EQ(v->vessel_class, VesselClass::kA);
  EXPECT_EQ(v->kind, TargetKind::kVessel);
  EXPECT_DOUBLE_EQ(v->first_heard_s, 10.0);
  EXPECT_DOUBLE_EQ(v->last_heard_s, 12.0);
  EXPECT_DOUBLE_EQ(v->static_time_s, 10.0);
  EXPECT_DOUBLE_EQ(v->position_time_s, 12.0);
  EXPECT_GT(v->length_m(), 0);
}

TEST(AisVesselTable, ClassBStaticPartsMerge) {
  VesselTable t;
  AisMessage a;
  a.type = 24;
  a.mmsi = 338087471u;
  a.part = 0;
  a.name = "SEA WIND";
  t.Update(a, 0);
  AisMessage b;
  b.type = 24;
  b.mmsi = 338087471u;
  b.part = 1;
  b.has_ship_type = true;
  b.ship_type = 36;
  b.call_sign = "WDA1234";
  b.has_dimensions = true;
  b.to_bow_m = 8;
  b.to_stern_m = 4;
  b.to_port_m = 2;
  b.to_starboard_m = 2;
  t.Update(b, 1);
  const Vessel* v = t.Find(338087471u);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(v->name, "SEA WIND");
  EXPECT_EQ(v->call_sign, "WDA1234");
  EXPECT_EQ(v->ship_type, 36);
  EXPECT_EQ(v->length_m(), 12);
  EXPECT_EQ(v->beam_m(), 4);
  EXPECT_EQ(v->vessel_class, VesselClass::kB);
  EXPECT_FALSE(v->has_position);
}

TEST(AisVesselTable, KindFromMmsiPrefixes) {
  EXPECT_EQ(KindFromMmsi(970123456u), TargetKind::kSart);
  EXPECT_EQ(KindFromMmsi(972123456u), TargetKind::kMob);
  EXPECT_EQ(KindFromMmsi(974123456u), TargetKind::kEpirb);
  EXPECT_EQ(KindFromMmsi(111257001u), TargetKind::kSarAircraft);
  EXPECT_EQ(KindFromMmsi(993672001u), TargetKind::kAidToNavigation);
  EXPECT_EQ(KindFromMmsi(2573104u), TargetKind::kBaseStation);
  EXPECT_EQ(KindFromMmsi(982570001u), TargetKind::kVessel);  // auxiliary craft
  EXPECT_EQ(KindFromMmsi(367123450u), TargetKind::kVessel);
}

TEST(AisVesselTable, KindFromMessageTypeAndBeaconStaysBeacon) {
  VesselTable t;
  FeedLines(&t, {"!AIVDM,1,1,,A,402M3<1vbeAAW0i>SNRA<mgP2@GV,0*29",
                 "!AIVDM,1,1,,A,91b6UbBIAkPir@HR5AWGg:P248I?,0*5C"},
            0);
  ASSERT_NE(t.Find(2573104u), nullptr);
  EXPECT_EQ(t.Find(2573104u)->kind, TargetKind::kBaseStation);
  ASSERT_NE(t.Find(111257001u), nullptr);
  EXPECT_EQ(t.Find(111257001u)->kind, TargetKind::kSarAircraft);
  EXPECT_TRUE(t.Find(111257001u)->has_altitude);

  // An AtoN on an ordinary vessel MMSI is still an AtoN once a type 21 is heard.
  AisMessage aton = Position(366999001u, 32.7, -79.8, 0, 0, 21);
  aton.has_sog = aton.has_cog = aton.has_heading = false;
  aton.kind = StationKind::kAidToNavigation;
  aton.name = "CHARLESTON LT 2";
  aton.virtual_aid = true;
  t.Update(aton, 1);
  EXPECT_EQ(t.Find(366999001u)->kind, TargetKind::kAidToNavigation);
  EXPECT_TRUE(t.Find(366999001u)->virtual_aid);
  EXPECT_TRUE(t.Find(366999001u)->trail.empty());

  t.Update(Position(970123456u, 32.7, -79.8, 0.5, 45), 2);
  EXPECT_EQ(t.Find(970123456u)->kind, TargetKind::kSart);
}

TEST(AisVesselTable, PositionReportReplacesMotionIncludingUnavailable) {
  VesselTable t;
  t.Update(Position(1u << 20, 10, 10, 6.0, 120), 0);
  AisMessage m = Position(1u << 20, 10.001, 10, 0, 0);
  m.has_sog = m.has_cog = m.has_heading = false;
  t.Update(m, 2);
  const Vessel* v = t.Find(1u << 20);
  EXPECT_FALSE(v->has_sog);
  EXPECT_FALSE(v->has_cog);
  EXPECT_FALSE(v->has_heading);
  EXPECT_DOUBLE_EQ(v->lat_deg, 10.001);
}

TEST(AisVesselTable, LongRangeReportDoesNotReplaceRecentPosition) {
  VesselTableOptions o;
  o.stale_after_s = 300;
  VesselTable t(o);
  t.Update(Position(366000001u, 32.0, -79.0), 0);
  t.Update(Position(366000001u, 32.1, -79.1, 5, 90, 27), 100);
  EXPECT_DOUBLE_EQ(t.Find(366000001u)->lat_deg, 32.0);
  EXPECT_DOUBLE_EQ(t.Find(366000001u)->last_heard_s, 100);
  t.Update(Position(366000001u, 32.2, -79.2, 5, 90, 27), 400);
  EXPECT_DOUBLE_EQ(t.Find(366000001u)->lat_deg, 32.2);
  EXPECT_EQ(t.Find(366000001u)->position_type, 27);
}

TEST(AisVesselTable, VoyageDataReplacedWholeByType5) {
  VesselTable t;
  AisMessage s;
  s.type = 5;
  s.mmsi = 366000002u;
  s.name = "TUG ONE";
  s.destination = "CHARLESTON";
  s.has_draught = true;
  s.draught_m = 4.2;
  t.Update(s, 0);
  s.destination.clear();
  s.has_draught = false;
  s.name.clear();
  t.Update(s, 1);
  const Vessel* v = t.Find(366000002u);
  EXPECT_EQ(v->name, "TUG ONE");
  EXPECT_EQ(v->destination, "");
  EXPECT_FALSE(v->has_draught);
}

TEST(AisVesselTable, TrailSpacingCountAndAge) {
  VesselTableOptions o;
  o.trail_max_points = 3;
  o.trail_min_interval_s = 10;
  o.trail_max_age_s = 100;
  o.drop_after_s = 0;
  VesselTable t(o);
  const std::uint32_t id = 366000003u;
  t.Update(Position(id, 0, 0.000), 0);
  t.Update(Position(id, 0, 0.001), 5);   // too soon
  t.Update(Position(id, 0, 0.002), 10);
  t.Update(Position(id, 0, 0.002), 20);  // did not move
  t.Update(Position(id, 0, 0.003), 30);
  t.Update(Position(id, 0, 0.004), 40);
  const Vessel* v = t.Find(id);
  ASSERT_EQ(v->trail.size(), 3u);
  EXPECT_DOUBLE_EQ(v->trail.front().time_s, 10);
  EXPECT_DOUBLE_EQ(v->trail.back().lon_deg, 0.004);

  t.Expire(135);
  EXPECT_EQ(v->trail.size(), 1u);
  EXPECT_DOUBLE_EQ(v->trail.front().time_s, 40);
}

TEST(AisVesselTable, StaleAndDrop) {
  VesselTableOptions o;
  o.stale_after_s = 60;
  o.drop_after_s = 600;
  VesselTable t(o);
  std::vector<std::pair<VesselChange, std::uint32_t>> log;
  t.SetListener([&](VesselChange c, const Vessel& v) { log.emplace_back(c, v.mmsi); });
  t.Update(Position(366000004u, 0, 0), 0);
  t.Update(Position(366000005u, 0, 0), 500);
  EXPECT_FALSE(t.IsStale(*t.Find(366000004u), 59));
  EXPECT_TRUE(t.IsStale(*t.Find(366000004u), 60));
  t.Expire(600);
  EXPECT_EQ(t.Find(366000004u), nullptr);
  EXPECT_NE(t.Find(366000005u), nullptr);
  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0], std::make_pair(VesselChange::kAdded, 366000004u));
  EXPECT_EQ(log[1], std::make_pair(VesselChange::kAdded, 366000005u));
  EXPECT_EQ(log[2], std::make_pair(VesselChange::kRemoved, 366000004u));
}

TEST(AisVesselTable, RevisionsAndListener) {
  VesselTable t;
  std::vector<VesselChange> changes;
  t.SetListener([&](VesselChange c, const Vessel&) { changes.push_back(c); });
  EXPECT_EQ(t.revision(), 0u);
  t.Update(Position(366000006u, 0, 0), 0);
  t.Update(Position(366000006u, 0, 0.01), 1);
  EXPECT_EQ(t.revision(), 2u);
  EXPECT_EQ(t.Find(366000006u)->revision, 2u);
  EXPECT_EQ(changes, (std::vector<VesselChange>{VesselChange::kAdded, VesselChange::kUpdated}));
  AisMessage none;
  EXPECT_EQ(t.Update(none, 2), nullptr);
  EXPECT_EQ(t.revision(), 2u);
}

TEST(AisVesselTable, EvictsOldestHeardButKeepsOwnShip) {
  VesselTableOptions o;
  o.max_vessels = 3;
  VesselTable t(o);
  AisMessage own = Position(366000010u, 0, 0);
  own.own_ship = true;
  t.Update(own, 0);
  t.Update(Position(366000011u, 0, 0), 1);
  t.Update(Position(366000012u, 0, 0), 2);
  t.Update(Position(366000011u, 0, 0), 3);
  t.Update(Position(366000013u, 0, 0), 4);
  EXPECT_EQ(t.size(), 3u);
  EXPECT_NE(t.Find(366000010u), nullptr);
  EXPECT_EQ(t.Find(366000012u), nullptr);
  EXPECT_EQ(t.own_ship_mmsi(), 366000010u);
  EXPECT_TRUE(t.Find(366000010u)->own_ship);
}

TEST(AisVesselTable, TagAttribution) {
  VesselTable t;
  FeedLines(&t, {"\\s:2573010,c:1787234980*03\\!BSVDM,1,1,,B,13noH:00000H@P@RSPEakGK@0D33,0*43"}, 0);
  const Vessel* v = t.Find(258857000u);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(v->station, "2573010");
}

PositionFix Own(double lat, double lon, double speed = 0, double heading = 0) {
  PositionFix f;
  f.SetPosition(lat, lon);
  f.has_speed = true;
  f.speed_mps = speed;
  f.has_true_heading = true;
  f.true_heading_deg = heading;
  return f;
}

Vessel Target(double lat, double lon, double sog, double cog, double time_s = 0) {
  Vessel v;
  v.has_position = true;
  v.lat_deg = lat;
  v.lon_deg = lon;
  v.position_time_s = time_s;
  v.has_sog = true;
  v.sog_mps = sog;
  v.has_cog = true;
  v.cog_deg = cog;
  return v;
}

TEST(AisCpa, HeadOn) {
  CpaResult r = ComputeCpa(Own(0, 0, 5, 0), Target(1852 / kMetresPerDeg, 0, 5, 180), 0);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.range_m, 1852, 0.01);
  EXPECT_NEAR(r.bearing_deg, 0, 1e-9);
  EXPECT_NEAR(r.tcpa_s, 185.2, 1e-6);
  EXPECT_NEAR(r.cpa_m, 0, 1e-6);
}

TEST(AisCpa, CrossingAhead) {
  // Target 1000 m south, 500 m east, northbound at 5 m/s; ownship stopped.
  CpaResult r = ComputeCpa(Own(0, 0), Target(-1000 / kMetresPerDeg, 500 / kMetresPerDeg, 5, 0), 0);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.range_m, 1118.034, 0.01);
  EXPECT_NEAR(r.bearing_deg, 153.435, 0.001);
  EXPECT_NEAR(r.tcpa_s, 200, 0.01);
  EXPECT_NEAR(r.cpa_m, 500, 0.01);
}

TEST(AisCpa, DeadReckonsTargetAndReportsPassedApproach) {
  // Reported 100 s ago 1000 m south; now 500 m north of ownship and opening.
  CpaResult r = ComputeCpa(Own(0, 0), Target(-1000 / kMetresPerDeg, 0, 15, 0, 0), 100);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.range_m, 500, 0.01);
  EXPECT_NEAR(r.tcpa_s, -100.0 / 3.0, 1e-3);
  EXPECT_NEAR(r.cpa_m, 0, 0.01);
}

TEST(AisCpa, UnknownMotionAndFixedStations) {
  Vessel v = Target(0, 1000 / kMetresPerDeg, 0, 0);
  v.has_sog = false;
  CpaResult r = ComputeCpa(Own(0, 0, 5, 90), v, 0);
  EXPECT_FALSE(r.valid);
  EXPECT_NEAR(r.range_m, 1000, 0.01);
  EXPECT_NEAR(r.bearing_deg, 90, 1e-9);

  v.kind = TargetKind::kAidToNavigation;
  r = ComputeCpa(Own(0, 0, 5, 90), v, 0);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.tcpa_s, 200, 0.01);
  EXPECT_NEAR(r.cpa_m, 0, 0.01);

  EXPECT_FALSE(ComputeCpa(PositionFix{}, v, 0).valid);
}

TEST(AisCpa, AcrossAntimeridian) {
  CpaResult r = ComputeCpa(Own(0, 179.999), Target(0, -179.999, 0, 0), 0);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.range_m, 0.002 * kMetresPerDeg, 0.01);
  EXPECT_NEAR(r.bearing_deg, 90, 1e-6);
}

}  // namespace
}  // namespace ais
}  // namespace fv
