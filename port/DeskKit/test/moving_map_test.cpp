// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the desktop moving map: Desk wiring, feeds, follow modes and the
/// options page.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "fv_desk_fake.h"
#include "fv_desk_moving_map.h"
#include "fvkit/nav/position.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/settings.h"
#include "scratch.h"

using fv::GeoPoint;
using fv::desk::FakeDesk;
using fv::desk::MovingMapFeed;
using fv::desk::test::Scratch;

namespace {

const char kNew[] = "overlay.new.fv.movingmap";

/// 2026-11-09T12:00:00Z, the first fix of a `WriteTrack` track with no offset.
constexpr double kNoon = 1794225600.0;

/// A three-point GPX track one second apart, heading east from `start`,
/// starting `offset_s` (0-57) seconds after `kNoon`.
std::string WriteTrack(const Scratch& dir, const std::string& name, const GeoPoint& start,
                       int offset_s = 0) {
  const std::string path = dir.Path(name);
  std::ofstream f(path);
  f << "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"test\">\n<trk><trkseg>\n";
  for (int i = 0; i < 3; ++i) {
    const int sec = offset_s + i;
    f << "<trkpt lat=\"" << start.lat << "\" lon=\"" << start.lon + 0.001 * i
      << "\"><time>2026-11-09T12:00:" << (sec < 10 ? "0" : "") << sec << "Z</time></trkpt>\n";
  }
  f << "</trkseg></trk>\n</gpx>\n";
  return path;
}

fv::PositionFix FixAt(const GeoPoint& p) {
  fv::PositionFix fix;
  fix.SetPosition(p.lat, p.lon);
  return fix;
}

/// The FakeDesk with the view at 1:50,000 over Charleston, jumps instead of slews.
class MovingMapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fd_.settings().Set("movingmap.slew_seconds", "0");
    fd_.settings().Set("movingmap.snap_to_road", "false");
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.8, -79.9}, 50000, 0));
  }
  /// The topmost moving map.
  fv::desk::DeskMovingMap* Top() {
    const fv::desk::MovingMapDriver* d = fd_.desk().ownship_map();
    return d != nullptr ? d->overlay() : nullptr;
  }
  std::shared_ptr<fv::Overlay> TopShared() {
    return fd_.desk().overlays().FirstOfType(fv::MovingMapOverlay::kTypeId);
  }
  /// True when `g` lies inside the view's surface.
  bool OnScreen(const GeoPoint& g) {
    const fv::view::Viewport& v = fd_.desk().map_view().View();
    fv::view::PointF p;
    return v.PointFor(g, &p) && p.x >= 0 && p.y >= 0 && p.x < v.WidthPt() && p.y < v.HeightPt();
  }
  void Ticks(int n) {
    for (int i = 0; i < n; ++i) fd_.desk().TickMovingMap(0.1);
  }
  fv::app::FlowResult Open(const std::string& path) {
    return fd_.desk().session().OpenFile(fv::app::TypeId(), path);
  }
  FakeDesk fd_;
};

TEST_F(MovingMapTest, ANewMovingMapListensToTheDefaultFeedAndTheMapFollows) {
  fd_.settings().Set("movingmap.continuous", "true");
  fv::desk::Desk& desk = fd_.desk();
  EXPECT_EQ(desk.moving_map_count(), 0u);
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  ASSERT_EQ(desk.moving_map_count(), 1u);
  ASSERT_NE(Top(), nullptr);
  EXPECT_FALSE(Top()->replay());
  EXPECT_EQ(desk.ownship_map()->feed(), MovingMapFeed::kDemo);
  EXPECT_TRUE(Top()->running());

  Ticks(1);
  const GeoPoint far{32.9, -79.7};
  Top()->PushFix(FixAt(far));
  Ticks(3);
  // The camera leads the ship along its course, so it is on screen, not centred.
  EXPECT_TRUE(OnScreen(far));
  EXPECT_GT(desk.map_view().View().Center().lat, 32.85);
  EXPECT_NE(desk.CurrentStatus().message.find("Feed demo"), std::string::npos);
}

TEST_F(MovingMapTest, ItIsAFileTypeNotAToggle) {
  fv::desk::Desk& desk = fd_.desk();
  EXPECT_TRUE(desk.types().IsFile(fv::MovingMapOverlay::kTypeId));
  EXPECT_EQ(desk.commands().Find("overlay.toggle.fv.movingmap"), nullptr);
  EXPECT_NE(desk.commands().Find(kNew), nullptr);
  const fv::app::OverlayTypeDesc* by_ext = desk.types().FindByExtension("gpx");
  ASSERT_NE(by_ext, nullptr);
  EXPECT_EQ(by_ext->id, fv::MovingMapOverlay::kTypeId);
}

TEST_F(MovingMapTest, NotCentringOnEveryFixLeavesTheMapWhereItIs) {
  fd_.settings().Set("movingmap.continuous", "false");
  fd_.settings().Set("movingmap.auto_rotate", "true");
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  EXPECT_FALSE(Top()->modes().auto_center);
  EXPECT_FALSE(Top()->modes().auto_rotate);
  const GeoPoint before = desk.map_view().View().Center();
  Top()->PushFix(FixAt(GeoPoint{32.9, -79.7}));
  Ticks(3);
  EXPECT_TRUE(Top()->has_fix());
  EXPECT_DOUBLE_EQ(desk.map_view().View().Center().lat, before.lat);
  EXPECT_DOUBLE_EQ(desk.map_view().View().Center().lon, before.lon);
  EXPECT_DOUBLE_EQ(desk.map_view().View().Rotation(), 0.0);
}

TEST_F(MovingMapTest, TrackUpTurnsTheChartToTheCourse) {
  fd_.settings().Set("movingmap.continuous", "true");
  fd_.settings().Set("movingmap.auto_rotate", "true");
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  EXPECT_TRUE(Top()->modes().auto_rotate);
  fv::PositionFix fix = FixAt(GeoPoint{32.8, -79.9});
  fix.true_heading_deg = 90.0;
  fix.has_true_heading = true;
  fix.speed_mps = 10.0;
  fix.has_speed = true;
  Top()->PushFix(fix);
  Ticks(3);
  // Course 090 up the screen: the chart turns 270 clockwise.
  EXPECT_NEAR(fd_.desk().map_view().View().Rotation(), 270.0, 1.0);
}

TEST_F(MovingMapTest, OpeningAGpxShowsTheWholeRide) {
  fd_.settings().Set("movingmap.continuous", "true");
  Scratch dir;
  const GeoPoint start{32.85, -79.8};
  const GeoPoint end{32.85, -79.798};
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", start)), fv::app::FlowResult::kDone);
  ASSERT_EQ(desk.moving_map_count(), 1u);
  EXPECT_TRUE(Top()->replay());
  EXPECT_EQ(Top()->Name(), "ride");
  EXPECT_EQ(Top()->track().size(), 3u);
  EXPECT_EQ(desk.ownship_map()->feed(), MovingMapFeed::kReplay);
  EXPECT_FALSE(Top()->is_dirty());
  EXPECT_TRUE(fd_.shell().errors.empty());
  // The clock stops at the last fix.
  EXPECT_DOUBLE_EQ(desk.replay_clock().time_s, kNoon + 2);
  EXPECT_FALSE(desk.replay_clock().playing);
  Ticks(3);
  ASSERT_TRUE(Top()->has_fix());
  EXPECT_NEAR(Top()->last_fix().lon, end.lon, 1e-9);
  EXPECT_TRUE(OnScreen(end));
  EXPECT_EQ(desk.ownship().mode, fv::OwnshipMode::kReplay);
  EXPECT_DOUBLE_EQ(desk.ownship().fix.time_s, kNoon + 2);
  EXPECT_NE(desk.CurrentStatus().message.find("ride.gpx"), std::string::npos);
}

TEST_F(MovingMapTest, SeekingMovesTheShipForwardAndBack) {
  fd_.settings().Set("movingmap.continuous", "true");
  Scratch dir;
  const GeoPoint start{32.85, -79.8};
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", start)), fv::app::FlowResult::kDone);
  Ticks(1);

  desk.SeekReplay(kNoon + 0.5);
  Ticks(1);
  EXPECT_NEAR(Top()->last_fix().lon, start.lon + 0.0005, 1e-9);
  EXPECT_NEAR(Top()->heading().degrees, 90.0, 0.1);
  EXPECT_NEAR(desk.ownship().fix.lon, start.lon + 0.0005, 1e-9);
  EXPECT_DOUBLE_EQ(desk.ownship().fix.time_s, kNoon + 0.5);

  desk.SeekReplay(kNoon - 100);  // clamped to the start
  Ticks(1);
  EXPECT_DOUBLE_EQ(desk.replay_clock().time_s, kNoon);
  EXPECT_NEAR(Top()->last_fix().lon, start.lon, 1e-9);
  EXPECT_TRUE(OnScreen(start));
}

TEST_F(MovingMapTest, PlayingAdvancesAtTheRateAndStopsAtTheEnd) {
  Scratch dir;
  const GeoPoint start{32.85, -79.8};
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", start)), fv::app::FlowResult::kDone);
  // Play from the end starts again from the start.
  desk.SetReplayRate(5.0);
  desk.SetReplayPlaying(true);
  EXPECT_DOUBLE_EQ(desk.replay_clock().time_s, kNoon);
  desk.TickMovingMap(0.1);
  EXPECT_NEAR(desk.replay_clock().time_s, kNoon + 0.5, 1e-6);
  EXPECT_NEAR(Top()->last_fix().lon, start.lon + 0.0005, 1e-9);
  Ticks(10);
  EXPECT_DOUBLE_EQ(desk.replay_clock().time_s, kNoon + 2);
  EXPECT_FALSE(desk.replay_clock().playing);
  desk.SetReplayRate(0.0);  // ignored
  EXPECT_DOUBLE_EQ(desk.replay_clock().rate, 5.0);
}

TEST_F(MovingMapTest, TheSliderSpansEveryOpenTrack) {
  Scratch dir;
  fv::desk::Desk& desk = fd_.desk();
  double start = 0.0;
  double end = 0.0;
  EXPECT_FALSE(desk.ReplayRange(&start, &end));
  ASSERT_EQ(Open(WriteTrack(dir, "a.gpx", GeoPoint{32.80, -79.9})), fv::app::FlowResult::kDone);
  const std::shared_ptr<fv::Overlay> a = TopShared();
  ASSERT_EQ(Open(WriteTrack(dir, "b.gpx", GeoPoint{32.81, -79.9}, 10)),
            fv::app::FlowResult::kDone);
  ASSERT_TRUE(desk.ReplayRange(&start, &end));
  EXPECT_DOUBLE_EQ(start, kNoon);
  EXPECT_DOUBLE_EQ(end, kNoon + 12);
  // The second file moved the clock to its own end; the first is past its end.
  EXPECT_DOUBLE_EQ(desk.replay_clock().time_s, kNoon + 12);
  Ticks(1);
  auto* ma = static_cast<fv::desk::DeskMovingMap*>(a.get());
  EXPECT_EQ(ma->phase(), fv::ReplayPhase::kAfter);

  // Between the two: the second waits at its first fix, dimmed.
  desk.SeekReplay(kNoon + 5);
  Ticks(1);
  EXPECT_EQ(ma->phase(), fv::ReplayPhase::kAfter);
  EXPECT_EQ(Top()->phase(), fv::ReplayPhase::kBefore);
  EXPECT_LT(Top()->draw_opacity(), 255);
  EXPECT_EQ(Top()->color().a, 255);  // the saved colour is never the dimmed one
  EXPECT_NEAR(Top()->last_fix().lon, -79.9, 1e-9);
  desk.SeekReplay(kNoon + 11);
  Ticks(1);
  EXPECT_EQ(Top()->phase(), fv::ReplayPhase::kDuring);
  EXPECT_EQ(Top()->draw_opacity(), 255);

  ASSERT_EQ(desk.session().Close(*a), fv::app::FlowResult::kDone);
  ASSERT_TRUE(desk.ReplayRange(&start, &end));
  EXPECT_DOUBLE_EQ(start, kNoon + 10);
}

TEST_F(MovingMapTest, ALiveMapIgnoresTheClock) {
  Scratch dir;
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", GeoPoint{32.85, -79.8})),
            fv::app::FlowResult::kDone);
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  fv::desk::DeskMovingMap* live = Top();
  ASSERT_FALSE(live->replay());
  Ticks(1);
  const GeoPoint here{32.9, -79.7};
  live->PushFix(FixAt(here));
  Ticks(1);
  desk.SeekReplay(kNoon);
  desk.SetReplayPlaying(true);
  Ticks(3);
  EXPECT_NEAR(live->last_fix().lat, here.lat, 1e-9);
  EXPECT_EQ(desk.ownship().mode, fv::OwnshipMode::kLive);
  EXPECT_TRUE(live->running());
}

TEST_F(MovingMapTest, TheOwnshipPanelMovesOnASeekWithALiveOwnship) {
  Scratch dir;
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", GeoPoint{32.85, -79.8})),
            fv::app::FlowResult::kDone);
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  Ticks(1);
  // A live map on top of a GPX: the ownship is live, the time row scrubs the GPX.
  fv::desk::OwnshipPanel p = desk.ownship_panel();
  EXPECT_EQ(p.mode, fv::OwnshipMode::kLive);
  EXPECT_EQ(p.status, "Live");
  EXPECT_TRUE(p.clock_enabled);
  EXPECT_DOUBLE_EQ(p.start_s, kNoon);
  EXPECT_DOUBLE_EQ(p.end_s, kNoon + 2);
  EXPECT_DOUBLE_EQ(p.clock_s, kNoon + 2);
  EXPECT_EQ(p.clock_text, "12:00:02");

  uint64_t gen = desk.OwnshipPanelGeneration();
  desk.SeekReplay(kNoon + 1);
  EXPECT_GT(desk.OwnshipPanelGeneration(), gen);
  EXPECT_EQ(desk.ownship_panel().clock_text, "12:00:01");
  gen = desk.OwnshipPanelGeneration();
  desk.SetReplayPlaying(true);
  EXPECT_GT(desk.OwnshipPanelGeneration(), gen);
  EXPECT_TRUE(desk.ownship_panel().playing);
  gen = desk.OwnshipPanelGeneration();
  Ticks(1);
  EXPECT_GT(desk.OwnshipPanelGeneration(), gen);
  gen = desk.OwnshipPanelGeneration();
  desk.SetReplayRate(10.0);
  EXPECT_GT(desk.OwnshipPanelGeneration(), gen);
  EXPECT_EQ(desk.ownship_panel().rate_index, 3);
  EXPECT_EQ(fd_.settings().GetString("ownship.playback_rate", ""), "10");
}

TEST_F(MovingMapTest, TheTimeRowIsDisabledWithNoReplayMap) {
  fv::desk::Desk& desk = fd_.desk();
  fv::desk::OwnshipPanel p = desk.ownship_panel();
  EXPECT_EQ(p.status, "No moving map");
  EXPECT_FALSE(p.clock_enabled);
  EXPECT_EQ(p.position, "\xE2\x80\x94");
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  Ticks(1);
  p = desk.ownship_panel();
  EXPECT_FALSE(p.clock_enabled);
  EXPECT_EQ(p.clock_text, "");
  desk.SetReplayPlaying(true);
  EXPECT_FALSE(desk.replay_clock().playing);
}

TEST_F(MovingMapTest, OpeningAndClosingAGpxChangesTheSliderRange) {
  Scratch dir;
  fv::desk::Desk& desk = fd_.desk();
  uint64_t gen = desk.OwnshipPanelGeneration();
  ASSERT_EQ(Open(WriteTrack(dir, "a.gpx", GeoPoint{32.80, -79.9})), fv::app::FlowResult::kDone);
  EXPECT_GT(desk.OwnshipPanelGeneration(), gen);
  const std::shared_ptr<fv::Overlay> a = TopShared();
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  gen = desk.OwnshipPanelGeneration();
  ASSERT_EQ(desk.session().Close(*a), fv::app::FlowResult::kDone);
  EXPECT_GT(desk.OwnshipPanelGeneration(), gen);
  EXPECT_FALSE(desk.ownship_panel().clock_enabled);
}

TEST(OwnshipPanelTest, FormatsTheFixInTheChosenUnits) {
  fv::OwnshipState s;
  s.mode = fv::OwnshipMode::kReplay;
  s.source_name = "ride";
  s.fix.SetPosition(32.5, -79.75);
  s.fix.altitude_msl_m = 304.8;
  s.fix.has_altitude = true;
  s.fix.speed_mps = 1852.0 / 3600.0 * 12.0;
  s.fix.has_speed = true;
  s.fix.time_s = kNoon + 3;
  s.fix.has_time = true;
  s.heading.known = true;
  s.heading.degrees = 359.7;
  s.heading.reported = false;
  fv::ReplayClock clock;
  clock.rate = 3.0;
  fv::desk::OwnshipPanel p =
      fv::desk::BuildOwnshipPanel(s, clock, false, 0, 0, fv::desk::OwnshipUnits::kNautical);
  EXPECT_EQ(p.status, "Replay");
  EXPECT_EQ(p.source, "ride");
  EXPECT_EQ(p.time, "2026-11-09 12:00:03 UTC");
  EXPECT_EQ(p.position, "32\xC2\xB0" "30.000' N  079\xC2\xB0" "45.000' W");
  EXPECT_EQ(p.altitude, "1,000 ft");
  EXPECT_EQ(p.speed, "12.0 kt");
  EXPECT_EQ(p.heading, "000\xC2\xB0 T");
  EXPECT_TRUE(p.heading_derived);
  EXPECT_EQ(p.rate_index, -1);
  p = fv::desk::BuildOwnshipPanel(s, clock, false, 0, 0, fv::desk::OwnshipUnits::kMetric);
  EXPECT_EQ(p.altitude, "305 m");
  EXPECT_EQ(p.speed, "22.2 km/h");
  EXPECT_EQ(fv::desk::PlaybackRateLabel(10.0), "10\xC3\x97");
  EXPECT_EQ(fv::desk::PlaybackRateLabel(0.5), "0.5\xC3\x97");
}

TEST_F(MovingMapTest, AnUnreadableTrackIsReported) {
  EXPECT_NE(Open("/nonexistent/ride.gpx"), fv::app::FlowResult::kDone);
  EXPECT_EQ(fd_.desk().moving_map_count(), 0u);
  EXPECT_FALSE(fd_.shell().errors.empty());
}

TEST_F(MovingMapTest, TheTopmostMapOwnsTheCamera) {
  fd_.settings().Set("movingmap.continuous", "true");
  Scratch dir;
  fv::desk::Desk& desk = fd_.desk();
  ASSERT_EQ(Open(WriteTrack(dir, "a.gpx", GeoPoint{32.8, -79.9})), fv::app::FlowResult::kDone);
  const std::shared_ptr<fv::Overlay> a = TopShared();
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  ASSERT_EQ(desk.moving_map_count(), 2u);
  auto* b = Top();
  ASSERT_NE(b, nullptr);
  EXPECT_NE(static_cast<fv::Overlay*>(b), a.get());
  EXPECT_EQ(desk.ownship().source.lock().get(), static_cast<fv::Overlay*>(b));
  Ticks(2);

  // A fix on the lower map draws its ship but leaves the view alone.
  const GeoPoint before = desk.map_view().View().Center();
  static_cast<fv::MovingMapOverlay*>(a.get())->PushFix(FixAt(GeoPoint{33.5, -79.0}));
  Ticks(3);
  EXPECT_DOUBLE_EQ(desk.map_view().View().Center().lat, before.lat);

  // Raised, it takes the ownship and the camera.
  ASSERT_TRUE(desk.overlays().MoveToTop(a).ok());
  Ticks(1);
  EXPECT_EQ(desk.ownship().source.lock(), a);
  const GeoPoint far{33.6, -79.0};
  static_cast<fv::MovingMapOverlay*>(a.get())->PushFix(FixAt(far));
  Ticks(3);
  EXPECT_TRUE(OnScreen(far));

  ASSERT_EQ(desk.session().Close(*a), fv::app::FlowResult::kDone);
  EXPECT_EQ(desk.moving_map_count(), 1u);
  EXPECT_EQ(desk.ownship().source.lock().get(), static_cast<fv::Overlay*>(b));
}

TEST_F(MovingMapTest, RaisingAPausedReplayFollowsItOnlyWhenCentringOnEveryFix) {
  for (const bool auto_center : {true, false}) {
    SCOPED_TRACE(auto_center ? "centring on every fix" : "not centring");
    FakeDesk fd;
    fd.settings().Set("movingmap.slew_seconds", "0");
    fd.settings().Set("movingmap.snap_to_road", "false");
    fd.settings().Set("movingmap.continuous", auto_center ? "true" : "false");
    fv::desk::Desk& desk = fd.desk();
    auto& mv = desk.map_view();
    mv.SetViewport(mv.View().WithCamera(GeoPoint{32.8, -79.9}, 50000, 0));
    Scratch dir;
    const GeoPoint far{33.6, -79.0};
    ASSERT_EQ(desk.session().OpenFile(fv::app::TypeId(), WriteTrack(dir, "old.gpx", far)),
              fv::app::FlowResult::kDone);
    const std::shared_ptr<fv::Overlay> old =
        desk.overlays().FirstOfType(fv::MovingMapOverlay::kTypeId);
    ASSERT_TRUE(fd.Run({kNew}).ok());
    auto* live = static_cast<fv::MovingMapOverlay*>(
        desk.overlays().FirstOfType(fv::MovingMapOverlay::kTypeId).get());
    for (int i = 0; i < 2; ++i) desk.TickMovingMap(0.1);
    const GeoPoint here{32.8, -79.9};
    live->PushFix(FixAt(here));
    for (int i = 0; i < 3; ++i) desk.TickMovingMap(0.1);
    const GeoPoint before = mv.View().Center();

    // The paused replay is raised with no new fix: it is the ownship now.
    ASSERT_TRUE(desk.overlays().MoveToTop(old).ok());
    for (int i = 0; i < 3; ++i) desk.TickMovingMap(0.1);
    EXPECT_EQ(desk.ownship().source.lock(), old);
    fv::view::PointF p;
    const bool on_screen = mv.View().PointFor(far, &p) && p.x >= 0 && p.y >= 0 &&
                           p.x < mv.View().WidthPt() && p.y < mv.View().HeightPt();
    if (auto_center) {
      EXPECT_TRUE(on_screen);
    } else {
      EXPECT_DOUBLE_EQ(mv.View().Center().lat, before.lat);
      EXPECT_DOUBLE_EQ(mv.View().Center().lon, before.lon);
    }
    // The live map below keeps taking fixes and moving its ship.
    const GeoPoint next{32.81, -79.9};
    live->PushFix(FixAt(next));
    for (int i = 0; i < 3; ++i) desk.TickMovingMap(0.1);
    EXPECT_NEAR(live->last_fix().lat, next.lat, 1e-9);
  }
}

TEST_F(MovingMapTest, ALiveMapRecordsItsTrailAndSavesItAsGpx) {
  Scratch dir;
  // A receiver nobody answers: the fixes below are the only ones.
  fd_.settings().Set("movingmap.feed", "nmea_tcp");
  fd_.settings().Set("movingmap.nmea_host", "127.0.0.1");
  fd_.settings().Set("movingmap.nmea_port", "1");
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  const GeoPoint points[] = {{32.80, -79.90}, {32.81, -79.89}, {32.82, -79.88}};
  for (const GeoPoint& p : points) {
    Top()->PushFix(FixAt(p));
    Ticks(1);
  }
  ASSERT_GE(Top()->recording().size(), 3u);
  EXPECT_TRUE(Top()->is_dirty());
  const std::string path = dir.Path("saved.gpx");
  ASSERT_TRUE(Top()->FileSaveAs(path, 0).ok());
  EXPECT_FALSE(Top()->is_dirty());

  std::vector<fv::PositionFix> read;
  ASSERT_TRUE(fv::ReadTrackFixes(path, &read).ok());
  ASSERT_EQ(read.size(), Top()->recording().size());
  EXPECT_NEAR(read.back().lat, points[2].lat, 1e-6);
  EXPECT_NEAR(read.back().lon, points[2].lon, 1e-6);
}

TEST_F(MovingMapTest, FeedFromSettingsNamesALiveFeed) {
  fv::Settings s;
  EXPECT_EQ(fv::desk::MovingMapFeedFromSettings(s), MovingMapFeed::kDemo);
  s.Set("movingmap.nmea_host", "192.168.1.5");
  EXPECT_EQ(fv::desk::MovingMapFeedFromSettings(s), MovingMapFeed::kNmeaTcp);
  s.Set("movingmap.feed", "nmea_udp");
  EXPECT_EQ(fv::desk::MovingMapFeedFromSettings(s), MovingMapFeed::kNmeaUdp);
}

TEST_F(MovingMapTest, ADemoMapIsNotDirty) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  Ticks(3);
  ASSERT_FALSE(Top()->recording().empty());
  EXPECT_FALSE(Top()->is_dirty());
}

TEST_F(MovingMapTest, ANewMapCopiesTheDefaultFeed) {
  fd_.settings().Set("movingmap.speed_mps", "4");
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  ASSERT_TRUE(Top()->has_feed_config());
  EXPECT_EQ(Top()->feed_config().kind, MovingMapFeed::kDemo);
  EXPECT_DOUBLE_EQ(Top()->feed_config().demo_speed_mps, 4.0);
  EXPECT_FALSE(Top()->feed_config().snap_to_road);  // the fixture turns it off
}

TEST_F(MovingMapTest, AnOptionsChangeRestartsNoFeed) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  Ticks(2);
  const std::shared_ptr<fv::IPositionSource> source = Top()->source();
  ASSERT_NE(source, nullptr);
  std::shared_ptr<fv::desk::OptionsModel> model = fd_.desk().OverlayOptions();
  fv::desk::OptionsPage* page = model->Page(fv::MovingMapOverlay::kTypeId);
  ASSERT_TRUE(page->Set("movingmap.feed", fv::app::PropertyValue::Choice(2)).ok());
  ASSERT_TRUE(page->Set("movingmap.continuous", fv::app::PropertyValue::Bool(true)).ok());
  ASSERT_TRUE(page->Set("movingmap.auto_rotate", fv::app::PropertyValue::Bool(true)).ok());
  ASSERT_TRUE(fd_.desk().ApplyOverlayOptions(*model).ok());
  EXPECT_EQ(Top()->source(), source);
  EXPECT_EQ(Top()->feed_config().kind, MovingMapFeed::kDemo);
  EXPECT_TRUE(Top()->modes().auto_rotate);  // follow settings still apply
  EXPECT_EQ(fd_.desk().ownship().source.lock().get(), Top());
}

TEST_F(MovingMapTest, ChangingOneMapsFeedRestartsOnlyThatMap) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  const std::shared_ptr<fv::Overlay> lower = TopShared();
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  const std::shared_ptr<fv::Overlay> upper = TopShared();
  ASSERT_NE(lower, upper);
  auto* lower_map = static_cast<fv::desk::DeskMovingMap*>(lower.get());
  const std::shared_ptr<fv::IPositionSource> lower_source = lower_map->source();
  Ticks(2);

  fv::desk::FeedConfig config = Top()->feed_config();
  config.kind = MovingMapFeed::kNmeaTcp;
  config.host = "127.0.0.1";
  config.port = 1;
  ASSERT_TRUE(fd_.desk().SetMovingMapFeed(*upper, config));
  EXPECT_EQ(Top()->source(), nullptr);  // the demo stopped
  EXPECT_EQ(fd_.desk().ownship_map()->feed(), MovingMapFeed::kNmeaTcp);
  ASSERT_NE(fd_.desk().ownship_map()->subscription(), nullptr);
  EXPECT_EQ(lower_map->source(), lower_source);
  EXPECT_EQ(lower_map->feed_config().kind, MovingMapFeed::kDemo);
}

TEST_F(MovingMapTest, TwoMapsOnOneEndpointOpenOneSocket) {
  fd_.settings().Set("movingmap.feed", "nmea_tcp");
  fd_.settings().Set("movingmap.nmea_host", "127.0.0.1");
  fd_.settings().Set("movingmap.nmea_port", "1");
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  EXPECT_EQ(fd_.desk().moving_map_count(), 2u);
  EXPECT_EQ(fd_.desk().feed_receivers().open_count(), 1u);
  Ticks(1);
  EXPECT_NE(fd_.desk().CurrentStatus().message.find("Feed 127.0.0.1:1"), std::string::npos);
}

TEST_F(MovingMapTest, NotListeningStartsNoFeed) {
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  fv::desk::FeedConfig config = Top()->feed_config();
  config.listening = false;
  ASSERT_TRUE(fd_.desk().SetMovingMapFeed(*TopShared(), config));
  EXPECT_EQ(Top()->source(), nullptr);
  EXPECT_NE(fd_.desk().CurrentStatus().message.find("paused"), std::string::npos);
}

TEST_F(MovingMapTest, AReplayHeadingWithNoReportedCourseReadsDerived) {
  Scratch dir;
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", GeoPoint{32.85, -79.8})),
            fv::app::FlowResult::kDone);
  Ticks(1);
  const fv::OwnshipState& own = fd_.desk().ownship();
  ASSERT_TRUE(own.heading.known);
  EXPECT_FALSE(own.heading.reported);
  EXPECT_NEAR(own.heading.degrees, 90.0, 0.5);
  EXPECT_FALSE(own.fix.has_true_heading);

  // A seek has no history to derive from, yet both headings are there at once.
  fd_.desk().SeekReplay(kNoon + 0.5);
  Ticks(1);
  ASSERT_TRUE(own.heading.known);
  EXPECT_FALSE(own.heading.reported);
  EXPECT_NEAR(own.heading.degrees, 90.0, 0.5);
  EXPECT_TRUE(Top()->heading().known);
}

TEST_F(MovingMapTest, TheOptionsPageHoldsTheDefaultFeed) {
  std::shared_ptr<fv::desk::OptionsModel> model = fd_.desk().OverlayOptions();
  fv::desk::OptionsPage* page = model->Page(fv::MovingMapOverlay::kTypeId);
  ASSERT_NE(page, nullptr);
  for (const char* key : {"movingmap.feed", "movingmap.nmea_host", "movingmap.nmea_port",
                          "ownship.playback_rate", "movingmap.auto_rotate",
                          "movingmap.slew_seconds"})
    EXPECT_NE(page->Field(key), nullptr) << key;
  EXPECT_EQ(page->Field("movingmap.track_file"), nullptr);
}

}  // namespace

TEST_F(MovingMapTest, TrailSettingsReachEveryMapAndAFullRecordingIsReported) {
  fd_.settings().Set("movingmap.feed", "nmea_tcp");
  fd_.settings().Set("movingmap.nmea_host", "127.0.0.1");
  fd_.settings().Set("movingmap.nmea_port", "1");
  fd_.settings().Set("movingmap.record_max", "2");
  ASSERT_TRUE(fd_.Run({kNew}).ok());
  EXPECT_TRUE(Top()->trail_style().dots);
  fd_.desk().SetMovingMapFlag("movingmap.show_trail_dots", false);
  EXPECT_FALSE(Top()->trail_style().dots);

  const GeoPoint points[] = {{32.80, -79.90}, {32.81, -79.89}, {32.82, -79.88}};
  for (const GeoPoint& p : points) {
    Top()->PushFix(FixAt(p));
    Ticks(1);
  }
  EXPECT_EQ(Top()->recording().size(), 2u);
  EXPECT_NE(fd_.desk().ownship_map()->StatusText().find("recording full at 2 fixes"),
            std::string::npos)
      << fd_.desk().ownship_map()->StatusText();
}

TEST_F(MovingMapTest, CentringOnEveryFixPansTheViewWithoutANewFrame) {
  fd_.settings().Set("movingmap.continuous", "true");
  Scratch dir;
  ASSERT_EQ(Open(WriteTrack(dir, "ride.gpx", GeoPoint{32.8, -79.9})), fv::app::FlowResult::kDone);
  fd_.desk().SeekReplay(kNoon);
  Ticks(2);
  const uint64_t frame = fd_.desk().map_view().FrameGeneration();
  const GeoPoint before = fd_.desk().map_view().View().Center();
  fd_.desk().SetReplayPlaying(true);
  Ticks(10);
  EXPECT_NE(fd_.desk().map_view().View().Center().lon, before.lon);
  EXPECT_EQ(fd_.desk().map_view().FrameGeneration(), frame);
}

// Diagnostic: FV_RIDE_GPX replayed with snapping over FV_RIDE_GRAPH from
// FV_RIDE_FROM (epoch s) for FV_RIDE_SECONDS; prints each tick's ship.
TEST_F(MovingMapTest, DISABLED_RideReplayTrace) {
  const char* gpx = std::getenv("FV_RIDE_GPX");
  const char* graph = std::getenv("FV_RIDE_GRAPH");
  if (gpx == nullptr || graph == nullptr) GTEST_SKIP();
  const double from = std::atof(std::getenv("FV_RIDE_FROM"));
  const double secs = std::atof(std::getenv("FV_RIDE_SECONDS"));
  fd_.settings().Set("movingmap.snap_to_road", "true");
  fd_.settings().Set("routing.graph", graph);
  fd_.desk().SettingsLoaded();
  ASSERT_EQ(Open(gpx), fv::app::FlowResult::kDone);
  fv::desk::Desk& desk = fd_.desk();
  desk.SeekReplay(from);
  Ticks(1);
  desk.SetReplayPlaying(true);
  const int n = static_cast<int>(secs * 10);
  for (int i = 0; i < n; ++i) {
    Ticks(1);
    const fv::PositionFix& f = desk.ownship().fix;
    std::printf("T %.3f %.8f %.8f %.1f | %s\n", desk.replay_clock().time_s, f.lat, f.lon,
                desk.ownship().heading.degrees, desk.ownship_map()->StatusText().c_str());
  }
}
