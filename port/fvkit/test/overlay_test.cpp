// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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
  bool OnKeyDown(const fv::KeyEvent& e) override {
    log_->push_back(Name() + ":key");
    last_key_ = e;
    return handles_;
  }

  const fv::KeyEvent& last_key() const { return last_key_; }

 private:
  std::vector<std::string>* log_;
  bool handles_;
  fv::KeyEvent last_key_;
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

// --- KeyEvent (2026-08-04) -------------------------------------------------
// The seam used to hand overlays a bare `int key` with no documented
// numbering and no modifiers, which is unimplementable from a real UI
// toolkit: tkinter's own keycode is a platform-specific composite, so there
// was no correct value to pass. These pin the replacement.

TEST(KeyEvent, VirtualKeyValuesAreWin32AndNeverRenumbered) {
  // Not decoration: overlay.h promises these ARE the Win32 VK codes, so a
  // Windows shell can pass WM_KEYDOWN's wParam straight through. If someone
  // ever "tidies" the enum into 0,1,2,... this fails, which is the point.
  EXPECT_EQ(fv::Key::kBackspace, 0x08);
  EXPECT_EQ(fv::Key::kTab, 0x09);
  EXPECT_EQ(fv::Key::kReturn, 0x0D);
  EXPECT_EQ(fv::Key::kEscape, 0x1B);
  EXPECT_EQ(fv::Key::kSpace, 0x20);
  EXPECT_EQ(fv::Key::kPageUp, 0x21);
  EXPECT_EQ(fv::Key::kPageDown, 0x22);
  EXPECT_EQ(fv::Key::kEnd, 0x23);
  EXPECT_EQ(fv::Key::kHome, 0x24);
  EXPECT_EQ(fv::Key::kLeft, 0x25);
  EXPECT_EQ(fv::Key::kUp, 0x26);
  EXPECT_EQ(fv::Key::kRight, 0x27);
  EXPECT_EQ(fv::Key::kDown, 0x28);
  EXPECT_EQ(fv::Key::kInsert, 0x2D);
  EXPECT_EQ(fv::Key::kDelete, 0x2E);
  EXPECT_EQ(fv::Key::kF1, 0x70);
  EXPECT_EQ(fv::Key::kF12, 0x7B);
  EXPECT_EQ(fv::Key::kF1 + 11, fv::Key::kF12);  // F(n) == kF1 + (n-1)

  // The property that makes the choice pleasant to use: letters and digits
  // need no constant, because VK_A..VK_Z and VK_0..VK_9 ARE their ASCII
  // uppercase code points.
  EXPECT_EQ(0x41, static_cast<int>('A'));
  EXPECT_EQ(0x5A, static_cast<int>('Z'));
  EXPECT_EQ(0x30, static_cast<int>('0'));
  // ... and these three coincide with their ASCII controls, deliberately.
  EXPECT_EQ(fv::Key::kReturn, static_cast<int>('\r'));
  EXPECT_EQ(fv::Key::kTab, static_cast<int>('\t'));
  EXPECT_EQ(fv::Key::kSpace, static_cast<int>(' '));
}

TEST(KeyEvent, DefaultsAreAnEmptyPressAndNotAnAccidentalKey) {
  const fv::KeyEvent e;
  EXPECT_EQ(e.key, fv::Key::kNone);
  EXPECT_EQ(e.text, 0u);
  EXPECT_FALSE(e.shift);
  EXPECT_FALSE(e.ctrl);
  EXPECT_FALSE(e.alt);
  EXPECT_FALSE(e.meta);
}

TEST_F(ManagerTest, TheWholeKeyEventReachesTheOverlayIntact) {
  // The bug this replaced: modifiers had nowhere to travel, so an overlay
  // could not tell Ctrl-Z from Z. Route one of each and read it back.
  fv::KeyEvent e;
  e.key = 'Z';
  e.text = 'z';
  e.ctrl = true;
  e.shift = true;
  EXPECT_TRUE(mgr_.RouteKeyDown(e));  // b handles

  EXPECT_EQ(b_->last_key().key, 'Z');
  EXPECT_EQ(b_->last_key().text, uint32_t{'z'});
  EXPECT_TRUE(b_->last_key().ctrl);
  EXPECT_TRUE(b_->last_key().shift);
  EXPECT_FALSE(b_->last_key().alt);
  EXPECT_FALSE(b_->last_key().meta);
  // c is above b and saw it first, unmodified in transit.
  EXPECT_EQ(c_->last_key().key, 'Z');
  EXPECT_TRUE(c_->last_key().ctrl);
  // a is below b, which handled it.
  EXPECT_EQ(a_->last_key().key, fv::Key::kNone);
  EXPECT_EQ(log_, (std::vector<std::string>{"c:key", "b:key"}));
}

TEST_F(ManagerTest, ANonPrintingKeyCarriesNoText) {
  fv::KeyEvent e;
  e.key = fv::Key::kDelete;   // text stays 0: Delete types nothing
  EXPECT_TRUE(mgr_.RouteKeyDown(e));
  EXPECT_EQ(b_->last_key().key, fv::Key::kDelete);
  EXPECT_EQ(b_->last_key().text, 0u);
}

TEST_F(ManagerTest, InvisibleOverlaysSkipped) {
  b_->SetVisible(false);
  EXPECT_FALSE(mgr_.RouteKeyDown(fv::KeyEvent{'X', 'x'}));  // neither handles
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

// Repinned 2026-08-29 with the real graticule (the 2026-07-17 pin was the
// 30-line sample). 0 = probe: set it to 0, run, and the test prints the hash
// and writes overlay_grid.png for a human to look at.
//
// The VIEWPORT moved with it, and that is the substantive change. The old one
// was 240x180 at 1:2M, which spans about half a degree — and the table's minor
// spacing at 1:2M is a whole degree, so the honest graticule there is one
// meridian and no parallels at all. The sample drew a full grid because it
// derived its interval from pixels; a cartographic table does not, and a
// golden over an empty picture proves nothing. 1:5M over 640x480 is a few
// degrees each way: major lines, minor lines, ticks and labels all present.
constexpr uint64_t kHashGrid = 0x8ff2e4e5ade27e84ull;

TEST(GridOverlayGolden, GraticuleAtlanta) {
  fv::MapProjection proj;
  ASSERT_TRUE(proj.SetSurfaceSize(640, 480).ok());
  ASSERT_TRUE(proj.SetCenter({33.7488, -84.3882}).ok());
  ASSERT_TRUE(proj.SetScale(5000000.0).ok());

  fv::CpuCanvas canvas(640, 480);
  canvas.Clear(fv::FvColor{0, 0, 32, 255});
  // Labels need a font; without one the grid still draws its lines, so the
  // golden would silently stop covering half the overlay.
  const char* fonts[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  bool have_font = false;
  for (const char* f : fonts)
    if (canvas.SetDefaultFont(f).ok()) {
      have_font = true;
      break;
    }
  if (!have_font) GTEST_SKIP() << "no system font; the golden would not match";

  fv::GridOverlay grid;
  ASSERT_TRUE(grid.OnDraw(proj, canvas).ok());
  ASSERT_GT(grid.last_draw().parallels, 0);
  ASSERT_GT(grid.last_draw().labels_placed, 0);

  // some ink, mostly background
  long inked = 0;
  for (int y = 0; y < 480; ++y)
    for (int x = 0; x < 640; ++x)
      if (canvas.Buffer().Row(y)[4 * x + 0] > 32) ++inked;
  EXPECT_GT(inked, 200);
  EXPECT_LT(inked, 640 * 480 / 4);

  uint64_t h = Fnv1a(canvas.Buffer());
  if (kHashGrid == 0) {
    printf("PROBE grid hash: 0x%llxull\n", (unsigned long long)h);
    fv::WritePng(canvas.Buffer(), "overlay_grid.png");
  } else {
    EXPECT_EQ(h, kHashGrid);
  }
}

}  // namespace
