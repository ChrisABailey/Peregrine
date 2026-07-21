// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// L4 overlay SPI tests: manager stack/draw-order/routing semantics
// (synthetic recording overlays) and the built-in grid overlay's golden.

#include "fvkit/overlay/grid.h"
#include "fvkit/overlay/manager.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/tools/png_write.h"

namespace {

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int i = 0; i < b.Width() * 4; ++i) {
      h ^= row[i];
      h *= 1099511628211ull;
    }
  }
  return h;
}

// Records calls; handles events when told to.
class Recorder : public fv::Overlay {
 public:
  Recorder(std::string name, std::vector<std::string>* log, bool handles)
      : Overlay(std::move(name)), log_(log), handles_(handles) {}

  fv::Status OnDraw(const fv::MapProjection&, fv::ICanvas&) override {
    log_->push_back(Name() + ":draw");
    return fv::Status::Ok();
  }
  bool OnMouseDown(const fv::MouseEvent&) override {
    log_->push_back(Name() + ":down");
    return handles_;
  }
  bool OnKeyDown(int) override {
    log_->push_back(Name() + ":key");
    return handles_;
  }

 private:
  std::vector<std::string>* log_;
  bool handles_;
};

class ManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    a_ = std::make_shared<Recorder>("a", &log_, false);
    b_ = std::make_shared<Recorder>("b", &log_, true);
    c_ = std::make_shared<Recorder>("c", &log_, false);
    ASSERT_TRUE(mgr_.Add(a_).ok());
    ASSERT_TRUE(mgr_.Add(b_).ok());
    ASSERT_TRUE(mgr_.Add(c_).ok());
    proj_.SetSurfaceSize(100, 100);
    proj_.SetCenter({0, 0});
    proj_.SetScale(1000000);
  }

  std::vector<std::string> log_;
  std::shared_ptr<Recorder> a_, b_, c_;
  fv::OverlayManager mgr_;
  fv::MapProjection proj_;
};

TEST_F(ManagerTest, DrawsBottomUp) {
  fv::CpuCanvas canvas(100, 100);
  ASSERT_TRUE(mgr_.DrawAll(proj_, canvas).ok());
  EXPECT_EQ(log_, (std::vector<std::string>{"a:draw", "b:draw", "c:draw"}));
}

TEST_F(ManagerTest, RoutesTopDownUntilHandled) {
  // c (top) declines, b handles -> a never sees it
  EXPECT_TRUE(mgr_.RouteMouseDown({10, 10, 0}));
  EXPECT_EQ(log_, (std::vector<std::string>{"c:down", "b:down"}));
}

TEST_F(ManagerTest, InvisibleOverlaysSkipped) {
  b_->SetVisible(false);
  EXPECT_FALSE(mgr_.RouteKeyDown('x'));  // only c and a, neither handles
  EXPECT_EQ(log_, (std::vector<std::string>{"c:key", "a:key"}));
  log_.clear();
  fv::CpuCanvas canvas(100, 100);
  ASSERT_TRUE(mgr_.DrawAll(proj_, canvas).ok());
  EXPECT_EQ(log_, (std::vector<std::string>{"a:draw", "c:draw"}));
}

TEST_F(ManagerTest, StackOps) {
  ASSERT_TRUE(mgr_.MoveToTop(a_).ok());
  EXPECT_TRUE(mgr_.RouteMouseDown({1, 1, 0}));
  // a now on top (declines), then c (declines), then b handles
  EXPECT_EQ(log_, (std::vector<std::string>{"a:down", "c:down", "b:down"}));

  EXPECT_TRUE(mgr_.Remove(b_).ok());
  EXPECT_EQ(mgr_.Remove(b_).code, fv::kNotFound);
  EXPECT_EQ(mgr_.Add(a_).code, fv::kInvalidArg);  // duplicate
  EXPECT_EQ(mgr_.Overlays().size(), 2u);
}

TEST_F(ManagerTest, DrawErrorNamesOverlay) {
  class Failing : public fv::Overlay {
   public:
    Failing() : Overlay("bad") {}
    fv::Status OnDraw(const fv::MapProjection&, fv::ICanvas&) override {
      return fv::Status::Error(fv::kInternal, "boom");
    }
  };
  ASSERT_TRUE(mgr_.Add(std::make_shared<Failing>()).ok());
  fv::CpuCanvas canvas(100, 100);
  fv::Status s = mgr_.DrawAll(proj_, canvas);
  EXPECT_EQ(s.code, fv::kInternal);
  EXPECT_NE(s.message.find("'bad'"), std::string::npos);
}

// Pinned 2026-07-17 after visually verifying overlay_grid.png (graticule
// over Atlanta at 1:2M: 1-deg lines, correct spacing). 0 = probe.
constexpr uint64_t kHashGrid = 0xef46a5e78c021e47ull;

TEST(GridOverlayGolden, GraticuleAtlanta) {
  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(240, 180).ok());
  ASSERT_TRUE(proj.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(proj.SetScale(2000000.0).ok());

  fv::CpuCanvas canvas(240, 180);
  canvas.Clear(fv::FvColor{0, 0, 32, 255});
  fv::GridOverlay grid;
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());

  // some ink, mostly background
  long inked = 0;
  for (int y = 0; y < 180; ++y)
    for (int x = 0; x < 240; ++x)
      if (canvas.Buffer().Row(y)[4 * x + 0] > 32) ++inked;
  EXPECT_GT(inked, 200);
  EXPECT_LT(inked, 240 * 180 / 4);

  uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashGrid == 0) {
    printf("PROBE grid hash: 0x%llxull\n", (unsigned long long)h);
    fv::WritePng(canvas.Buffer(), "overlay_grid.png");
  } else {
    EXPECT_EQ(h, kHashGrid);
  }
}

}  // namespace
