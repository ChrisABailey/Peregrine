// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// The fix and the thread crossing (nav plan MM1). Headless and synthetic;
// nothing here reads data or a clock it does not own.

#include "fvkit/nav/position.h"

#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <vector>

namespace {

using fv::FixCallback;
using fv::FixQueue;
using fv::PositionFix;
using fv::PositionSourceBase;

TEST(PositionFix, IsEmptyUntilAFieldIsSet) {
  PositionFix fix;
  EXPECT_FALSE(fix.has_position);
  EXPECT_FALSE(fix.has_speed);
  EXPECT_FALSE(fix.has_true_heading);
  EXPECT_FALSE(fix.has_time);

  fix.SetPosition(32.78, -79.93);
  EXPECT_TRUE(fix.has_position);
  EXPECT_DOUBLE_EQ(32.78, fix.position().lat);
  EXPECT_DOUBLE_EQ(-79.93, fix.position().lon);
}

// The reason the -1000.0 sentinels did not port: the NMEA sentences of one
// epoch each carry a DIFFERENT subset, and merging them must not let an
// absent field overwrite a present one.
TEST(PositionFix, MergeTakesOnlyTheValidFieldsOfTheOther) {
  PositionFix gga;  // position + altitude + quality, no speed, no course
  gga.SetPosition(32.78, -79.93);
  gga.altitude_msl_m = 12.0;
  gga.has_altitude = true;
  gga.hdop = 0.9;
  gga.has_hdop = true;

  PositionFix vtg;  // course + speed and nothing else
  vtg.true_heading_deg = 137.5;
  vtg.has_true_heading = true;
  vtg.speed_mps = 8.0;
  vtg.has_speed = true;

  PositionFix merged = gga;
  merged.Merge(vtg);

  EXPECT_TRUE(merged.has_position);
  EXPECT_DOUBLE_EQ(32.78, merged.lat);
  EXPECT_TRUE(merged.has_altitude);
  EXPECT_DOUBLE_EQ(12.0, merged.altitude_msl_m);
  EXPECT_TRUE(merged.has_true_heading);
  EXPECT_DOUBLE_EQ(137.5, merged.true_heading_deg);
  EXPECT_TRUE(merged.has_speed);
  EXPECT_TRUE(merged.has_hdop);
  EXPECT_FALSE(merged.has_time);

  // And the other way round: the altitude the VTG does not carry survives.
  PositionFix other = vtg;
  other.Merge(gga);
  EXPECT_TRUE(other.has_true_heading);
  EXPECT_TRUE(other.has_altitude);
}

TEST(NormalizeHeadingDeg, WrapsIntoZeroToThreeSixty) {
  EXPECT_DOUBLE_EQ(0.0, fv::NormalizeHeadingDeg(0.0));
  EXPECT_DOUBLE_EQ(0.0, fv::NormalizeHeadingDeg(360.0));
  EXPECT_DOUBLE_EQ(350.0, fv::NormalizeHeadingDeg(-10.0));
  EXPECT_DOUBLE_EQ(10.0, fv::NormalizeHeadingDeg(730.0));
  EXPECT_DOUBLE_EQ(180.0, fv::NormalizeHeadingDeg(-180.0));
}

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

// The smallest possible source: it emits what it is told to, on the caller's
// thread, which is exactly the contract PositionSourceBase states.
class ManualSource : public PositionSourceBase {
 public:
  fv::Status Start() override {
    running_ = true;
    return fv::Status::Ok();
  }
  void Stop() override { running_ = false; }
  void Send(const PositionFix& fix) { Emit(fix); }
};

TEST(PositionSource, DeliversToTheListenerAndNowhereElse) {
  auto source = std::make_shared<ManualSource>();
  std::vector<PositionFix> seen;
  source->SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });

  PositionFix fix;
  fix.SetPosition(1.0, 2.0);
  ASSERT_TRUE(source->Start().ok());
  EXPECT_TRUE(source->running());
  source->Send(fix);
  ASSERT_EQ(1u, seen.size());
  EXPECT_DOUBLE_EQ(1.0, seen[0].lat);

  // A second SetListener REPLACES; an empty one clears, and emitting into no
  // listener is legal and silent.
  source->SetListener(FixCallback{});
  source->Send(fix);
  EXPECT_EQ(1u, seen.size());

  source->Stop();
  EXPECT_FALSE(source->running());
}

// ---------------------------------------------------------------------------
// FixQueue
// ---------------------------------------------------------------------------

TEST(FixQueue, DrainsInOrderAndEmptiesItself) {
  FixQueue queue;
  for (int i = 0; i < 3; ++i) {
    PositionFix fix;
    fix.SetPosition(static_cast<double>(i), 0.0);
    queue.Push(fix);
  }
  EXPECT_EQ(3u, queue.size());

  std::vector<PositionFix> out;
  EXPECT_EQ(3u, queue.Drain(&out));
  ASSERT_EQ(3u, out.size());
  EXPECT_DOUBLE_EQ(0.0, out[0].lat);
  EXPECT_DOUBLE_EQ(2.0, out[2].lat);
  EXPECT_TRUE(queue.empty());
}

// A position feed is a stream of the present, so the fix that is dropped when
// the consumer has fallen behind is the OLDEST one — and the drop is counted
// rather than silent.
TEST(FixQueue, FullDropsTheOldestAndSaysHowMany) {
  FixQueue queue(2);
  for (int i = 0; i < 5; ++i) {
    PositionFix fix;
    fix.SetPosition(static_cast<double>(i), 0.0);
    queue.Push(fix);
  }
  EXPECT_EQ(2u, queue.size());
  EXPECT_EQ(3u, queue.dropped());

  std::vector<PositionFix> out;
  queue.Drain(&out);
  ASSERT_EQ(2u, out.size());
  EXPECT_DOUBLE_EQ(3.0, out[0].lat);
  EXPECT_DOUBLE_EQ(4.0, out[1].lat);
}

TEST(FixQueue, DrainLatestKeepsTheNewestAndThrowsTheRestAway) {
  FixQueue queue;
  PositionFix out;
  EXPECT_FALSE(queue.DrainLatest(&out));

  for (int i = 0; i < 4; ++i) {
    PositionFix fix;
    fix.SetPosition(static_cast<double>(i), 0.0);
    queue.Push(fix);
  }
  ASSERT_TRUE(queue.DrainLatest(&out));
  EXPECT_DOUBLE_EQ(3.0, out.lat);
  EXPECT_TRUE(queue.empty());
}

// The queue's whole reason for existing: a source that delivers on its own
// thread, drained on the consumer's, with no lock in the consumer's code.
TEST(FixQueue, IsTheCrossingBetweenAProducerThreadAndAConsumerTick) {
  FixQueue queue(1024);
  auto source = std::make_shared<ManualSource>();
  source->SetListener(queue.Listener());
  ASSERT_TRUE(source->Start().ok());

  constexpr int kCount = 500;
  std::thread producer([&source] {
    for (int i = 0; i < kCount; ++i) {
      PositionFix fix;
      fix.SetPosition(static_cast<double>(i), 0.0);
      source->Send(fix);
    }
  });

  std::vector<PositionFix> seen;
  while (static_cast<int>(seen.size()) < kCount) {
    queue.Drain(&seen);  // the "tick"
  }
  producer.join();
  queue.Drain(&seen);

  ASSERT_EQ(kCount, static_cast<int>(seen.size()));
  EXPECT_EQ(0u, queue.dropped());
  for (int i = 0; i < kCount; ++i) {
    EXPECT_DOUBLE_EQ(static_cast<double>(i), seen[i].lat);
  }
}

}  // namespace
