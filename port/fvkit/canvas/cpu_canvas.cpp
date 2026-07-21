// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

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
  int a = c.a;                            // src-over, non-premultiplied
  p[0] = (unsigned char)((c.r * a + p[0] * (255 - a)) / 255);
  p[1] = (unsigned char)((c.g * a + p[1] * (255 - a)) / 255);
  p[2] = (unsigned char)((c.b * a + p[2] * (255 - a)) / 255);
  p[3] = (unsigned char)(a + p[3] * (255 - a) / 255);
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

  // dash bookkeeping continues across segments
  long dash_total = 0;
  for (int d : pen.dash) dash_total += d;
  long travelled = 0;

  auto inked = [&](long dist) {
    if (pen.dash.empty() || dash_total <= 0) return true;
    long m = dist % dash_total;
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

  std::vector<double> xs;
  for (int y = y_min; y <= y_max; ++y) {
    xs.clear();
    double yc = y + 0.5;  // sample scanline at pixel centers
    for (const auto& ring : rings) {
      size_t n = ring.size();
      if (n < 3) continue;
      for (size_t i = 0; i < n; ++i) {
        const PixelPoint& a = ring[i];
        const PixelPoint& b = ring[(i + 1) % n];  // implicit closure
        if ((a.y <= yc) == (b.y <= yc)) continue;  // no crossing
        double t = (yc - a.y) / (double)(b.y - a.y);
        xs.push_back(a.x + t * (b.x - a.x));
      }
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {  // even-odd pairs
      int x0 = (int)std::ceil(xs[i] - 0.5);
      int x1 = (int)std::floor(xs[i + 1] - 0.5);
      for (int x = std::max(x0, 0); x <= std::min(x1, buf_.Width() - 1); ++x)
        BlendPixel(x, y, c);
    }
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

Status CpuCanvas::DrawTextString(const std::string& utf8, int x, int y,
                                 const TextStyle& style) {
  Status s;
  FontEntry* font = LoadFont(style.font_path, &s);
  if (font == nullptr) return s;

  float scale = stbtt_ScaleForPixelHeight(&font->info, (float)style.size);
  double pen_x = x;
  // ASCII subset is all GeoSym labels need for now; document and move on.
  for (unsigned char ch : utf8) {
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
  for (unsigned char ch : utf8) {
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, ch, &advance, &lsb);
    width += advance * scale;
  }
  out->width = (int)std::lround(width);
  out->height = (int)std::lround((ascent - descent) * scale);
  return Status::Ok();
}

}  // namespace fv
