// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/vector/renderer.h"

#include <algorithm>
#include <cmath>

namespace fv {
namespace {

// Outcodes for Cohen-Sutherland.
enum { kInside = 0, kLeft = 1, kRight = 2, kBottom = 4, kTop = 8 };

int OutCode(double x, double y, double xmax, double ymax) {
  int code = kInside;
  if (x < 0.0) code |= kLeft;
  else if (x > xmax) code |= kRight;
  if (y < 0.0) code |= kTop;
  else if (y > ymax) code |= kBottom;
  return code;
}

PixelPoint ToPixel(double x, double y) {
  return PixelPoint{static_cast<int>(std::lround(x)),
                    static_cast<int>(std::lround(y))};
}

// Clips one segment; returns false when it is wholly outside.
bool ClipSegment(double* x0, double* y0, double* x1, double* y1, double xmax,
                 double ymax) {
  int c0 = OutCode(*x0, *y0, xmax, ymax);
  int c1 = OutCode(*x1, *y1, xmax, ymax);
  for (int guard = 0; guard < 8; ++guard) {
    if ((c0 | c1) == 0) return true;   // both inside
    if ((c0 & c1) != 0) return false;  // both off the same side
    const int c = c0 != 0 ? c0 : c1;
    double x = 0.0, y = 0.0;
    const double dx = *x1 - *x0, dy = *y1 - *y0;
    if (c & kBottom) {
      y = ymax;
      x = *x0 + dx * (ymax - *y0) / dy;
    } else if (c & kTop) {
      y = 0.0;
      x = *x0 + dx * (0.0 - *y0) / dy;
    } else if (c & kRight) {
      x = xmax;
      y = *y0 + dy * (xmax - *x0) / dx;
    } else {  // kLeft
      x = 0.0;
      y = *y0 + dy * (0.0 - *x0) / dx;
    }
    if (c == c0) {
      *x0 = x; *y0 = y;
      c0 = OutCode(*x0, *y0, xmax, ymax);
    } else {
      *x1 = x; *y1 = y;
      c1 = OutCode(*x1, *y1, xmax, ymax);
    }
  }
  // Degenerate input (NaN); drop it rather than loop.
  return false;
}

bool SamePixel(const PixelPoint& a, const PixelPoint& b) {
  return a.x == b.x && a.y == b.y;
}

// One edge of the Sutherland-Hodgman clip. side: 0 left, 1 right, 2 top,
// 3 bottom.
bool InsideEdge(const SurfacePoint& p, int side, double xmax, double ymax) {
  switch (side) {
    case 0: return p.x >= 0.0;
    case 1: return p.x <= xmax;
    case 2: return p.y >= 0.0;
    default: return p.y <= ymax;
  }
}

SurfacePoint IntersectEdge(const SurfacePoint& a, const SurfacePoint& b,
                           int side, double xmax, double ymax) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  SurfacePoint out;
  switch (side) {
    case 0: out.x = 0.0;  out.y = a.y + dy * (0.0 - a.x) / dx; break;
    case 1: out.x = xmax; out.y = a.y + dy * (xmax - a.x) / dx; break;
    case 2: out.y = 0.0;  out.x = a.x + dx * (0.0 - a.y) / dy; break;
    default: out.y = ymax; out.x = a.x + dx * (ymax - a.y) / dy; break;
  }
  return out;
}

}  // namespace

std::vector<std::vector<PixelPoint>> ClipPolyline(
    const std::vector<SurfacePoint>& pts, int width, int height) {
  std::vector<std::vector<PixelPoint>> runs;
  if (pts.size() < 2 || width <= 0 || height <= 0) return runs;

  const double xmax = width - 1.0, ymax = height - 1.0;
  std::vector<PixelPoint> current;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    double x0 = pts[i].x, y0 = pts[i].y;
    double x1 = pts[i + 1].x, y1 = pts[i + 1].y;
    const bool a_in = OutCode(x0, y0, xmax, ymax) == kInside;
    const bool b_in = OutCode(x1, y1, xmax, ymax) == kInside;
    if (!ClipSegment(&x0, &y0, &x1, &y1, xmax, ymax)) {
      // Wholly outside: whatever run was open ends here.
      if (current.size() >= 2) runs.push_back(std::move(current));
      current.clear();
      continue;
    }
    const PixelPoint pa = ToPixel(x0, y0);
    const PixelPoint pb = ToPixel(x1, y1);
    // A run continues only while segments join end-to-end inside the rect;
    // the moment a segment was clipped on entry, a new run starts.
    if (current.empty() || !a_in || !SamePixel(current.back(), pa)) {
      if (current.size() >= 2) runs.push_back(std::move(current));
      current.clear();
      current.push_back(pa);
    }
    if (!SamePixel(current.back(), pb)) current.push_back(pb);
    if (!b_in) {  // left the rect: close the run
      if (current.size() >= 2) runs.push_back(std::move(current));
      current.clear();
    }
  }
  if (current.size() >= 2) runs.push_back(std::move(current));
  return runs;
}

std::vector<PixelPoint> ClipPolygon(const std::vector<SurfacePoint>& ring,
                                    int width, int height) {
  std::vector<PixelPoint> out;
  if (ring.size() < 3 || width <= 0 || height <= 0) return out;

  const double xmax = width - 1.0, ymax = height - 1.0;
  std::vector<SurfacePoint> in = ring;
  std::vector<SurfacePoint> work;
  for (int side = 0; side < 4 && !in.empty(); ++side) {
    work.clear();
    for (size_t i = 0; i < in.size(); ++i) {
      const SurfacePoint& cur = in[i];
      const SurfacePoint& prev = in[(i + in.size() - 1) % in.size()];
      const bool cur_in = InsideEdge(cur, side, xmax, ymax);
      const bool prev_in = InsideEdge(prev, side, xmax, ymax);
      if (cur_in) {
        if (!prev_in) work.push_back(IntersectEdge(prev, cur, side, xmax, ymax));
        work.push_back(cur);
      } else if (prev_in) {
        work.push_back(IntersectEdge(prev, cur, side, xmax, ymax));
      }
    }
    in.swap(work);
  }
  out.reserve(in.size());
  for (const SurfacePoint& p : in) {
    const PixelPoint px = ToPixel(p.x, p.y);
    if (out.empty() || !SamePixel(out.back(), px)) out.push_back(px);
  }
  if (out.size() > 1 && SamePixel(out.front(), out.back())) out.pop_back();
  if (out.size() < 3) out.clear();
  return out;
}

// ---------------------------------------------------------------------------

VectorRenderer::VectorRenderer(VectorSourcePtr source, StyleEnginePtr style)
    : source_(std::move(source)), style_(std::move(style)) {}

namespace {

// One styled pass over one feature, ready to sort.
struct DrawItem {
  int priority = 0;
  size_t order = 0;     // query order, for a stable tie-break
  size_t feature = 0;
  size_t result = 0;
};

// Projects a feature part; returns false if the projection rejected a point
// (out of the surface's valid domain), in which case the part is skipped.
bool ProjectPart(const MapProjection& proj, const std::vector<GeoPoint>& part,
                 std::vector<SurfacePoint>* out) {
  out->clear();
  out->reserve(part.size());
  for (const GeoPoint& g : part) {
    double sx = 0.0, sy = 0.0;
    if (!proj.GeoToSurface(g, &sx, &sy).ok()) return false;
    out->push_back(SurfacePoint{sx, sy});
  }
  return true;
}

// Smallest hit box a point symbol gets, whatever it actually inked. A 2-px
// navaid dot is still something a user aims at.
constexpr int kMinPickBox = 9;

// Accumulates the pixel extent a symbol actually drew into, which is what the
// pick index uses as the symbol's hit box (plan §5.3: hit-test the glyph, not
// the anchor pixel).
struct InkBox {
  double minx = 0, miny = 0, maxx = 0, maxy = 0;
  bool any = false;
  void Add(double x, double y) {
    if (!any) {
      minx = maxx = x;
      miny = maxy = y;
      any = true;
      return;
    }
    minx = std::min(minx, x);
    maxx = std::max(maxx, x);
    miny = std::min(miny, y);
    maxy = std::max(maxy, y);
  }
  PixelRect ToRect(double pad) const {
    PixelRect r;
    if (!any) return r;
    r.x = static_cast<int>(std::floor(minx - pad));
    r.y = static_cast<int>(std::floor(miny - pad));
    r.width = static_cast<int>(std::ceil(maxx + pad)) - r.x + 1;
    r.height = static_cast<int>(std::ceil(maxy + pad)) - r.y + 1;
    return r;
  }
};

// Draws one symbol display list anchored at (ax, ay) pixels.
//
// Mapping (verbatim from CCGMSymbol::DrawSymbol's DC setup): logical (0,0) is
// the anchor, viewport extent is (+k, -k) so y flips, and k px per HIMETRIC
// unit is scale/25.4 — the s_dblConversionFactor path, which treats 1/100
// inch as one pixel for symbols regardless of the device.
//
// `ink` (optional) collects the extent drawn, for the pick index.
void DrawSymbolAt(ICanvas* canvas, const VectorSymbol& sym, double ax,
                  double ay, double px_per_himetric, double rotation_rad,
                  InkBox* ink) {
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
    pen.color = prim.stroke_color;
    pen.width = std::max(1, static_cast<int>(std::lround(prim.stroke_width *
                                                        px_per_himetric)));
    Brush brush;
    brush.color = prim.fill_color;

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
        ts.color = prim.has_fill ? prim.fill_color : prim.stroke_color;
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

}  // namespace

Status VectorRenderer::Render(const MapProjection& proj, ICanvas* canvas) {
  features_queried_ = 0;
  draws_emitted_ = 0;
  pick_.Clear();
  if (canvas == nullptr) return Status::Error(kInvalidArg, "null canvas");
  if (!source_ || !style_)
    return Status::Error(kInvalidArg, "renderer needs a source and a style");
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  VectorQuery q;
  q.area = proj.VmapBounds();
  q.scale_denominator = proj.Scale();
  q.max_features = max_features_;

  std::vector<VectorFeature> features;
  Status s = source_->Query(q, &features);
  if (!s.ok()) return s;
  features_queried_ = features.size();

  StyleContext ctx;
  ctx.scale_denominator = proj.Scale();
  ctx.device_dpi = dpi_;
  ctx.symbol_scale = symbol_scale_;

  // Style everything first: the draw order is by display priority ACROSS
  // features, so nothing can be drawn until every feature has been styled.
  std::vector<std::vector<StyleResult>> styled(features.size());
  std::vector<DrawItem> items;
  for (size_t i = 0; i < features.size(); ++i) {
    Status ss = style_->Style(features[i], ctx, &styled[i]);
    if (!ss.ok()) return ss;
    for (size_t r = 0; r < styled[i].size(); ++r) {
      if (!styled[i][r].visible) continue;
      DrawItem it;
      it.priority = styled[i][r].priority;
      it.order = items.size();
      it.feature = i;
      it.result = r;
      items.push_back(it);
    }
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const DrawItem& a, const DrawItem& b) {
                     if (a.priority != b.priority) return a.priority < b.priority;
                     return a.order < b.order;
                   });

  const PixelSize size = canvas->Size();
  const double px_per_himetric = symbol_scale_ / kHimetricPerHundredthInch;
  std::vector<SurfacePoint> proj_part;

  for (const DrawItem& it : items) {
    const VectorFeature& f = features[it.feature];
    const StyleResult& sr = styled[it.feature][it.result];

    for (size_t p = 0; p < f.parts.size(); ++p) {
      const std::vector<GeoPoint>& part = f.parts[p];
      if (part.empty()) continue;
      if (!ProjectPart(proj, part, &proj_part)) continue;

      if (f.type == VectorGeometryType::kArea && sr.fill.valid && p == 0) {
        // Outer ring only for now; holes arrive with V5c's face topology.
        std::vector<PixelPoint> ring =
            ClipPolygon(proj_part, size.width, size.height);
        if (ring.size() >= 3) {
          std::vector<std::vector<PixelPoint>> rings{std::move(ring)};
          canvas->DrawPolyPolygon(rings, &sr.fill.brush,
                                  sr.stroke.valid ? &sr.stroke.pen : nullptr);
          ++draws_emitted_;
          if (pick_enabled_) pick_.AddFill(f.ref, sr.priority, rings[0]);
        }
      } else if (sr.stroke.valid && part.size() >= 2) {
        const double half = std::max(0.5, sr.stroke.pen.width / 2.0);
        for (auto& run : ClipPolyline(proj_part, size.width, size.height)) {
          canvas->DrawLines(run, sr.stroke.pen);
          ++draws_emitted_;
          if (pick_enabled_) pick_.AddStroke(f.ref, sr.priority, run, half);
        }
      }

      // Point symbology anchors at the first vertex of each part (a line's
      // symbol, when a row carries one, marks its start — GeoSym's
      // along-path SAMI placement is V5c).
      if (sr.symbol.valid) {
        const VectorSymbol* sym = style_->Symbol(sr.symbol.symbol_id);
        if (sym != nullptr && !sym->primitives.empty()) {
          const SurfacePoint& a = proj_part.front();
          if (a.x >= -1e4 && a.y >= -1e4 && a.x <= size.width + 1e4 &&
              a.y <= size.height + 1e4) {
            InkBox ink;
            DrawSymbolAt(canvas, *sym, a.x, a.y,
                         px_per_himetric * sr.symbol.scale,
                         sr.symbol.rotation_deg * 3.14159265358979323846 / 180.0,
                         pick_enabled_ ? &ink : nullptr);
            ++draws_emitted_;
            if (pick_enabled_) {
              // A symbol that degenerates to a couple of pixels is still
              // tappable: grow each axis that came out under the minimum,
              // about the ink's own centre, leaving the other axis alone (a
              // wide, one-pixel-tall symbol keeps its width).
              ink.Add(a.x, a.y);
              PixelRect box = ink.ToRect(1.0);
              if (box.width < kMinPickBox) {
                box.x -= (kMinPickBox - box.width) / 2;
                box.width = kMinPickBox;
              }
              if (box.height < kMinPickBox) {
                box.y -= (kMinPickBox - box.height) / 2;
                box.height = kMinPickBox;
              }
              pick_.AddBox(f.ref, sr.priority, box);
            }
          }
        }
      }

      if (sr.label.valid && !sr.label.text.empty()) {
        const SurfacePoint& a = proj_part.front();
        const int lx = static_cast<int>(std::lround(a.x)) + sr.label.dx;
        const int ly = static_cast<int>(std::lround(a.y)) + sr.label.dy;
        if (lx > -1000 && ly > -1000 && lx < size.width + 1000 &&
            ly < size.height + 1000) {
          canvas->DrawTextString(sr.label.text, lx, ly, sr.label.style);
          ++draws_emitted_;
          if (pick_enabled_) {
            // Text draws from its BASELINE-left, so the box runs upward.
            PixelSize ext;
            if (canvas->GetTextExtent(sr.label.text, sr.label.style, &ext).ok() &&
                ext.width > 0 && ext.height > 0) {
              PixelRect box;
              box.x = lx;
              box.y = ly - ext.height;
              box.width = ext.width;
              box.height = ext.height;
              pick_.AddBox(f.ref, sr.priority, box);
            }
          }
        }
      }
    }
  }
  return Status::Ok();
}

}  // namespace fv
