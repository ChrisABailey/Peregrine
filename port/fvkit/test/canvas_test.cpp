// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// CpuCanvas tests. The rasterizer is deterministic (non-AA, no float
// text in the pinned scenes), so scenes pin an FNV-1a hash of the buffer;
// a PNG of each scene is also written through the ported libpng (fv_png)
// into the build dir for eyeball checks and as codec-interop proof.
// Text rendering depends on the host font file, so text tests assert
// extents/ink, never hashes.

#include "fvkit/canvas/cpu_canvas.h"

#include <gtest/gtest.h>
#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// Golden-PNG side channel: prove the ported libpng round-trips the canvas.
void WriteAndCheckPng(const fv::PixelBuffer& b, const std::string& name) {
  std::string path = name + ".png";
  FILE* f = fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(png);
  ASSERT_EQ(setjmp(png_jmpbuf(png)), 0);
  png_init_io(png, f);
  png_set_IHDR(png, info, b.Width(), b.Height(), 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (int y = 0; y < b.Height(); ++y)
    png_write_row(png, const_cast<unsigned char*>(b.Row(y)));
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  fclose(f);
  // read back and compare a pixel row to prove the round trip
  FILE* rf = fopen(path.c_str(), "rb");
  ASSERT_NE(rf, nullptr);
  png_structp rpng =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop rinfo = png_create_info_struct(rpng);
  ASSERT_EQ(setjmp(png_jmpbuf(rpng)), 0);
  png_init_io(rpng, rf);
  png_read_info(rpng, rinfo);
  ASSERT_EQ((int)png_get_image_width(rpng, rinfo), b.Width());
  std::vector<unsigned char> row(b.Width() * 4);
  png_read_row(rpng, row.data(), nullptr);
  EXPECT_EQ(memcmp(row.data(), b.Row(0), row.size()), 0);
  png_destroy_read_struct(&rpng, &rinfo, nullptr);
  fclose(rf);
}

fv::FvColor Rgb(unsigned char r, unsigned char g, unsigned char b,
                unsigned char a = 255) {
  return fv::FvColor{r, g, b, a};
}

const unsigned char* Px(const fv::PixelBuffer& b, int x, int y) {
  return b.Row(y) + 4 * x;
}

// Update these after an intentional rasterizer change; a silent change in
// any primitive breaks the corresponding hash.
// Pinned 2026-07-16 after visual verification of the PNGs (star hole, dash
// gaps, blend, coastline all eyeballed correct). 0 = probe mode.
constexpr uint64_t kHashTriangle = 0xd836bc9ee4383ebull;
constexpr uint64_t kHashStar = 0x8ccc756ff03d6443ull;
constexpr uint64_t kHashScene = 0x868d7405110182a1ull;

TEST(CpuCanvasGolden, FilledTriangle) {
  fv::CpuCanvas c(64, 64);
  c.Clear(Rgb(0, 0, 0));
  fv::Brush fill{Rgb(255, 0, 0)};
  ASSERT_TRUE(
      c.DrawPolyPolygon({{{8, 8}, {56, 16}, {16, 56}}}, &fill, nullptr).ok());
  // interior filled, exterior untouched
  EXPECT_EQ(Px(c.Buffer(), 24, 24)[0], 255);
  EXPECT_EQ(Px(c.Buffer(), 4, 4)[0], 0);
  uint64_t h = Fnv1a(c.Buffer());
  if (kHashTriangle == 0)
    printf("PROBE triangle hash: 0x%llxull\n", (unsigned long long)h);
  else
    EXPECT_EQ(h, kHashTriangle);
  WriteAndCheckPng(c.Buffer(), "canvas_triangle");
}

TEST(CpuCanvasGolden, EvenOddStarHasHole) {
  fv::CpuCanvas c(64, 64);
  c.Clear(Rgb(0, 0, 0));
  // classic 5-point star drawn as a self-intersecting polygon: even-odd
  // fill leaves the central pentagon EMPTY (GDI ALTERNATE semantics)
  std::vector<fv::PixelPoint> star{
      {32, 4}, {13, 60}, {60, 25}, {4, 25}, {51, 60}};
  fv::Brush fill{Rgb(0, 255, 0)};
  ASSERT_TRUE(c.DrawPolyPolygon({star}, &fill, nullptr).ok());
  EXPECT_EQ(Px(c.Buffer(), 32, 32)[1], 0) << "center must be a hole";
  EXPECT_EQ(Px(c.Buffer(), 32, 12)[1], 255) << "top point must be filled";
  uint64_t h = Fnv1a(c.Buffer());
  if (kHashStar == 0)
    printf("PROBE star hash: 0x%llxull\n", (unsigned long long)h);
  else
    EXPECT_EQ(h, kHashStar);
  WriteAndCheckPng(c.Buffer(), "canvas_star");
}

TEST(CpuCanvasGolden, CompositeScene) {
  fv::CpuCanvas c(128, 96);
  c.Clear(Rgb(16, 24, 40));
  fv::Brush sea{Rgb(40, 80, 160)};
  fv::Pen coast{Rgb(255, 255, 128), 2, {}};
  ASSERT_TRUE(c.DrawRectangle({8, 8, 112, 80}, &sea, &coast).ok());
  fv::Brush land{Rgb(72, 128, 64)};
  ASSERT_TRUE(
      c.DrawPolyPolygon({{{20, 70}, {60, 30}, {100, 60}, {80, 84}, {30, 84}}},
                        &land, nullptr)
          .ok());
  fv::Pen dashed{Rgb(255, 64, 64), 1, {6, 3}};
  ASSERT_TRUE(c.DrawLines({{12, 20}, {116, 20}}, dashed).ok());
  fv::Brush light{Rgb(255, 255, 255)};
  ASSERT_TRUE(c.DrawEllipse({88, 16, 20, 20}, &light, nullptr).ok());
  // half-transparent overlay pixmap
  fv::PixelBuffer overlay(16, 16);
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      unsigned char* p = overlay.Row(y) + 4 * x;
      p[0] = 255; p[1] = 0; p[2] = 255; p[3] = 128;
    }
  ASSERT_TRUE(c.DrawPixmap(overlay, 16, 16).ok());

  uint64_t h = Fnv1a(c.Buffer());
  if (kHashScene == 0)
    printf("PROBE scene hash: 0x%llxull\n", (unsigned long long)h);
  else
    EXPECT_EQ(h, kHashScene);
  WriteAndCheckPng(c.Buffer(), "canvas_scene");
}

// --- the R3b edge-table fill, against the algorithm it replaced ------------
//
// FillScanlines used to walk every edge of every ring for every scanline in
// the bounding box. R3b buckets edges by the first scanline they can cross,
// which measured as the largest single saving in a vector frame — and which
// is only worth having if it is EXACTLY the same picture. The golden hashes
// above are one guard; this is the direct one, over the shapes that actually
// exercise the bucketing: horizontal edges (never cross a scanline and are
// dropped from the table entirely), edges that start above the canvas or end
// below it (clamped into the drawn range), several rings at once, and a
// self-intersecting ring whose crossings must still pair up even-odd.
//
// The oracle is the pre-R3b loop, transcribed. It is deliberately the naive
// O(scanlines * edges) version — that is the point.
void ReferenceFill(fv::PixelBuffer* buf,
                   const std::vector<std::vector<fv::PixelPoint>>& rings,
                   const fv::FvColor& c) {
  int y_min = buf->Height(), y_max = -1;
  for (const auto& ring : rings)
    for (const auto& p : ring) {
      y_min = std::min(y_min, p.y);
      y_max = std::max(y_max, p.y);
    }
  y_min = std::max(y_min, 0);
  y_max = std::min(y_max, buf->Height() - 1);

  std::vector<double> xs;
  for (int y = y_min; y <= y_max; ++y) {
    xs.clear();
    const double yc = y + 0.5;
    for (const auto& ring : rings) {
      const size_t n = ring.size();
      if (n < 3) continue;
      for (size_t i = 0; i < n; ++i) {
        const fv::PixelPoint& a = ring[i];
        const fv::PixelPoint& b = ring[(i + 1) % n];
        if ((a.y <= yc) == (b.y <= yc)) continue;
        const double t = (yc - a.y) / (double)(b.y - a.y);
        xs.push_back(a.x + t * (b.x - a.x));
      }
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {
      const int x0 = (int)std::ceil(xs[i] - 0.5);
      const int x1 = (int)std::floor(xs[i + 1] - 0.5);
      for (int x = std::max(x0, 0); x <= std::min(x1, buf->Width() - 1); ++x) {
        unsigned char* p = buf->Row(y) + 4 * x;
        if (c.a == 0) continue;
        if (c.a == 255) {
          p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
          continue;
        }
        const int a = c.a;
        p[0] = (unsigned char)((c.r * a + p[0] * (255 - a)) / 255);
        p[1] = (unsigned char)((c.g * a + p[1] * (255 - a)) / 255);
        p[2] = (unsigned char)((c.b * a + p[2] * (255 - a)) / 255);
        p[3] = (unsigned char)(a + p[3] * (255 - a) / 255);
      }
    }
  }
}

std::vector<std::vector<std::vector<fv::PixelPoint>>> FillCases() {
  return {
      // plain convex
      {{{8, 8}, {56, 16}, {16, 56}}},
      // horizontal top and bottom edges — never cross a scanline
      {{{10, 10}, {50, 10}, {50, 40}, {10, 40}}},
      // self-intersecting: even-odd leaves a hole
      {{{32, 4}, {13, 60}, {60, 25}, {4, 25}, {51, 60}}},
      // two disjoint rings in one call
      {{{4, 4}, {28, 4}, {28, 28}, {4, 28}},
       {{36, 36}, {60, 36}, {60, 60}, {36, 60}}},
      // ring with a hole (outer + inner, even-odd)
      {{{4, 4}, {60, 4}, {60, 60}, {4, 60}},
       {{20, 20}, {44, 20}, {44, 44}, {20, 44}}},
      // hangs off the top and the bottom: edges clamped into the drawn range
      {{{-30, -40}, {90, -40}, {70, 120}, {-10, 120}}},
      // entirely above the canvas
      {{{10, -80}, {50, -80}, {30, -40}}},
      // a sliver one scanline tall
      {{{5, 30}, {58, 30}, {58, 31}, {5, 31}}},
      // degenerate: all points on one scanline, no crossings at all
      {{{5, 20}, {40, 20}, {58, 20}}},
  };
}

TEST(CpuCanvas, EdgeTableFillMatchesTheNaiveScanline) {
  int case_index = 0;
  for (const auto& rings : FillCases()) {
    for (const fv::FvColor& color :
         {Rgb(255, 0, 0), fv::FvColor{0, 128, 255, 96}}) {
      fv::CpuCanvas c(64, 64);
      c.Clear(Rgb(20, 30, 40));
      fv::PixelBuffer want(64, 64);
      for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
          unsigned char* p = want.Row(y) + 4 * x;
          p[0] = 20; p[1] = 30; p[2] = 40; p[3] = 255;
        }

      fv::Brush brush{color};
      c.DrawPolyPolygon(rings, &brush, nullptr);  // may reject; oracle agrees
      ReferenceFill(&want, rings, color);

      for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
          const unsigned char* g = Px(c.Buffer(), x, y);
          const unsigned char* w = want.Row(y) + 4 * x;
          ASSERT_EQ(g[0], w[0]) << "case " << case_index << " at " << x << ","
                                << y;
          ASSERT_EQ(g[1], w[1]);
          ASSERT_EQ(g[2], w[2]);
          ASSERT_EQ(g[3], w[3]);
        }
    }
    ++case_index;
  }
}

TEST(CpuCanvas, BlendExactness) {
  fv::CpuCanvas c(4, 4);
  c.Clear(Rgb(0, 200, 0));
  fv::PixelBuffer over(4, 4);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      unsigned char* p = over.Row(y) + 4 * x;
      p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 128;  // half red
    }
  ASSERT_TRUE(c.DrawPixmap(over, 0, 0).ok());
  const unsigned char* p = Px(c.Buffer(), 1, 1);
  EXPECT_EQ(p[0], (255 * 128) / 255);            // 128
  EXPECT_EQ(p[1], (200 * (255 - 128)) / 255);    // 99
}

TEST(CpuCanvas, DashedLineHasGaps) {
  fv::CpuCanvas c(64, 8);
  c.Clear(Rgb(0, 0, 0));
  fv::Pen dashed{Rgb(255, 255, 255), 1, {4, 4}};
  ASSERT_TRUE(c.DrawLines({{0, 4}, {63, 4}}, dashed).ok());
  int inked = 0;
  for (int x = 0; x < 64; ++x) inked += Px(c.Buffer(), x, 4)[0] ? 1 : 0;
  EXPECT_GT(inked, 24);
  EXPECT_LT(inked, 40);  // ~half on
  EXPECT_EQ(Px(c.Buffer(), 5, 4)[0], 0) << "first gap";
}

TEST(CpuCanvas, ArgumentErrors) {
  fv::CpuCanvas c(8, 8);
  fv::Pen pen{Rgb(1, 1, 1), 1, {}};
  EXPECT_EQ(c.DrawLines({{1, 1}}, pen).code, fv::kInvalidArg);
  fv::Brush b{Rgb(1, 1, 1)};
  EXPECT_EQ(c.DrawPolyPolygon({{{1, 1}, {2, 2}}}, &b, nullptr).code,
            fv::kInvalidArg);
  EXPECT_EQ(c.DrawRectangle({0, 0, 0, 5}, &b, nullptr).code, fv::kInvalidArg);
  fv::TextStyle ts;
  fv::PixelSize sz;
  EXPECT_EQ(c.GetTextExtent("x", ts, &sz).code, fv::kInvalidArg);  // no font
}

TEST(CpuCanvas, TextWithSystemFont) {
  // host-dependent rasterization: assert ink and extents, never hashes
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  std::string font;
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      font = f;
      break;
    }
  if (font.empty()) GTEST_SKIP() << "no known system TTF";

  fv::CpuCanvas c(128, 32);
  c.Clear(Rgb(0, 0, 0));
  fv::TextStyle ts;
  ts.font_path = font;
  ts.size = 16;
  ts.color = Rgb(255, 255, 255);
  fv::PixelSize ext;
  ASSERT_TRUE(c.GetTextExtent("Chart 42", ts, &ext).ok());
  EXPECT_GT(ext.width, 30);
  EXPECT_GT(ext.height, 8);
  ASSERT_TRUE(c.DrawTextString("Chart 42", 4, 20, ts).ok());
  long ink = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 128; ++x) ink += Px(c.Buffer(), x, y)[0] ? 1 : 0;
  EXPECT_GT(ink, 50) << "text should leave ink";
  WriteAndCheckPng(c.Buffer(), "canvas_text");
}

// Same font search as above; text tests skip rather than fail on a host with
// no TTF where this one looks.
std::string SystemFont() {
  const char* candidates[] = {
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  for (const char* f : candidates)
    if (FILE* fp = fopen(f, "rb")) {
      fclose(fp);
      return f;
    }
  return std::string();
}

// The ink's own bounding box, which is what a rotation is visible in. A hash
// would prove nothing about the ANGLE — the lesson F1/F2 paid for.
bool InkBounds(const fv::PixelBuffer& b, int* x0, int* y0, int* x1, int* y1) {
  bool any = false;
  for (int y = 0; y < b.Height(); ++y)
    for (int x = 0; x < b.Width(); ++x) {
      if (Px(b, x, y)[0] == 0) continue;
      if (!any) {
        *x0 = *x1 = x;
        *y0 = *y1 = y;
        any = true;
        continue;
      }
      *x0 = std::min(*x0, x);
      *x1 = std::max(*x1, x);
      *y0 = std::min(*y0, y);
      *y1 = std::max(*y1, y);
    }
  return any;
}

TEST(CpuCanvas, RotatedTextTurnsTheStringOnTheScreen) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  fv::TextStyle ts;
  ts.font_path = font;
  ts.size = 16;
  ts.color = Rgb(255, 255, 255);

  // Horizontal: a wide, short box. Quarter turn: the SAME string standing up,
  // so the box is tall and narrow. Asserting the aspect ratio flips is the
  // directional check; the ink count says the glyphs survived the resampling.
  fv::CpuCanvas flat(160, 160), turned(160, 160);
  flat.Clear(Rgb(0, 0, 0));
  turned.Clear(Rgb(0, 0, 0));
  ASSERT_TRUE(flat.DrawRotatedTextString("Main Street", 20, 100, 0.0, ts).ok());
  ASSERT_TRUE(
      turned.DrawRotatedTextString("Main Street", 100, 140, M_PI / 2, ts).ok());

  int fx0, fy0, fx1, fy1, tx0, ty0, tx1, ty1;
  ASSERT_TRUE(InkBounds(flat.Buffer(), &fx0, &fy0, &fx1, &fy1));
  ASSERT_TRUE(InkBounds(turned.Buffer(), &tx0, &ty0, &tx1, &ty1));
  EXPECT_GT(fx1 - fx0, fy1 - fy0) << "horizontal text should be wide";
  EXPECT_GT(ty1 - ty0, tx1 - tx0) << "text at +90 should stand up";
  // A quarter turn swaps the extents, near enough to prove it is the same run.
  EXPECT_NEAR(ty1 - ty0, fx1 - fx0, 3);
  EXPECT_NEAR(tx1 - tx0, fy1 - fy0, 3);
  // +90 is COUNTERCLOCKWISE on screen: from the (100, 140) origin the text
  // runs UP the canvas, i.e. to smaller y, and stays near its origin column.
  EXPECT_LT(ty0, 140);
  EXPECT_NEAR(tx0, 100, 20);
}

TEST(CpuCanvas, RotatedTextAtZeroIsTheUprightPathExactly) {
  const std::string font = SystemFont();
  if (font.empty()) GTEST_SKIP() << "no known system TTF";
  fv::TextStyle ts;
  ts.font_path = font;
  ts.size = 14;
  ts.color = Rgb(255, 255, 255);

  // Pinned because every existing golden with text in it depends on it: a
  // zero angle must take the ORIGINAL glyph path, not a resampled copy of it.
  fv::CpuCanvas a(128, 32), b(128, 32);
  a.Clear(Rgb(0, 0, 0));
  b.Clear(Rgb(0, 0, 0));
  ASSERT_TRUE(a.DrawTextString("Chart 42", 4, 20, ts).ok());
  ASSERT_TRUE(b.DrawRotatedTextString("Chart 42", 4.0, 20.0, 0.0, ts).ok());
  EXPECT_EQ(memcmp(a.Buffer().Row(0), b.Buffer().Row(0),
                   (size_t)a.Buffer().Height() * a.Buffer().Width() * 4),
            0);
}

TEST(CpuCanvas, ADefaultCanvasDrawsRotatedTextUpright) {
  // ICanvas::DrawRotatedTextString is not pure: a backend that has no rotated
  // text (pyfvw's Python subclasses) must still put the string down. This
  // pins the fallback by calling it through the base class on a canvas that
  // does NOT override it.
  struct PlainCanvas : fv::ICanvas {
    fv::PixelSize Size() const override { return {0, 0}; }
    void Clear(const fv::FvColor&) override {}
    fv::Status DrawLines(const std::vector<fv::PixelPoint>&,
                         const fv::Pen&) override { return fv::Status::Ok(); }
    fv::Status DrawPolyPolygon(const std::vector<std::vector<fv::PixelPoint>>&,
                               const fv::Brush*, const fv::Pen*) override {
      return fv::Status::Ok();
    }
    fv::Status DrawRectangle(const fv::PixelRect&, const fv::Brush*,
                             const fv::Pen*) override { return fv::Status::Ok(); }
    fv::Status DrawEllipse(const fv::PixelRect&, const fv::Brush*,
                           const fv::Pen*) override { return fv::Status::Ok(); }
    fv::Status DrawPixmap(const fv::PixelBuffer&, int, int) override {
      return fv::Status::Ok();
    }
    fv::Status DrawTextString(const std::string& s, int x, int y,
                              const fv::TextStyle&) override {
      text = s;
      px = x;
      py = y;
      return fv::Status::Ok();
    }
    fv::Status GetTextExtent(const std::string&, const fv::TextStyle&,
                             fv::PixelSize*) override {
      return fv::Status::Ok();
    }
    std::string text;
    int px = 0, py = 0;
  } plain;

  fv::TextStyle ts;
  ASSERT_TRUE(plain.DrawRotatedTextString("A", 10.4, -3.4, 1.0, ts).ok());
  EXPECT_EQ(plain.text, "A");
  EXPECT_EQ(plain.px, 10);
  EXPECT_EQ(plain.py, -3);
}

}  // namespace
