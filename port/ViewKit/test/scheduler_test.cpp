// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RenderScheduler: latest request wins, duplicates are ignored, the last frame
// is kept.

#include "fv_view_scheduler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace {

using fv::GeoPoint;
using fv::view::Frame;
using fv::view::RenderScheduler;
using fv::view::Viewport;

Viewport View(double lon) {
  return Viewport::Make(GeoPoint{0, lon}, 1e6).WithSurface(4, 3, 1, 0.25);
}

std::shared_ptr<const Frame> Draw(const Viewport& v, uint64_t gen) {
  auto f = std::make_shared<Frame>();
  f->viewport = v;
  f->generation = gen;
  f->width = v.PixelWidth();
  f->height = v.PixelHeight();
  f->rgba.assign(static_cast<size_t>(f->width) * f->height * 4, 0xff);
  return f;
}

TEST(RenderScheduler, RendersAndKeepsTheLastFrame) {
  std::vector<uint64_t> delivered;
  std::mutex mu;
  RenderScheduler s(Draw, [&](std::shared_ptr<const Frame> f) {
    std::lock_guard<std::mutex> lock(mu);
    delivered.push_back(f->generation);
  });
  s.Request(View(1), 1);
  s.WaitIdle();
  ASSERT_NE(s.LastFrame(), nullptr);
  EXPECT_EQ(s.LastFrame()->generation, 1u);
  EXPECT_EQ(s.LastFrame()->rgba.size(), 4u * 3u * 4u);
  // The same generation again is not drawn twice.
  s.Request(View(1), 1);
  s.WaitIdle();
  EXPECT_EQ(s.FramesRendered(), 1u);
  std::lock_guard<std::mutex> lock(mu);
  EXPECT_EQ(delivered, std::vector<uint64_t>{1});
}

TEST(RenderScheduler, RequestsOvertakenWhileBusyAreDropped) {
  // The first render blocks until released; requests 2..9 arrive meanwhile
  // and only the newest is drawn after it.
  std::mutex mu;
  std::condition_variable cv;
  bool started = false, release = false;
  std::vector<uint64_t> drawn;
  RenderScheduler s(
      [&](const Viewport& v, uint64_t gen) {
        std::unique_lock<std::mutex> lock(mu);
        drawn.push_back(gen);
        if (gen == 1) {
          started = true;
          cv.notify_all();
          cv.wait(lock, [&] { return release; });
        }
        return Draw(v, gen);
      },
      nullptr);
  s.Request(View(1), 1);
  {
    std::unique_lock<std::mutex> lock(mu);
    cv.wait(lock, [&] { return started; });
  }
  for (uint64_t g = 2; g <= 9; ++g) s.Request(View(static_cast<double>(g)), g);
  {
    std::lock_guard<std::mutex> lock(mu);
    release = true;
  }
  cv.notify_all();
  s.WaitIdle();
  EXPECT_EQ(drawn, (std::vector<uint64_t>{1, 9}));
  EXPECT_EQ(s.LastFrame()->generation, 9u);
  EXPECT_EQ(s.LastFrame()->viewport.Center().lon, 9.0);
}

TEST(RenderScheduler, NullFrameIsNotKept) {
  RenderScheduler s([](const Viewport&, uint64_t) { return nullptr; }, nullptr);
  s.Request(View(1), 1);
  s.WaitIdle();
  EXPECT_EQ(s.LastFrame(), nullptr);
  EXPECT_EQ(s.FramesRendered(), 0u);
}

TEST(RenderScheduler, DestroyWithWorkPending) {
  std::atomic<int> calls{0};
  {
    RenderScheduler s(
        [&](const Viewport& v, uint64_t g) {
          ++calls;
          return Draw(v, g);
        },
        nullptr);
    for (uint64_t g = 1; g <= 50; ++g) s.Request(View(1), g);
  }
  EXPECT_GE(calls.load(), 0);
}

}  // namespace
