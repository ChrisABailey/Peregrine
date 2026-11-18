// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the moving map trail: the recording kept apart from the drawn
/// trail, dot thinning, and the past and future dots of a replay.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fv_desk_fake.h"
#include "fv_desk_moving_map.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/nav/gpx.h"
#include "fvkit/nav/position.h"
#include "fvkit/nav/track_replay.h"
#include "fvkit/proj.h"
#include "scratch.h"

using fv::GeoPoint;
using fv::PositionFix;
using fv::desk::DeskMovingMap;
using fv::desk::TrailStyle;
using fv::desk::test::Scratch;

namespace {

/// 800 x 600 over Kiawah at 1:100,000.
fv::MapProjection Proj() {
  fv::MapProjection p;
  p.SetSurfaceSize(800, 600);
  p.SetCenter({32.60, -80.08});
  p.SetScale(100000.0);
  return p;
}

/// A fix at surface pixel (x, y) of `proj`, `t` seconds after an epoch.
PositionFix FixAtPixel(const fv::MapProjection& proj, double x, double y, double t) {
  GeoPoint g;
  EXPECT_TRUE(proj.SurfaceToGeo(x, y, &g).ok());
  PositionFix f;
  f.SetPosition(g.lat, g.lon);
  f.time_s = 1794225600.0 + t;
  f.has_time = true;
  return f;
}

/// A CpuCanvas that also counts dots and remembers each one's fill alpha.
class DotCanvas : public fv::CpuCanvas {
 public:
  using CpuCanvas::CpuCanvas;
  fv::Status DrawEllipse(const fv::PixelRect& bbox, const fv::Brush* fill,
                         const fv::Pen* outline) override {
    alphas.push_back(fill != nullptr ? fill->color.a : 0);
    return CpuCanvas::DrawEllipse(bbox, fill, outline);
  }
  std::vector<int> alphas;
};

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width() * 4; ++x) {
      h ^= row[x];
      h *= 1099511628211ull;
    }
  }
  return h;
}

/// A live map with `n` fixes `step_px` apart along the middle row.
std::unique_ptr<DeskMovingMap> LiveMap(const fv::MapProjection& proj, int n, double step_px) {
  auto mm = std::make_unique<DeskMovingMap>();
  EXPECT_TRUE(mm->FileNew().ok());
  for (int i = 0; i < n; ++i) mm->AddTrailFix(FixAtPixel(proj, 100 + i * step_px, 300, i));
  return mm;
}

/// Lays `mm` onto a fresh canvas with its ship at `ship`.
std::unique_ptr<DotCanvas> Draw(DeskMovingMap& mm, const fv::MapProjection& proj,
                                const PositionFix& ship) {
  mm.PushFix(ship);
  (void)mm.Tick(proj, 0.0);
  auto canvas = std::make_unique<DotCanvas>(800, 600);
  canvas->Clear(fv::FvColor{255, 255, 255, 255});
  EXPECT_TRUE(mm.OnDraw(proj, *canvas).ok());
  return canvas;
}

}  // namespace

TEST(MovingMapTrailTest, DotsCloserThanThreePixelsAreSkipped) {
  const fv::MapProjection proj = Proj();
  // 100 fixes 1 px apart: one dot every 3 px is 34 dots.
  auto dense = LiveMap(proj, 100, 1.0);
  EXPECT_EQ(Draw(*dense, proj, dense->recording().back())->alphas.size(), 34u);
  // 20 fixes 5 px apart: none is thinned.
  auto sparse = LiveMap(proj, 20, 5.0);
  EXPECT_EQ(Draw(*sparse, proj, sparse->recording().back())->alphas.size(), 20u);
}

TEST(MovingMapTrailTest, TrailDotsOffDrawsNoDots) {
  const fv::MapProjection proj = Proj();
  auto mm = LiveMap(proj, 20, 5.0);
  TrailStyle style;
  style.dots = false;
  style.line = true;
  mm->SetTrailStyle(style);
  EXPECT_TRUE(Draw(*mm, proj, mm->recording().back())->alphas.empty());
}

TEST(MovingMapTrailTest, TheDrawnTrailIsCappedAndASaveHoldsEveryFix) {
  Scratch dir;
  const fv::MapProjection proj = Proj();
  DeskMovingMap mm;
  TrailStyle style;
  style.trail_max = 5;
  mm.SetTrailStyle(style);
  ASSERT_TRUE(mm.FileNew().ok());
  for (int i = 0; i < 12; ++i) mm.AddTrailFix(FixAtPixel(proj, 100 + i * 10.0, 300, i));
  ASSERT_EQ(mm.recording().size(), 12u);
  ASSERT_EQ(mm.trail().size, 5u);
  // The drawn trail is the newest five.
  EXPECT_EQ(mm.trail().data[0].time_s, mm.recording()[7].time_s);
  EXPECT_EQ(Draw(mm, proj, mm.recording().back())->alphas.size(), 5u);

  const std::string path = dir.Path("capped.gpx");
  ASSERT_TRUE(mm.FileSaveAs(path, 0).ok());
  std::vector<PositionFix> read;
  ASSERT_TRUE(fv::ReadTrackFixes(path, &read).ok());
  ASSERT_EQ(read.size(), 12u);
  EXPECT_NEAR(read.front().lat, mm.recording().front().lat, 1e-6);
  EXPECT_NEAR(read.front().lon, mm.recording().front().lon, 1e-6);
}

TEST(MovingMapTrailTest, RecordingStopsAtTheLimit) {
  const fv::MapProjection proj = Proj();
  DeskMovingMap mm;
  TrailStyle style;
  style.record_max = 3;
  mm.SetTrailStyle(style);
  ASSERT_TRUE(mm.FileNew().ok());
  for (int i = 0; i < 3; ++i) mm.AddTrailFix(FixAtPixel(proj, 100 + i * 10.0, 300, i));
  EXPECT_FALSE(mm.recording_full());
  mm.AddTrailFix(FixAtPixel(proj, 200, 300, 9));
  EXPECT_TRUE(mm.recording_full());
  ASSERT_EQ(mm.recording().size(), 3u);
  EXPECT_EQ(mm.recording().back().time_s, 1794225602.0);
}

TEST(MovingMapTrailTest, AReplayDrawsPastDotsInItsColourAndFutureDotsFaint) {
  Scratch dir;
  const fv::MapProjection proj = Proj();
  std::vector<PositionFix> fixes;
  for (int i = 0; i < 10; ++i) fixes.push_back(FixAtPixel(proj, 150 + i * 50.0, 300 + i * 10.0, i));
  const std::string path = dir.Path("ride.gpx");
  ASSERT_TRUE(fv::WriteGpxFile(path, fv::BuildGpxTrack(fixes, "ride", std::string())).ok());

  DeskMovingMap mm;
  ASSERT_TRUE(mm.FileOpen(path).ok());
  // The ship sits between the fifth and sixth fixes.
  mm.SetReplayTime(fixes[4].time_s + 0.5);
  const PositionFix ship = FixAtPixel(proj, 375, 345, 4.5);
  const std::unique_ptr<DotCanvas> drawn = Draw(mm, proj, ship);
  const DotCanvas& canvas = *drawn;

  ASSERT_EQ(canvas.alphas.size(), 10u);
  for (int i = 0; i < 10; ++i) EXPECT_EQ(canvas.alphas[i], i < 5 ? 255 : 64) << "dot " << i;
  // A past dot's centre is the map's colour; a future one is a faint tint of it.
  const fv::FvColor c = mm.color();
  const unsigned char* past = canvas.Buffer().Row(300) + 150 * 4;
  EXPECT_EQ(past[0], c.r);
  EXPECT_EQ(past[1], c.g);
  EXPECT_EQ(past[2], c.b);
  const unsigned char* future = canvas.Buffer().Row(390) + 600 * 4;
  EXPECT_GT(future[1], c.g);
  EXPECT_LT(future[1], 255);
  EXPECT_EQ(Fnv1a(canvas.Buffer()), 15521372013564688658ull) << "actual = " << Fnv1a(canvas.Buffer());
}
