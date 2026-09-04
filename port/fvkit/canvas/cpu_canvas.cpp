// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// CpuCanvas implementation — see fvkit/canvas/cpu_canvas.h.

#include "fvkit/canvas/cpu_canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace fv {

struct CpuCanvas::FontEntry {
  std::vector<unsigned char> data;
  stbtt_fontinfo info;
};

CpuCanvas::CpuCanvas(int width, int height) : buf_(width, height) {
  Clear(FvColor{0, 0, 0, 255});
}
CpuCanvas::~CpuCanvas() = default;

PixelSize CpuCanvas::Size() const { return {buf_.Width(), buf_.Height()}; }

void CpuCanvas::Clear(const FvColor& c) {
  for (int y = 0; y < buf_.Height(); ++y) {
    unsigned char* row = buf_.Row(y);
    for (int x = 0; x < buf_.Width(); ++x) {
      row[4 * x + 0] = c.r;
      row[4 * x + 1] = c.g;
      row[4 * x + 2] = c.b;
      row[4 * x + 3] = c.a;
    }
  }
}

void CpuCanvas::BlendPixel(int x, int y, const FvColor& c) {
  if (x < 0 || y < 0 || x >= buf_.Width() || y >= buf_.Height() || c.a == 0)
    return;
  unsigned char* p = buf_.Row(y) + 4 * x;
  if (c.a == 255) {
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = 255;
    return;
  }
  const int a = c.a;                      // src-over, non-premultiplied
  const int inv = 255 - a;
  if (p[3] == 255) {
    // THE OPAQUE DESTINATION, UNCHANGED. Every canvas in the tree before P18
    // was cleared opaque and stayed opaque, so this is the arithmetic every
    // pinned golden was made with, in the same order. It is kept as its own
    // branch rather than folded into the general case below — the two agree
    // to the byte when p[3] is 255, and a branch says so structurally instead
    // of asking a reader to verify the algebra.
    p[0] = (unsigned char)((c.r * a + p[0] * inv) / 255);
    p[1] = (unsigned char)((c.g * a + p[1] * inv) / 255);
    p[2] = (unsigned char)((c.b * a + p[2] * inv) / 255);
    p[3] = 255;
    return;
  }

  // A TRANSLUCENT DESTINATION — which is what a canvas being used as a LAYER
  // is (P18: the overlay is drawn on its own transparent surface and
  // composited over the cached base map).
  //
  // The formula above is wrong here and wrong in a way that looks like a
  // rendering bug rather than a blending one. These bytes are STRAIGHT
  // (non-premultiplied) colour, so weighting the source by its own alpha
  // against a destination that contributes nothing drags every antialiased
  // edge toward the cleared colour: a half-covered white glyph pixel over
  // transparent black comes out mid-grey at alpha 128, and CoreGraphics —
  // told the buffer is `kCGImageAlphaLast` — then draws exactly that grey.
  // Symbols and text get a dark fringe; the map underneath is innocent.
  //
  // So the destination is weighted by ITS OWN alpha too, and the result is
  // divided back out of the composite alpha to return to straight colour.
  // Everything is kept in 1/255 units (`num` is the composite alpha times
  // 255) so no intermediate is rounded before the division.
  //
  // The divisor cannot be zero: a fully transparent SOURCE returned at the
  // top of this function, so `a` is at least 1 and `num` at least 255. There
  // is deliberately no guard for a case that cannot arrive.
  const int num = a * 255 + p[3] * inv;  // composite alpha * 255
  const int dw = p[3] * inv;             // the destination's weight, same units
  p[0] = (unsigned char)((c.r * a * 255 + p[0] * dw) / num);
  p[1] = (unsigned char)((c.g * a * 255 + p[1] * dw) / num);
  p[2] = (unsigned char)((c.b * a * 255 + p[2] * dw) / num);
  p[3] = (unsigned char)(num / 255);
}

void CpuCanvas::Stamp(int x, int y, const Pen& pen) {
  // square nib centered on (x, y); width 1 = single pixel
  int half_lo = (pen.width - 1) / 2, half_hi = pen.width / 2;
  for (int dy = -half_lo; dy <= half_hi; ++dy)
    for (int dx = -half_lo; dx <= half_hi; ++dx)
      BlendPixel(x + dx, y + dy, pen.color);
}

Status CpuCanvas::DrawLines(const std::vector<PixelPoint>& pts, const Pen& pen) {
  if (pts.size() < 2) return Status::Error(kInvalidArg, "need >= 2 points");
  if (pen.width < 1) return Status::Error(kInvalidArg, "pen width < 1");

  // Dash bookkeeping continues across segments. This measures the step count
  // of a Bresenham walk, which is the Chebyshev distance, not the arc length —
  // a diagonal run comes out sqrt(2) long — and it restarts at whatever point
  // the caller happened to hand in. Callers that need a dash to hold still
  // under a pan, or to line up with a second pass at another width, place the
  // runs themselves against the unclipped path (VectorRenderer::DashRuns +
  // PlaceAlongPath) and stroke the pieces with a solid pen.
  double dash_total = 0.0;
  for (double d : pen.dash) dash_total += d;
  long travelled = 0;

  auto inked = [&](long dist) {
    if (pen.dash.empty() || !(dash_total > 0.0)) return true;
    double m = std::fmod(static_cast<double>(dist), dash_total);
    for (size_t i = 0; i < pen.dash.size(); ++i) {
      if (m < pen.dash[i]) return i % 2 == 0;  // even runs are "on"
      m -= pen.dash[i];
    }
    return true;
  };

  for (size_t s = 0; s + 1 < pts.size(); ++s) {
    int x0 = pts[s].x, y0 = pts[s].y, x1 = pts[s + 1].x, y1 = pts[s + 1].y;
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      if (inked(travelled)) Stamp(x0, y0, pen);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
      ++travelled;  // step count approximates arc length (non-AA nib)
    }
  }
  return Status::Ok();
}

// Even-odd scanline fill = GDI's ALTERNATE mode.
//
// PERFORMANCE (R3b). The straightforward version walked EVERY edge of every
// ring for EVERY scanline in the bounding box — O(scanlines * edges), which on
// a DNC depth area (thousands of vertices over hundreds of scanlines) is
// millions of crossing tests to produce a few thousand spans, and measured as
// the single largest cost in a vector frame. This is the classic edge table:
// each edge is bucketed by the first scanline it can cross and retired after
// its last, so a scanline only visits the edges that actually span it.
//
// BIT-FAITHFUL. The crossing test and the x it produces are unchanged, down to
// the order of the arithmetic: `x` is recomputed from the edge's own endpoints
// on every scanline rather than stepped by a dx/dy increment, because an
// incremental x accumulates floating-point error and would move pixels. The
// scanline's active set is a different ORDER than the old ring-by-ring walk,
// but the crossings are then sorted, and a sorted sequence of doubles does not
// depend on the order they arrived in. Output is byte-identical.
void CpuCanvas::FillScanlines(const std::vector<std::vector<PixelPoint>>& rings,
                              const FvColor& c) {
  int y_min = buf_.Height(), y_max = -1;
  for (const auto& ring : rings)
    for (const auto& p : ring) {
      y_min = std::min(y_min, p.y);
      y_max = std::max(y_max, p.y);
    }
  y_min = std::max(y_min, 0);
  y_max = std::min(y_max, buf_.Height() - 1);
  // Nothing to draw into. Checked up front because the span loop below
  // resolves a row pointer per scanline, where the old per-pixel BlendPixel
  // would have bounds-checked its way out of a zero-width buffer.
  if (y_max < y_min || buf_.Width() <= 0) return;

  // An edge crosses scanline y exactly when (a.y <= y+0.5) != (b.y <= y+0.5);
  // both endpoints are integers, so that is min(a.y,b.y) <= y < max(a.y,b.y) —
  // a half-open span of scanlines, which is what makes bucketing exact.
  struct Edge {
    int ax, ay, bx, by;
    int y_last;  // inclusive
  };
  std::vector<Edge> edges;
  for (const auto& ring : rings) {
    const size_t n = ring.size();
    if (n < 3) continue;
    edges.reserve(edges.size() + n);
    for (size_t i = 0; i < n; ++i) {
      const PixelPoint& a = ring[i];
      const PixelPoint& b = ring[(i + 1) % n];  // implicit closure
      if (a.y == b.y) continue;                 // never crosses
      Edge e;
      e.ax = a.x;
      e.ay = a.y;
      e.bx = b.x;
      e.by = b.y;
      e.y_last = std::max(a.y, b.y) - 1;
      if (e.y_last < y_min) continue;
      const int y_first = std::min(a.y, b.y);
      if (y_first > y_max) continue;
      edges.push_back(e);
    }
  }
  if (edges.empty()) return;

  // Bucket by first crossed scanline (clamped into the drawn range, so an edge
  // that starts above the canvas becomes active on the first row).
  const size_t rows = static_cast<size_t>(y_max - y_min) + 1;
  std::vector<uint32_t> bucket_head(rows, UINT32_MAX);
  std::vector<uint32_t> bucket_next(edges.size(), UINT32_MAX);
  for (size_t i = 0; i < edges.size(); ++i) {
    const int y_first =
        std::max(std::min(edges[i].ay, edges[i].by), y_min) - y_min;
    bucket_next[i] = bucket_head[static_cast<size_t>(y_first)];
    bucket_head[static_cast<size_t>(y_first)] = static_cast<uint32_t>(i);
  }

  std::vector<uint32_t> active;
  std::vector<double> xs;
  for (int y = y_min; y <= y_max; ++y) {
    for (uint32_t i = bucket_head[static_cast<size_t>(y - y_min)];
         i != UINT32_MAX; i = bucket_next[i])
      active.push_back(i);
    active.erase(std::remove_if(active.begin(), active.end(),
                                [&](uint32_t i) {
                                  return edges[i].y_last < y;
                                }),
                 active.end());
    if (active.empty()) continue;

    const double yc = y + 0.5;  // sample scanline at pixel centers
    xs.clear();
    for (uint32_t i : active) {
      const Edge& e = edges[i];
      const double t = (yc - e.ay) / (double)(e.by - e.ay);
      xs.push_back(e.ax + t * (e.bx - e.ax));
    }
    std::sort(xs.begin(), xs.end());

    unsigned char* row = buf_.Row(y);
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {  // even-odd pairs
      const int x0 = std::max((int)std::ceil(xs[i] - 0.5), 0);
      const int x1 = std::min((int)std::floor(xs[i + 1] - 0.5),
                              buf_.Width() - 1);
      if (x1 < x0) continue;
      BlendSpan(row, x0, x1, c);
    }
  }
}

// One horizontal run of BlendPixel, hoisting the per-pixel bounds check and
// Row() lookup out of the loop. x0/x1 are already clamped and y is inside the
// canvas, which is exactly what BlendPixel's guard would have tested.
void CpuCanvas::BlendSpan(unsigned char* row, int x0, int x1, const FvColor& c) {
  if (c.a == 0) return;
  unsigned char* p = row + 4 * x0;
  if (c.a == 255) {
    for (int x = x0; x <= x1; ++x, p += 4) {
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = 255;
    }
    return;
  }
  const int a = c.a;  // src-over, non-premultiplied — same as BlendPixel
  for (int x = x0; x <= x1; ++x, p += 4) {
    p[0] = (unsigned char)((c.r * a + p[0] * (255 - a)) / 255);
    p[1] = (unsigned char)((c.g * a + p[1] * (255 - a)) / 255);
    p[2] = (unsigned char)((c.b * a + p[2] * (255 - a)) / 255);
    p[3] = (unsigned char)(a + p[3] * (255 - a) / 255);
  }
}

Status CpuCanvas::DrawPolyPolygon(
    const std::vector<std::vector<PixelPoint>>& rings, const Brush* fill,
    const Pen* outline) {
  if (rings.empty()) return Status::Error(kInvalidArg, "no rings");
  for (const auto& r : rings)
    if (r.size() < 3) return Status::Error(kInvalidArg, "ring with < 3 points");

  if (fill != nullptr) FillScanlines(rings, fill->color);
  if (outline != nullptr) {
    for (const auto& r : rings) {
      std::vector<PixelPoint> closed = r;
      closed.push_back(r.front());
      Status s = DrawLines(closed, *outline);
      if (!s.ok()) return s;
    }
  }
  return Status::Ok();
}

Status CpuCanvas::DrawRectangle(const PixelRect& r, const Brush* fill,
                                const Pen* outline) {
  if (r.width <= 0 || r.height <= 0)
    return Status::Error(kInvalidArg, "empty rectangle");
  std::vector<std::vector<PixelPoint>> ring{{{r.x, r.y},
                                             {r.x + r.width - 1, r.y},
                                             {r.x + r.width - 1, r.y + r.height - 1},
                                             {r.x, r.y + r.height - 1}}};
  return DrawPolyPolygon(ring, fill, outline);
}

Status CpuCanvas::DrawEllipse(const PixelRect& bbox, const Brush* fill,
                              const Pen* outline) {
  if (bbox.width <= 0 || bbox.height <= 0)
    return Status::Error(kInvalidArg, "empty ellipse bbox");
  // 64-segment polygon approximation, consistent fill/outline everywhere
  const int kSegs = 64;
  double cx = bbox.x + (bbox.width - 1) / 2.0;
  double cy = bbox.y + (bbox.height - 1) / 2.0;
  double rx = (bbox.width - 1) / 2.0, ry = (bbox.height - 1) / 2.0;
  std::vector<PixelPoint> ring;
  ring.reserve(kSegs);
  for (int i = 0; i < kSegs; ++i) {
    double t = 2.0 * M_PI * i / kSegs;
    ring.push_back({(int)std::lround(cx + rx * std::cos(t)),
                    (int)std::lround(cy + ry * std::sin(t))});
  }
  return DrawPolyPolygon({ring}, fill, outline);
}

Status CpuCanvas::DrawPixmap(const PixelBuffer& src, int x, int y) {
  if (src.Empty()) return Status::Error(kInvalidArg, "empty pixmap");
  for (int sy = 0; sy < src.Height(); ++sy) {
    int ty = y + sy;
    if (ty < 0 || ty >= buf_.Height()) continue;
    const unsigned char* srow = src.Row(sy);
    for (int sx = 0; sx < src.Width(); ++sx) {
      int tx = x + sx;
      if (tx < 0 || tx >= buf_.Width()) continue;
      const unsigned char* p = srow + 4 * sx;
      BlendPixel(tx, ty, FvColor{p[0], p[1], p[2], p[3]});
    }
  }
  return Status::Ok();
}

CpuCanvas::FontEntry* CpuCanvas::LoadFont(const std::string& path,
                                          Status* status) {
  const std::string& key = path.empty() ? default_font_ : path;
  if (key.empty()) {
    *status = Status::Error(kInvalidArg,
                            "no font: TextStyle.font_path empty and no "
                            "SetDefaultFont");
    return nullptr;
  }
  auto it = fonts_.find(key);
  if (it != fonts_.end()) return it->second.get();

  FILE* f = std::fopen(key.c_str(), "rb");
  if (f == nullptr) {
    *status = Status::Error(kNotFound, "font not found: " + key);
    return nullptr;
  }
  auto entry = std::make_unique<FontEntry>();
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  entry->data.resize((size_t)n);
  size_t rd = std::fread(entry->data.data(), 1, (size_t)n, f);
  std::fclose(f);
  if (rd != (size_t)n ||
      !stbtt_InitFont(&entry->info, entry->data.data(),
                      stbtt_GetFontOffsetForIndex(entry->data.data(), 0))) {
    *status = Status::Error(kUnsupported, "font not parseable: " + key);
    return nullptr;
  }
  FontEntry* raw = entry.get();
  fonts_[key] = std::move(entry);
  *status = Status::Ok();
  return raw;
}

Status CpuCanvas::SetDefaultFont(const std::string& font_path) {
  Status s;
  if (LoadFont(font_path, &s) == nullptr) return s;
  default_font_ = font_path;
  return Status::Ok();
}

namespace {

// UTF-8 -> code points, so `utf8` means what ICanvas says it means.
//
// Every glyph loop below used to walk BYTES, with a comment saying the ASCII
// subset was all GeoSym needed. It was, until the graticule wanted a degree
// sign: U+00B0 is two bytes, and a byte walk drew both of them ("35A°"). An
// ASCII string decodes to exactly the same code points it did before, so no
// pinned golden in the tree moves -- the only strings whose rendering changes
// are the ones that were already wrong.
//
// Malformed input yields U+FFFD and advances one byte, which keeps the loop
// finite on any bytes at all; a font without the replacement glyph then draws
// nothing for it, which is the right amount of noise for bad text.
int NextCodepoint(const std::string& s, size_t* i) {
  const unsigned char c0 = static_cast<unsigned char>(s[*i]);
  auto cont = [&s](size_t k) {
    return k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80;
  };
  auto bits = [&s](size_t k) {
    return static_cast<int>(static_cast<unsigned char>(s[k]) & 0x3F);
  };
  if (c0 < 0x80) {
    *i += 1;
    return c0;
  }
  if ((c0 & 0xE0) == 0xC0 && cont(*i + 1)) {
    const int cp = ((c0 & 0x1F) << 6) | bits(*i + 1);
    *i += 2;
    return cp;
  }
  if ((c0 & 0xF0) == 0xE0 && cont(*i + 1) && cont(*i + 2)) {
    const int cp = ((c0 & 0x0F) << 12) | (bits(*i + 1) << 6) | bits(*i + 2);
    *i += 3;
    return cp;
  }
  if ((c0 & 0xF8) == 0xF0 && cont(*i + 1) && cont(*i + 2) && cont(*i + 3)) {
    const int cp = ((c0 & 0x07) << 18) | (bits(*i + 1) << 12) |
                   (bits(*i + 2) << 6) | bits(*i + 3);
    *i += 4;
    return cp;
  }
  *i += 1;
  return 0xFFFD;
}

}  // namespace

Status CpuCanvas::DrawTextString(const std::string& utf8, int x, int y,
                                 const TextStyle& style) {
  Status s;
  FontEntry* font = LoadFont(style.font_path, &s);
  if (font == nullptr) return s;

  float scale = stbtt_ScaleForPixelHeight(&font->info, (float)style.size);
  double pen_x = x;
  for (size_t i = 0; i < utf8.size();) {
    const int ch = NextCodepoint(utf8, &i);
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bmp = stbtt_GetCodepointBitmap(&font->info, scale, scale,
                                                  ch, &w, &h, &xoff, &yoff);
    if (bmp != nullptr) {
      for (int gy = 0; gy < h; ++gy)
        for (int gx = 0; gx < w; ++gx) {
          unsigned char cov = bmp[gy * w + gx];
          if (cov == 0) continue;
          FvColor c = style.color;
          c.a = (unsigned char)(c.a * cov / 255);
          BlendPixel((int)pen_x + xoff + gx, y + yoff + gy, c);
        }
      stbtt_FreeBitmap(bmp, nullptr);
    }
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, ch, &advance, &lsb);
    pen_x += advance * scale;
  }
  return Status::Ok();
}

// Rotated text.
//
// The glyph is rasterized UPRIGHT by stb at the requested pixel height and
// then resampled into the canvas through the inverse rotation, rather than
// asking stb for a transformed outline: stb_truetype has no outline transform,
// and rasterizing a rotated outline by hand would mean a second rasterizer
// whose antialiasing did not match the upright one. Resampling keeps ONE
// source of glyph coverage, which is what makes a label look the same whether
// it lands on a straight road or a curved one.
//
// The cost of that choice is one bilinear filter's worth of softness on a
// rotated glyph. At label sizes it is not visible; if it ever is, the fix is a
// supersampled glyph bitmap here, not a second rasterizer.
//
// A zero angle takes the upright path EXACTLY, so every already-pinned golden
// that draws horizontal text is untouched by this function existing.
Status CpuCanvas::DrawRotatedTextString(const std::string& utf8, double x,
                                        double y, double angle_rad,
                                        const TextStyle& style) {
  const double ca = std::cos(angle_rad), sa = std::sin(angle_rad);
  if (std::fabs(sa) < 1e-12 && ca > 0.0) {
    return DrawTextString(utf8, (int)std::lround(x), (int)std::lround(y),
                          style);
  }
  Status s;
  FontEntry* font = LoadFont(style.font_path, &s);
  if (font == nullptr) return s;

  // Text-local (u along the baseline, v up) -> screen. See the convention on
  // ICanvas::DrawRotatedTextString: e_u = (cos a, -sin a), e_v = (-sin a, -cos a).
  const float scale = stbtt_ScaleForPixelHeight(&font->info, (float)style.size);
  double pen_u = 0.0;
  for (size_t i = 0; i < utf8.size();) {
    const int ch = NextCodepoint(utf8, &i);
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bmp = stbtt_GetCodepointBitmap(&font->info, scale, scale, ch,
                                                  &w, &h, &xoff, &yoff);
    if (bmp != nullptr) {
      // The glyph bitmap occupies u in [u0, u0+w), and lies yoff..yoff+h BELOW
      // the baseline, i.e. v in (-(yoff+h), -yoff].
      const double u0 = pen_u + xoff, v0 = -(double)yoff;
      double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
      for (int c = 0; c < 4; ++c) {
        const double u = u0 + ((c & 1) ? w : 0);
        const double v = v0 - ((c & 2) ? h : 0);
        const double sx = x + u * ca - v * sa;
        const double sy = y - u * sa - v * ca;
        minx = (std::min)(minx, sx); maxx = (std::max)(maxx, sx);
        miny = (std::min)(miny, sy); maxy = (std::max)(maxy, sy);
      }
      int x0 = (int)std::floor(minx) - 1, x1 = (int)std::ceil(maxx) + 1;
      int y0 = (int)std::floor(miny) - 1, y1 = (int)std::ceil(maxy) + 1;
      x0 = (std::max)(x0, 0); y0 = (std::max)(y0, 0);
      x1 = (std::min)(x1, buf_.Width() - 1);
      y1 = (std::min)(y1, buf_.Height() - 1);
      for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
          // Inverse of the map above: the basis is orthonormal, so it is a
          // dot product with each axis.
          const double dx = px + 0.5 - x, dy = py + 0.5 - y;
          const double u = dx * ca - dy * sa;
          const double v = -dx * sa - dy * ca;
          // Continuous glyph-bitmap coordinates, then bilinear on centres.
          const double fx = (u - u0) - 0.5, fy = (v0 - v) - 0.5;
          const int gx = (int)std::floor(fx), gy = (int)std::floor(fy);
          const double tx = fx - gx, ty = fy - gy;
          auto at = [&](int ix, int iy) -> double {
            if (ix < 0 || iy < 0 || ix >= w || iy >= h) return 0.0;
            return bmp[iy * w + ix];
          };
          const double cov = at(gx, gy) * (1 - tx) * (1 - ty) +
                             at(gx + 1, gy) * tx * (1 - ty) +
                             at(gx, gy + 1) * (1 - tx) * ty +
                             at(gx + 1, gy + 1) * tx * ty;
          if (cov < 0.5) continue;
          FvColor c = style.color;
          c.a = (unsigned char)std::lround(c.a * (std::min)(cov, 255.0) / 255.0);
          if (c.a == 0) continue;
          BlendPixel(px, py, c);
        }
      }
      stbtt_FreeBitmap(bmp, nullptr);
    }
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, ch, &advance, &lsb);
    pen_u += advance * scale;
  }
  return Status::Ok();
}

Status CpuCanvas::GetTextExtent(const std::string& utf8, const TextStyle& style,
                                PixelSize* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  Status s;
  FontEntry* font = LoadFont(style.font_path, &s);
  if (font == nullptr) return s;

  float scale = stbtt_ScaleForPixelHeight(&font->info, (float)style.size);
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
  double width = 0;
  for (size_t i = 0; i < utf8.size();) {
    const int ch = NextCodepoint(utf8, &i);
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, ch, &advance, &lsb);
    width += advance * scale;
  }
  out->width = (int)std::lround(width);
  out->height = (int)std::lround((ascent - descent) * scale);
  return Status::Ok();
}

}  // namespace fv
