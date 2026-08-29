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

}  // namespace fv
