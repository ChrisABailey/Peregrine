// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Extracted verbatim from vector/renderer.cpp's anonymous namespace (G2).
// See symbol_draw.h for why. The only edit is IStyleEngine -> ISymbolLibrary
// and the pixel_ratio division in DrawResolvedSymbol.

#include "fvkit/vector/symbol_draw.h"

#include <cstring>
#include <vector>

#include "fvkit/vector/renderer.h"  // ClipPolyline / ClipPolygon

namespace fv {

void DrawSymbolAt(ICanvas* canvas, const VectorSymbol& sym, double ax,
                  double ay, double px_per_himetric, double rotation_rad,
                  InkBox* ink, const FvColor* tint) {
  const double cs = std::cos(rotation_rad), sn = std::sin(rotation_rad);
  auto map = [&](const SymbolPoint& p) {
    // Rotate in symbol space (y up), then scale and flip to screen.
    const double rx = p.x * cs - p.y * sn;
    const double ry = p.x * sn + p.y * cs;
    const SurfacePoint sp{ax + rx * px_per_himetric,
                          ay - ry * px_per_himetric};
    if (ink != nullptr) ink->Add(sp.x, sp.y);
    return sp;
  };

  for (const SymbolPrimitive& prim : sym.primitives) {
    Pen pen;
    pen.color = tint != nullptr ? *tint : prim.stroke_color;
    pen.width = std::max(1, static_cast<int>(std::lround(prim.stroke_width *
                                                        px_per_himetric)));
    Brush brush;
    brush.color = tint != nullptr ? *tint : prim.fill_color;

    switch (prim.type) {
      case SymbolPrimitiveType::kPolyline: {
        if (prim.points.size() < 2 || !prim.has_stroke) break;
        std::vector<SurfacePoint> pts;
        pts.reserve(prim.points.size());
        for (const SymbolPoint& p : prim.points) pts.push_back(map(p));
        const PixelSize size = canvas->Size();
        for (auto& run : ClipPolyline(pts, size.width, size.height))
          canvas->DrawLines(run, pen);
        break;
      }
      case SymbolPrimitiveType::kPolygon: {
        if (prim.points.size() < 3) break;
        std::vector<SurfacePoint> pts;
        pts.reserve(prim.points.size());
        for (const SymbolPoint& p : prim.points) pts.push_back(map(p));
        const PixelSize size = canvas->Size();
        std::vector<PixelPoint> ring =
            ClipPolygon(pts, size.width, size.height);
        if (ring.size() < 3) break;
        std::vector<std::vector<PixelPoint>> rings{std::move(ring)};
        canvas->DrawPolyPolygon(rings, prim.has_fill ? &brush : nullptr,
                                prim.has_stroke ? &pen : nullptr);
        break;
      }
      case SymbolPrimitiveType::kEllipse: {
        // The conjugate radius vectors reduce to an axis-aligned box only
        // when they are axis-aligned themselves; GeoSym's are (they encode
        // circles and axis-aligned ellipses). A rotated one degrades to its
        // bounding box — documented, revisit if a symbol needs it.
        const SurfacePoint c = map(prim.center);
        const double rx = std::hypot(prim.radius1.x, prim.radius1.y) *
                          px_per_himetric;
        const double ry = std::hypot(prim.radius2.x, prim.radius2.y) *
                          px_per_himetric;
        PixelRect box;
        box.x = static_cast<int>(std::lround(c.x - rx));
        box.y = static_cast<int>(std::lround(c.y - ry));
        box.width = std::max(1, static_cast<int>(std::lround(2.0 * rx)));
        box.height = std::max(1, static_cast<int>(std::lround(2.0 * ry)));
        if (ink != nullptr) {  // map() only saw the centre
          ink->Add(box.x, box.y);
          ink->Add(box.x + box.width, box.y + box.height);
        }
        canvas->DrawEllipse(box, prim.has_fill ? &brush : nullptr,
                            prim.has_stroke ? &pen : nullptr);
        break;
      }
      case SymbolPrimitiveType::kText: {
        if (prim.text.empty()) break;
        const SurfacePoint p = map(prim.center);
        TextStyle ts;
        ts.size = prim.text_height * px_per_himetric;
        ts.color = tint != nullptr
                       ? *tint
                       : (prim.has_fill ? prim.fill_color : prim.stroke_color);
        if (ts.size >= 1.0) {
          canvas->DrawTextString(prim.text, static_cast<int>(std::lround(p.x)),
                                 static_cast<int>(std::lround(p.y)), ts);
          if (ink != nullptr) {
            PixelSize ext;
            if (canvas->GetTextExtent(prim.text, ts, &ext).ok())
              ink->Add(p.x + ext.width, p.y - ext.height);
          }
        }
        break;
      }
    }
  }
}

void DrawPixmapSymbolAt(ICanvas* canvas, const SymbolPixmap& sym, double ax,
                        double ay, double scale, double rotation_rad,
                        InkBox* ink, const FvColor* tint) {
  const int sw = sym.tile.Width(), sh = sym.tile.Height();
  if (sw <= 0 || sh <= 0) return;

  // G4. A tinted stamp is the tile's SHAPE in one flat colour: RGB replaced,
  // alpha kept (scaled by the tint's own, so a translucent highlight is
  // expressible). Done into a local copy the two paths below then treat as
  // the tile — the caller's tile is const and shared, and a library hands the
  // same one out to every stamp of that id.
  if (tint != nullptr) {
    SymbolPixmap recoloured;
    recoloured.pivot_x = sym.pivot_x;
    recoloured.pivot_y = sym.pivot_y;
    recoloured.pixel_ratio = sym.pixel_ratio;
    recoloured.tile = PixelBuffer(sw, sh);
    for (int y = 0; y < sh; ++y) {
      const unsigned char* src = sym.tile.Row(y);
      unsigned char* dst = recoloured.tile.Row(y);
      for (int x = 0; x < sw; ++x) {
        const unsigned a = src[x * 4 + 3];
        dst[x * 4 + 0] = tint->r;
        dst[x * 4 + 1] = tint->g;
        dst[x * 4 + 2] = tint->b;
        dst[x * 4 + 3] = static_cast<unsigned char>((a * tint->a + 127) / 255);
      }
    }
    DrawPixmapSymbolAt(canvas, recoloured, ax, ay, scale, rotation_rad, ink,
                       nullptr);
    return;
  }

  // The anchor is snapped to a whole pixel FIRST, for both paths. D4 puts a
  // pixel's centre ON the integer, so a symbol at a half-pixel anchor has no
  // "correct" sub-pixel placement to preserve without resampling — and
  // snapping is what makes the two paths agree: at scale 1 with no rotation
  // the resampler below reproduces the straight blit exactly instead of
  // shifting the glyph by a pixel as the zoom crosses 1.
  const double cx = std::round(ax), cy = std::round(ay);

  const bool plain = std::fabs(scale - 1.0) < 1e-6 &&
                     std::fabs(rotation_rad) < 1e-9;
  if (plain) {
    const int x = static_cast<int>(std::lround(cx - sym.pivot_x));
    const int y = static_cast<int>(std::lround(cy - sym.pivot_y));
    canvas->DrawPixmap(sym.tile, x, y);
    if (ink != nullptr) {
      ink->Add(x, y);
      ink->Add(x + sw, y + sh);
    }
    return;
  }
  if (!(scale > 0.0)) return;

  // Corners of the tile relative to the pivot, forward-mapped, to size the
  // destination. A pixel covers half a unit either side of its centre, so the
  // painted extent runs from -0.5 to size-0.5. Rotation is clockwise on screen
  // for a positive angle, the same sense DrawSymbolAt applies (its symbol
  // space is y up, this one is y down, hence the sign in the y row).
  const double cs = std::cos(rotation_rad), sn = std::sin(rotation_rad);
  auto fwd = [&](double sx, double sy, double* dx, double* dy) {
    const double px = (sx - sym.pivot_x) * scale;
    const double py = (sy - sym.pivot_y) * scale;
    *dx = px * cs + py * sn;
    *dy = -px * sn + py * cs;
  };
  double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
  const double corners[4][2] = {{-0.5, -0.5},
                                {sw - 0.5, -0.5},
                                {-0.5, sh - 0.5},
                                {sw - 0.5, sh - 0.5}};
  for (const auto& c : corners) {
    double dx = 0, dy = 0;
    fwd(c[0], c[1], &dx, &dy);
    x0 = std::min(x0, dx); x1 = std::max(x1, dx);
    y0 = std::min(y0, dy); y1 = std::max(y1, dy);
  }
  // One pixel of slack on each side: the destination grid is not aligned to
  // the rotated source grid, so a boundary sample can fall just outside a
  // tight box. Slack pixels that sample outside the tile stay transparent and
  // blit as nothing, which is cheaper than losing an edge row.
  const int ox = static_cast<int>(std::floor(cx + x0)) - 1;
  const int oy = static_cast<int>(std::floor(cy + y0)) - 1;
  const int dw = static_cast<int>(std::ceil(cx + x1)) - ox + 2;
  const int dh = static_cast<int>(std::ceil(cy + y1)) - oy + 2;
  if (dw <= 0 || dh <= 0) return;
  // A symbol scaled past this is a bug in the caller's units, not a symbol.
  if (dw > 4096 || dh > 4096) return;

  PixelBuffer dst(dw, dh);
  const double inv = 1.0 / scale;
  for (int y = 0; y < dh; ++y) {
    unsigned char* drow = dst.Row(y);
    const double ry = (oy + y) - cy;  // destination pixel centre, D4
    for (int x = 0; x < dw; ++x) {
      const double rx = (ox + x) - cx;
      // Inverse rotation (the transpose) then inverse scale, back to the
      // tile's own grid; nearest sample. floor(t + 0.5), NOT lround: they
      // differ at exactly -0.5, which is where a 2x upscale puts the first
      // column of the tile, and lround's round-half-away-from-zero drops it.
      const double px = rx * cs - ry * sn;
      const double py = rx * sn + ry * cs;
      const int sx =
          static_cast<int>(std::floor(px * inv + sym.pivot_x + 0.5));
      const int sy =
          static_cast<int>(std::floor(py * inv + sym.pivot_y + 0.5));
      if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) continue;
      std::memcpy(drow + x * 4, sym.tile.Row(sy) + sx * 4, 4);
    }
  }
  canvas->DrawPixmap(dst, ox, oy);
  if (ink != nullptr) {
    ink->Add(ox, oy);
    ink->Add(ox + dw, oy + dh);
  }
}

ResolvedSymbol ResolveSymbol(ISymbolLibrary* lib, const std::string& id) {
  ResolvedSymbol r;
  if (lib == nullptr) return r;
  const VectorSymbol* sym = lib->Symbol(id);
  if (sym != nullptr && !sym->primitives.empty()) {
    r.vec = sym;
    return r;
  }
  const SymbolPixmap* pix = lib->Pixmap(id);
  if (pix != nullptr && !pix->tile.Empty()) r.pix = pix;
  return r;
}

bool DrawResolvedSymbol(ICanvas* canvas, const ResolvedSymbol& sym, double ax,
                        double ay, double px_per_himetric, double pixmap_scale,
                        double rotation_rad, InkBox* ink,
                        const FvColor* tint) {
  if (sym.vec != nullptr) {
    DrawSymbolAt(canvas, *sym.vec, ax, ay, px_per_himetric, rotation_rad, ink,
                 tint);
    return true;
  }
  if (sym.pix == nullptr) return false;
  // The tile states how many of its own pixels make one nominal pixel (a
  // sprite sheet's `pixelRatio`, a loose file's `@2x`), so the sampler is
  // asked for the scale in TILE pixels. Default 1.0 divides exactly and is
  // what keeps the pre-G2 pixmaps byte-identical.
  const double ratio =
      sym.pix->pixel_ratio > 0.0 ? sym.pix->pixel_ratio : 1.0;
  DrawPixmapSymbolAt(canvas, *sym.pix, ax, ay, pixmap_scale / ratio,
                     rotation_rad, ink, tint);
  return true;
}

PathArc::PathArc(const std::vector<SurfacePoint>& path)
    : path_(path), cum_(path.size(), 0.0) {
  for (size_t i = 1; i < path_.size(); ++i) {
    cum_[i] = cum_[i - 1] + std::hypot(path_[i].x - path_[i - 1].x,
                                       path_[i].y - path_[i - 1].y);
  }
}

size_t PathArc::SegmentAt(double s) const {
  if (path_.size() < 2) return 0;
  const size_t last = path_.size() - 2;
  size_t i = static_cast<size_t>(
      std::upper_bound(cum_.begin(), cum_.end(), s) - cum_.begin());
  i = i == 0 ? 0 : (std::min)(i - 1, last);
  // Zero-length segments carry no direction: step back to the nearest real one,
  // then forward if the path starts with them.
  while (i > 0 && !(cum_[i + 1] > cum_[i])) --i;
  while (i < last && !(cum_[i + 1] > cum_[i])) ++i;
  return i;
}

SurfacePoint PathArc::At(double s, double d, double seg_s) const {
  if (path_.empty()) return SurfacePoint{};
  if (path_.size() < 2) return path_.front();
  const size_t i = SegmentAt(seg_s);
  const SurfacePoint& a = path_[i];
  const SurfacePoint& b = path_[i + 1];
  const double len = cum_[i + 1] - cum_[i];
  if (!(len > 0.0)) return a;
  const double tx = (b.x - a.x) / len, ty = (b.y - a.y) / len;
  const double t = s - cum_[i];
  // (ty, -tx) is the left-hand normal on a y-down surface, the side a
  // symbol's +y lands on in DrawSymbolAt.
  return SurfacePoint{a.x + tx * t + d * ty, a.y + ty * t - d * tx};
}

void PathArc::VerticesBetween(double a, double b,
                              std::vector<double>* out) const {
  if (cum_.size() < 3 || !(b > a)) return;
  auto it = std::upper_bound(cum_.begin() + 1, cum_.end() - 1, a);
  for (; it != cum_.end() - 1 && *it < b; ++it) out->push_back(*it);
}

namespace {

/// A symbol point in path space: u is arc length, v the left-hand offset,
/// both in pixels.
struct UV {
  double u = 0.0, v = 0.0;
};

/// Bends one symbol-space polyline, cutting it at u = 0 and u = arc.length()
/// and at every path vertex it crosses. Each piece is mapped on the segment
/// under its midpoint, so a crossing becomes a bevel join.
void BendRun(const PathArc& arc, const std::vector<UV>& pts, bool closed,
             bool clip, std::vector<std::vector<SurfacePoint>>* out) {
  const double total = arc.length();
  std::vector<SurfacePoint> run;
  auto push = [&run](const SurfacePoint& p) {
    if (run.empty() || std::fabs(run.back().x - p.x) > 1e-9 ||
        std::fabs(run.back().y - p.y) > 1e-9)
      run.push_back(p);
  };
  auto flush = [&] {
    if (run.size() >= 2) out->push_back(std::move(run));
    run.clear();
  };
  const size_t n = pts.size();
  const size_t edges = closed ? n : n - 1;
  std::vector<double> cuts;
  for (size_t e = 0; e < edges; ++e) {
    const UV& p = pts[e];
    const UV& q = pts[(e + 1) % n];
    const double du = q.u - p.u, dv = q.v - p.v;
    cuts.assign({0.0, 1.0});
    if (du != 0.0) {
      std::vector<double> us;
      arc.VerticesBetween((std::min)(p.u, q.u), (std::max)(p.u, q.u), &us);
      if (clip) {
        if ((std::min)(p.u, q.u) < 0.0 && (std::max)(p.u, q.u) > 0.0)
          us.push_back(0.0);
        if ((std::min)(p.u, q.u) < total && (std::max)(p.u, q.u) > total)
          us.push_back(total);
      }
      for (double u : us) cuts.push_back((u - p.u) / du);
      std::sort(cuts.begin(), cuts.end());
    }
    for (size_t k = 0; k + 1 < cuts.size(); ++k) {
      const double t0 = cuts[k], t1 = cuts[k + 1];
      if (!(t1 > t0)) continue;
      const double mid = p.u + du * (t0 + t1) / 2.0;
      if (clip && (mid < 0.0 || mid > total)) {
        flush();
        continue;
      }
      push(arc.At(p.u + du * t0, p.v + dv * t0, mid));
      push(arc.At(p.u + du * t1, p.v + dv * t1, mid));
    }
  }
  flush();
}

/// Sutherland-Hodgman against one side of u = bound.
std::vector<UV> ClipU(const std::vector<UV>& ring, double bound, bool keep_above) {
  std::vector<UV> out;
  const size_t n = ring.size();
  auto inside = [&](const UV& p) {
    return keep_above ? p.u >= bound : p.u <= bound;
  };
  for (size_t i = 0; i < n; ++i) {
    const UV& a = ring[i];
    const UV& b = ring[(i + 1) % n];
    const bool ia = inside(a), ib = inside(b);
    if (ia) out.push_back(a);
    if (ia != ib) {
      const double t = (bound - a.u) / (b.u - a.u);
      out.push_back(UV{bound, a.v + (b.v - a.v) * t});
    }
  }
  return out;
}

}  // namespace

std::vector<BentPrimitive> BendSymbol(const VectorSymbol& sym,
                                      const PathArc& arc, double along,
                                      double offset_px,
                                      double px_per_himetric) {
  std::vector<BentPrimitive> out;
  if (!(arc.length() > 0.0)) return out;
  auto to_uv = [&](const SymbolPoint& p) {
    return UV{along + p.x * px_per_himetric, offset_px + p.y * px_per_himetric};
  };
  for (const SymbolPrimitive& prim : sym.primitives) {
    std::vector<UV> pts;
    bool closed = false;
    switch (prim.type) {
      case SymbolPrimitiveType::kPolyline:
        if (prim.points.size() < 2 || !prim.has_stroke) continue;
        for (const SymbolPoint& p : prim.points) pts.push_back(to_uv(p));
        break;
      case SymbolPrimitiveType::kPolygon:
        if (prim.points.size() < 3) continue;
        for (const SymbolPoint& p : prim.points) pts.push_back(to_uv(p));
        closed = true;
        break;
      case SymbolPrimitiveType::kEllipse: {
        // Conjugate radii: c + r1 cos t + r2 sin t traces the ellipse exactly.
        constexpr int kSides = 24;
        for (int k = 0; k < kSides; ++k) {
          const double t = 2.0 * 3.14159265358979323846 * k / kSides;
          pts.push_back(to_uv(SymbolPoint{
              prim.center.x + prim.radius1.x * std::cos(t) +
                  prim.radius2.x * std::sin(t),
              prim.center.y + prim.radius1.y * std::cos(t) +
                  prim.radius2.y * std::sin(t)}));
        }
        closed = true;
        break;
      }
      case SymbolPrimitiveType::kText:
        continue;
    }
    BentPrimitive bp;
    bp.source = &prim;
    if (closed && prim.has_fill) {
      std::vector<UV> ring = ClipU(ClipU(pts, 0.0, true), arc.length(), false);
      if (ring.size() >= 3) {
        std::vector<std::vector<SurfacePoint>> runs;
        BendRun(arc, ring, true, false, &runs);
        if (!runs.empty() && runs.front().size() >= 3) bp.ring = runs.front();
      }
    }
    if (prim.has_stroke) BendRun(arc, pts, closed, true, &bp.lines);
    if (!bp.ring.empty() || !bp.lines.empty()) out.push_back(std::move(bp));
  }
  return out;
}

void DrawSymbolAlongPath(ICanvas* canvas, const VectorSymbol& sym,
                         const PathArc& arc, double along, double offset_px,
                         double px_per_himetric, InkBox* ink,
                         const FvColor* tint) {
  const PixelSize size = canvas->Size();
  for (BentPrimitive& bp :
       BendSymbol(sym, arc, along, offset_px, px_per_himetric)) {
    const SymbolPrimitive& prim = *bp.source;
    if (!bp.ring.empty()) {
      Brush brush;
      brush.color = tint != nullptr ? *tint : prim.fill_color;
      std::vector<PixelPoint> ring =
          ClipPolygon(bp.ring, size.width, size.height);
      if (ring.size() >= 3) {
        std::vector<std::vector<PixelPoint>> rings{std::move(ring)};
        canvas->DrawPolyPolygon(rings, &brush, nullptr);
      }
      if (ink != nullptr)
        for (const SurfacePoint& p : bp.ring) ink->Add(p.x, p.y);
    }
    if (!bp.lines.empty()) {
      Pen pen;
      pen.color = tint != nullptr ? *tint : prim.stroke_color;
      pen.width = std::max(1, static_cast<int>(std::lround(
                                  prim.stroke_width * px_per_himetric)));
      for (const std::vector<SurfacePoint>& line : bp.lines) {
        for (auto& run : ClipPolyline(line, size.width, size.height))
          canvas->DrawLines(run, pen);
        if (ink != nullptr)
          for (const SurfacePoint& p : line) ink->Add(p.x, p.y);
      }
    }
  }
}

}  // namespace fv
