// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the window's ownship state (`Desk::ownship`, `DeskHost::OwnshipPanel`).

#include <gtest/gtest.h>

#include <fstream>

#include "fv_desk_fake.h"
#include "fv_desk_host.h"
#include "fv_desk_ownship.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/settings.h"
#include "scratch.h"

using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::OwnshipMode;
using fv::OwnshipState;
using fv::desk::test::Scratch;

namespace {

const char kNew[] = "overlay.new.fv.movingmap";

fv::PositionFix FixAt(double lat, double lon) {
  fv::PositionFix fix;
  fix.SetPosition(lat, lon);
  return fix;
}

class OwnshipTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fd_.settings().Set("movingmap.snap_to_road", "false");
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{60.0, 10.0}, 50000, 0));
  }
  fv::MovingMapOverlay* Overlay() {
    const fv::desk::MovingMapDriver* d = fd_.desk().ownship_map();
    return d != nullptr ? d->overlay() : nullptr;
  }
  const OwnshipState& Ownship() { return fd_.desk().ownship(); }
  /// Pushes `fix` and ticks until the overlay has taken it.
  void Feed(const fv::PositionFix& fix) {
    Overlay()->PushFix(fix);
    fd_.desk().TickMovingMap(0.1);
  }
  FakeDesk fd_;
};

TEST_F(OwnshipTest, NoMovingMapMeansNoOwnship) {
  EXPECT_EQ(Ownship().mode, OwnshipMode::kNone);
  EXPECT_FALSE(Ownship().has_position());
}

TEST_F(OwnshipTest, AFixSetsPositionAltitudeSpeedAndTime) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  EXPECT_EQ(Ownship().mode, OwnshipMode::kLive);
  EXPECT_EQ(Ownship().source_name, Overlay()->Name());
  EXPECT_EQ(Ownship().source.lock().get(), Overlay());
  fd_.desk().TickMovingMap(0.1);  // the demo's first fix

  fv::PositionFix fix = FixAt(60.1, 10.1);
  fix.altitude_msl_m = 152.5;
  fix.has_altitude = true;
  fix.speed_mps = 12.0;
  fix.has_speed = true;
  fix.time_s = 1.8e9;
  fix.has_time = true;
  const uint64_t before = Ownship().generation;
  Feed(fix);
  EXPECT_GT(Ownship().generation, before);
  ASSERT_TRUE(Ownship().has_position());
  EXPECT_DOUBLE_EQ(Ownship().fix.lat, 60.1);
  EXPECT_DOUBLE_EQ(Ownship().fix.lon, 10.1);
  EXPECT_DOUBLE_EQ(Ownship().fix.altitude_msl_m, 152.5);
  EXPECT_DOUBLE_EQ(Ownship().fix.speed_mps, 12.0);
  EXPECT_DOUBLE_EQ(Ownship().fix.time_s, 1.8e9);
  EXPECT_TRUE(Ownship().time_from_fix);
}

TEST_F(OwnshipTest, AFixWithoutTimeIsStampedOnArrival) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  const double before = fv::desk::UtcNowSeconds();
  Feed(FixAt(60.2, 10.2));
  EXPECT_FALSE(Ownship().time_from_fix);
  EXPECT_TRUE(Ownship().fix.has_time);
  EXPECT_GE(Ownship().fix.time_s, before);
  EXPECT_LE(Ownship().fix.time_s, fv::desk::UtcNowSeconds());
}

TEST_F(OwnshipTest, ADerivedHeadingIsATrueBearing) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  fd_.desk().TickMovingMap(0.1);
  // Due north-east on the ground at 60 N: one minute of latitude, two of longitude.
  Feed(FixAt(60.0, 10.0));
  Feed(FixAt(60.0 + 1.0 / 60, 10.0 + 2.0 / 60));
  ASSERT_TRUE(Ownship().heading.known);
  EXPECT_FALSE(Ownship().heading.reported);
  EXPECT_NEAR(Ownship().heading.degrees, 45.0, 1.0);

  fv::PositionFix reported = FixAt(60.05, 10.1);
  reported.true_heading_deg = 200.0;
  reported.has_true_heading = true;
  Feed(reported);
  EXPECT_TRUE(Ownship().heading.reported);
  EXPECT_DOUBLE_EQ(Ownship().heading.degrees, 200.0);
}

TEST_F(OwnshipTest, ATrackIsAReplayInRecordedTime) {
  Scratch dir;
  const std::string path = dir.Path("ride.gpx");
  {
    std::ofstream f(path);
    f << "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"test\"><trk><trkseg>\n"
         "<trkpt lat=\"60\" lon=\"10\"><ele>40</ele><time>2026-11-09T12:00:00Z</time></trkpt>\n"
         "<trkpt lat=\"60\" lon=\"10.001\"><ele>41</ele><time>2026-11-09T12:00:01Z</time></trkpt>\n"
         "</trkseg></trk></gpx>\n";
  }
  ASSERT_EQ(fd_.desk().session().OpenFile(fv::app::TypeId(), path), fv::app::FlowResult::kDone);
  EXPECT_EQ(Ownship().mode, OwnshipMode::kReplay);
  fd_.desk().TickMovingMap(0.1);
  ASSERT_TRUE(Ownship().has_position());
  EXPECT_TRUE(Ownship().time_from_fix);
  // Opening lands on the last fix.
  EXPECT_DOUBLE_EQ(Ownship().fix.time_s, 1794225601.0);  // 2026-11-09T12:00:01Z
  EXPECT_DOUBLE_EQ(Ownship().fix.altitude_msl_m, 41.0);
  fd_.desk().SeekReplay(1794225600.5);
  fd_.desk().TickMovingMap(0.1);
  EXPECT_DOUBLE_EQ(Ownship().fix.altitude_msl_m, 40.5);
}

TEST_F(OwnshipTest, ClosingTheMovingMapClearsTheOwnship) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  Feed(FixAt(60.1, 10.1));
  const uint64_t before = Ownship().generation;
  ASSERT_EQ(fd_.desk().session().Close(*Overlay()), fv::app::FlowResult::kDone);
  EXPECT_EQ(Ownship().mode, OwnshipMode::kNone);
  EXPECT_FALSE(Ownship().has_position());
  EXPECT_GT(Ownship().generation, before);
}

TEST(OwnshipHost, TickReportsTheChangeAndThePanelCarriesIt) {
  fv::desk::DeskHost* h = fv::desk::DeskHost::Create();
  h->Resize(400, 300, 1.0, 0.25);
  (void)h->Tick();
  h->WaitForRender();
  EXPECT_EQ(h->OwnshipPanel().mode, 0);
  EXPECT_EQ(h->OwnshipPanel().status, "No moving map");

  ASSERT_EQ(h->Execute("view.ownship"), "");
  EXPECT_TRUE(h->Tick().ownship_requested);
  EXPECT_TRUE(h->TakeOwnshipRequest());
  EXPECT_FALSE(h->TakeOwnshipRequest());

  ASSERT_EQ(h->Execute(kNew), "");
  bool changed = false;
  for (int i = 0; i < 5 && h->OwnshipPanel().position == "\xE2\x80\x94"; ++i) {
    h->WaitForRender();
    changed |= h->Tick().ownship_changed;
  }
  EXPECT_TRUE(changed);
  const fv::desk::HostOwnshipPanel o = h->OwnshipPanel();
  EXPECT_EQ(o.mode, 1);
  EXPECT_EQ(o.status, "Live");
  EXPECT_NE(o.time, "\xE2\x80\x94");
  EXPECT_FALSE(o.source.empty());
  EXPECT_FALSE(o.clock_enabled);
  EXPECT_EQ(h->OwnshipRateCount(), 5);
  EXPECT_EQ(h->OwnshipRateLabel(4), "60\xC3\x97");
  h->SetOwnshipRate(2);
  EXPECT_EQ(h->OwnshipPanel().rate_label, "5\xC3\x97");
  fv_desk_host_release(h);
}

}  // namespace
