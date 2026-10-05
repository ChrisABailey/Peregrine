// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for PPSprite.h: the ink rectangle and the crop.

#include "PPSprite.h"

#include <gtest/gtest.h>

namespace {

void Ink(fv::PixelBuffer& buf, int x, int y, unsigned char alpha) {
  unsigned char* px = buf.Row(y) + x * 4;
  px[0] = 10;
  px[1] = 20;
  px[2] = 30;
  px[3] = alpha;
}

TEST(Sprite, ABlankBufferHasNoInk) {
  fv::PixelBuffer buf(16, 9);
  EXPECT_TRUE(pippin::InkBounds(buf).empty());
}

TEST(Sprite, TheBoundsHoldEveryInkedPixelAndNoMore) {
  fv::PixelBuffer buf(40, 30);
  Ink(buf, 5, 7, 255);
  Ink(buf, 31, 12, 1);  // faint ink is still ink
  Ink(buf, 9, 22, 128);
  const pippin::InkRect r = pippin::InkBounds(buf);
  EXPECT_EQ(5, r.x);
  EXPECT_EQ(7, r.y);
  EXPECT_EQ(27, r.width);
  EXPECT_EQ(16, r.height);
}

TEST(Sprite, CropKeepsThePixelsAtTheirOffsets) {
  fv::PixelBuffer buf(40, 30);
  Ink(buf, 5, 7, 255);
  Ink(buf, 31, 12, 200);
  const pippin::InkRect r = pippin::InkBounds(buf);
  const fv::PixelBuffer out = pippin::Crop(buf, r);
  ASSERT_EQ(r.width, out.Width());
  ASSERT_EQ(r.height, out.Height());
  EXPECT_EQ(255, out.Row(0)[3]);
  EXPECT_EQ(200, out.Row(12 - 7)[(31 - 5) * 4 + 3]);
  EXPECT_EQ(0, out.Row(1)[3]);
}

}  // namespace
