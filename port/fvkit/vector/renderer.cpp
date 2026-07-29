// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/vector/renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

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

  // FAST PATH (R3b): a path wholly inside the canvas is not clipped at all —
  // it is the common case on a chart drawn at its own scale, and it was
  // costing a Cohen-Sutherland set-up per segment. When every point is inside,
  // ClipSegment returns each segment unchanged and every segment joins the
  // previous one end to end, so the loop below provably emits exactly ONE run:
  // the points in order with consecutive duplicate PIXELS suppressed. Same
  // output, byte for byte, including the >= 2 point rule.
  bool all_in = true;
  for (const SurfacePoint& p : pts)
    if (OutCode(p.x, p.y, xmax, ymax) != kInside) {
      all_in = false;
      break;
    }
  if (all_in) {
    std::vector<PixelPoint> run;
    run.reserve(pts.size());
    for (const SurfacePoint& p : pts) {
      const PixelPoint px = ToPixel(p.x, p.y);
      if (run.empty() || !SamePixel(run.back(), px)) run.push_back(px);
    }
    if (run.size() >= 2) runs.push_back(std::move(run));
    return runs;
  }

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

  // FAST PATH (R3b): when every vertex is inside all four edges, each of the
  // four Sutherland-Hodgman passes is the identity (cur_in and prev_in are
  // both true, so the pass emits `cur` and nothing else), and the ring was
  // being copied five times to prove it. A DNC depth area can carry thousands
  // of vertices, and clipping measured as the single largest cost in a vector
  // frame — larger than the fill it feeds. Skipping straight to the pixel
  // conversion is the same output by construction.
  bool all_in = true;
  for (const SurfacePoint& p : ring)
    if (!InsideEdge(p, 0, xmax, ymax) || !InsideEdge(p, 1, xmax, ymax) ||
        !InsideEdge(p, 2, xmax, ymax) || !InsideEdge(p, 3, xmax, ymax)) {
      all_in = false;
      break;
    }

  // Scratch reused across calls: the clipper is hot and per-call vectors were
  // a measurable share of it. Not re-entrant, which it never was.
  static thread_local std::vector<SurfacePoint> in, work;
  if (!all_in) {
    in.assign(ring.begin(), ring.end());
    for (int side = 0; side < 4 && !in.empty(); ++side) {
      work.clear();
      work.reserve(in.size() + 4);
      for (size_t i = 0; i < in.size(); ++i) {
        const SurfacePoint& cur = in[i];
        const SurfacePoint& prev = in[(i + in.size() - 1) % in.size()];
        const bool cur_in = InsideEdge(cur, side, xmax, ymax);
        const bool prev_in = InsideEdge(prev, side, xmax, ymax);
        if (cur_in) {
          if (!prev_in)
            work.push_back(IntersectEdge(prev, cur, side, xmax, ymax));
          work.push_back(cur);
        } else if (prev_in) {
          work.push_back(IntersectEdge(prev, cur, side, xmax, ymax));
        }
      }
      in.swap(work);
    }
  }
  const std::vector<SurfacePoint>& clipped = all_in ? ring : in;
  out.reserve(clipped.size());
  for (const SurfacePoint& p : clipped) {
    const PixelPoint px = ToPixel(p.x, p.y);
    if (out.empty() || !SamePixel(out.back(), px)) out.push_back(px);
  }
  if (out.size() > 1 && SamePixel(out.front(), out.back())) out.pop_back();
  if (out.size() < 3) out.clear();
  return out;
}

// ---------------------------------------------------------------------------
// The shared along-path / area placer (E3b). Pure geometry; see renderer.h.
// ---------------------------------------------------------------------------

namespace {

constexpr double kPi = 3.14159265358979323846;

// Ceiling on stamps from one PlaceOverArea call. A 4000x4000 area at 2 px
// spacing would otherwise ask for four million symbol draws.
constexpr size_t kMaxPatternStamps = 20000;

// Point and unit tangent at arc-length `s` along the polyline. Returns the
// index of the segment holding `s`, which the caller reuses as the starting
// point of the next search — the walk only ever moves forward, so this keeps
// a whole pattern walk linear in the number of vertices instead of searching
// (or worse, rescanning) the path once per run.
size_t SampleAt(const std::vector<SurfacePoint>& path,
                const std::vector<double>& cum, double s, size_t from,
                SurfacePoint* p, double* tx, double* ty) {
  // Segment i runs from cum[i] to cum[i+1]; find the one holding s.
  size_t i = from < path.size() - 1 ? from : path.size() - 2;
  while (i + 2 < path.size() && cum[i + 1] < s) ++i;
  const double seg = cum[i + 1] - cum[i];
  const double t = seg > 0.0 ? (s - cum[i]) / seg : 0.0;
  const double dx = path[i + 1].x - path[i].x;
  const double dy = path[i + 1].y - path[i].y;
  p->x = path[i].x + dx * t;
  p->y = path[i].y + dy * t;
  const double len = std::hypot(dx, dy);
  *tx = len > 0.0 ? dx / len : 1.0;
  *ty = len > 0.0 ? dy / len : 0.0;
  return i;
}

// The piece of the polyline between two arc lengths, endpoints interpolated.
// `from` is the segment holding s0, as returned by SampleAt.
std::vector<SurfacePoint> Subpath(const std::vector<SurfacePoint>& path,
                                  const std::vector<double>& cum, double s0,
                                  double s1, size_t from) {
  std::vector<SurfacePoint> out;
  SurfacePoint p;
  double tx = 0.0, ty = 0.0;
  SampleAt(path, cum, s0, from, &p, &tx, &ty);
  out.push_back(p);
  for (size_t i = from + 1; i + 1 < path.size(); ++i) {
    if (cum[i] <= s0) continue;
    if (cum[i] >= s1) break;
    out.push_back(path[i]);
  }
  SampleAt(path, cum, s1, from, &p, &tx, &ty);
  out.push_back(p);
  return out;
}

bool PointInRing(const std::vector<PixelPoint>& ring, double x, double y) {
  bool in = false;
  for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
    const double yi = ring[i].y, yj = ring[j].y;
    if ((yi > y) == (yj > y)) continue;
    const double xi = ring[i].x, xj = ring[j].x;
    const double cut = xi + (y - yi) * (xj - xi) / (yj - yi);
    if (x < cut) in = !in;
  }
  return in;
}

}  // namespace

PathPlacement PlaceAlongPath(const std::vector<SurfacePoint>& path,
                             const std::vector<PathRun>& runs, double phase) {
  PathPlacement out;
  if (path.size() < 2 || runs.empty()) return out;

  std::vector<double> cum(path.size(), 0.0);
  for (size_t i = 1; i < path.size(); ++i) {
    cum[i] = cum[i - 1] + std::hypot(path[i].x - path[i - 1].x,
                                     path[i].y - path[i - 1].y);
  }
  const double total = cum.back();
  if (!(total > 0.0)) return out;

  double pattern_len = 0.0;
  for (const PathRun& r : runs) pattern_len += (std::max)(0.0, r.length);

  // Where in the cycle the path starts.
  size_t ri = 0;
  double into = 0.0;
  if (phase > 0.0 && pattern_len > 0.0) {
    double p = std::fmod(phase, pattern_len);
    for (size_t n = 0; n < runs.size() && p > 0.0; ++n) {
      const double len = (std::max)(0.0, runs[ri].length);
      if (p < len) {
        into = p;
        break;
      }
      p -= len;
      ri = (ri + 1) % runs.size();
    }
  }

  // Cycles x runs, saturated: a pathological run list must not overflow the
  // budget into a negative number and disable it.
  const long max_steps = (std::min)(
      static_cast<long>(kMaxPatternCycles) * static_cast<long>(runs.size()),
      static_cast<long>(1) << 24);
  double s = 0.0;
  size_t seg = 0;  // the segment the walk has reached; only moves forward
  for (long step = 0; s < total; ++step) {
    if (step >= max_steps) {
      out.truncated = true;
      break;
    }
    const PathRun& r = runs[ri];
    // A kDash with no length is GeoSym's "run to the end of the line".
    const bool to_end = r.type == PathRunType::kDash && r.length <= 0.0;
    const double len =
        to_end ? total - s : (std::max)(0.0, r.length - into);
    into = 0.0;
    const double e = (std::min)(total, s + len);

    if (r.type == PathRunType::kDash) {
      if (e > s) {
        SurfacePoint ignore;
        double itx = 0.0, ity = 0.0;
        seg = SampleAt(path, cum, s, seg, &ignore, &itx, &ity);
        out.dashes.push_back(Subpath(path, cum, s, e, seg));
      }
    } else if (r.type == PathRunType::kSymbol) {
      SurfacePoint p;
      double tx = 0.0, ty = 0.0;
      seg = SampleAt(path, cum, s, seg, &p, &tx, &ty);
      PlacedSymbol ps;
      ps.symbol_id = r.symbol_id;
      // Symbol +y maps to screen (ty, -tx) once the renderer's y flip is
      // applied, which is the left-hand normal of the direction of travel.
      ps.x = p.x + r.offset * ty;
      ps.y = p.y - r.offset * tx;
      ps.rotation_deg = std::atan2(-ty, tx) * 180.0 / kPi + r.rotation_deg;
      ps.scale = r.symbol_scale > 0.0 ? r.symbol_scale : 1.0;
      out.symbols.push_back(std::move(ps));
    }

    s = e;
    if (to_end) break;
    ri = (ri + 1) % runs.size();
  }
  return out;
}

std::vector<SurfacePoint> PlaceOverArea(const std::vector<PixelPoint>& ring,
                                        double spacing_x, double spacing_y,
                                        bool staggered) {
  std::vector<SurfacePoint> out;
  if (ring.size() < 3 || !(spacing_x > 0.0) || !(spacing_y > 0.0)) return out;

  double minx = ring[0].x, maxx = ring[0].x;
  double miny = ring[0].y, maxy = ring[0].y;
  for (const PixelPoint& p : ring) {
    minx = (std::min)(minx, static_cast<double>(p.x));
    maxx = (std::max)(maxx, static_cast<double>(p.x));
    miny = (std::min)(miny, static_cast<double>(p.y));
    maxy = (std::max)(maxy, static_cast<double>(p.y));
  }

  // The grid is anchored to the canvas origin, not to the ring's own corner,
  // so two adjacent areas sharing a pattern line up instead of each starting
  // its own grid. (It still shifts when the map pans — a geographic anchor is
  // an R3 concern, along with the retained scene.)
  const double x0 = std::floor(minx / spacing_x) * spacing_x;
  const double y0 = std::floor(miny / spacing_y) * spacing_y;

  int row = 0;
  for (double y = y0; y <= maxy && out.size() < kMaxPatternStamps;
       y += spacing_y, ++row) {
    const double shift = (staggered && (row & 1)) ? spacing_x * 0.5 : 0.0;
    for (double x = x0 + shift; x <= maxx && out.size() < kMaxPatternStamps;
         x += spacing_x) {
      if (PointInRing(ring, x, y)) out.push_back(SurfacePoint{x, y});
    }
  }
  return out;
}

// ---------------------------------------------------------------------------

VectorRenderer::VectorRenderer(VectorSourcePtr source, StyleEnginePtr style)
    : source_(std::move(source)), style_(std::move(style)) {}

namespace {

// The viewport grown by `margin` on each side, clamped to the sphere. Used to
// retain slightly more than is on screen so a small pan is a scene hit.
GeoRect GrowRect(const GeoRect& r, double margin) {
  if (!(margin > 0.0) || r.CrossesAntimeridian()) return r;
  const double dlat = (r.ur.lat - r.ll.lat) * margin;
  const double dlon = (r.ur.lon - r.ll.lon) * margin;
  GeoRect out;
  out.ll.lat = std::max(-90.0, r.ll.lat - dlat);
  out.ur.lat = std::min(90.0, r.ur.lat + dlat);
  out.ll.lon = std::max(-180.0, r.ll.lon - dlon);
  out.ur.lon = std::min(180.0, r.ur.lon + dlon);
  return out;
}

// Projects a run of scene vertices; returns false if the projection rejected a
// point (out of the surface's valid domain), in which case the part is skipped.
bool ProjectPart(const MapProjection& proj, const GeoPoint* pts, size_t n,
                 std::vector<SurfacePoint>* out) {
  out->clear();
  out->reserve(n);
  for (size_t i = 0; i < n; ++i) {
    double sx = 0.0, sy = 0.0;
    if (!proj.GeoToSurface(pts[i], &sx, &sy).ok()) return false;
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

// Draws a pixmap symbol so that its PIVOT lands on (ax, ay).
//
// The tile is authored in pixels, so the identity case — no user zoom, no
// rotation — is a straight blit at an integer offset and the sheet's own
// anti-aliased edges reach the canvas untouched. That is the case that must
// stay exact, and it is also every point symbol on a default chart.
//
// Otherwise the tile is resampled NEAREST-NEIGHBOUR into a temporary buffer by
// inverse-mapping each destination pixel. Nearest, not bilinear: these glyphs
// are 9-46 px of hard-edged chart symbology, and interpolating them smears the
// one-pixel strokes a buoy is drawn with. A rotated raster symbol is a
// degradation either way — the vector twin is what a product should ship — so
// the cheap sampler is the honest one.
void DrawPixmapSymbolAt(ICanvas* canvas, const SymbolPixmap& sym, double ax,
                        double ay, double scale, double rotation_rad,
                        InkBox* ink) {
  const int sw = sym.tile.Width(), sh = sym.tile.Height();
  if (sw <= 0 || sh <= 0) return;

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

// A symbol id resolved to whichever form the engine has for it. Looked up
// ONCE and then stamped as many times as the placer asks — an area pattern is
// hundreds of stamps of the same id, and R3b did not make the fill fast so a
// hash lookup could be added back per stamp.
struct ResolvedSymbol {
  const VectorSymbol* vec = nullptr;
  const SymbolPixmap* pix = nullptr;
  bool drawable() const { return vec != nullptr || pix != nullptr; }
};

// Display list first: a product that authors a symbol both ways keeps its
// vector definition, which scales and rotates without resampling.
ResolvedSymbol ResolveSymbol(IStyleEngine* style, const std::string& id) {
  ResolvedSymbol r;
  const VectorSymbol* sym = style->Symbol(id);
  if (sym != nullptr && !sym->primitives.empty()) {
    r.vec = sym;
    return r;
  }
  const SymbolPixmap* pix = style->Pixmap(id);
  if (pix != nullptr && !pix->tile.Empty()) r.pix = pix;
  return r;
}

// Draws a resolved symbol at (ax, ay). Returns true when something reached the
// canvas, so a caller only records a pick box for ink that exists.
//
// `px_per_himetric` sizes a display list; `pixmap_scale` sizes a tile, which is
// already in pixels. They are the same zoom in each form's own units and are
// passed SEPARATELY rather than derived from one another: a tile's scale must
// be exact (2.0, not 2.0 divided and re-multiplied by 25.4), because the
// nearest sampler decides the tile's first row and column on a boundary that
// lands exactly on a half-pixel at integer zooms.
bool DrawResolvedSymbol(ICanvas* canvas, const ResolvedSymbol& sym, double ax,
                        double ay, double px_per_himetric, double pixmap_scale,
                        double rotation_rad, InkBox* ink) {
  if (sym.vec != nullptr) {
    DrawSymbolAt(canvas, *sym.vec, ax, ay, px_per_himetric, rotation_rad, ink);
    return true;
  }
  if (sym.pix == nullptr) return false;
  DrawPixmapSymbolAt(canvas, *sym.pix, ax, ay, pixmap_scale, rotation_rad, ink);
  return true;
}

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

}  // namespace

Status VectorRenderer::Render(const MapProjection& proj, ICanvas* canvas) {
  features_queried_ = 0;
  draws_emitted_ = 0;
  query_ms_ = style_ms_ = draw_ms_ = 0.0;
  pick_.Clear();
  if (canvas == nullptr) return Status::Error(kInvalidArg, "null canvas");
  if (!source_ || !style_)
    return Status::Error(kInvalidArg, "renderer needs a source and a style");
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  StyleContext ctx;
  ctx.scale_denominator = proj.Scale();
  ctx.device_dpi = dpi_;
  ctx.symbol_scale = symbol_scale_;

  const GeoRect view = proj.VmapBounds();
  scene_reused_ = scene_.CanServe(view, ctx, style_->style_epoch());
  if (!scene_reused_) {
    SceneBuildParams p;
    p.area = GrowRect(view, scene_margin_);
    p.ctx = ctx;
    p.max_features = max_features_;
    p.simplify_px = simplify_px_;
    p.dpp_x = proj.DegPerPixelLon();
    p.dpp_y = proj.DegPerPixelLat();
    Status s = scene_.Build(source_.get(), style_.get(), p);
    if (!s.ok()) return s;
  }
  // Styling is by priority ACROSS features, so the scene is already in draw
  // order; a reused one cost nothing to get there.
  query_ms_ = scene_reused_ ? 0.0 : scene_.query_ms();
  style_ms_ = scene_reused_ ? 0.0 : scene_.style_ms();
  features_queried_ = scene_.features();

  const Clock::time_point t_draw = Clock::now();
  const PixelSize size = canvas->Size();
  const double px_per_himetric = symbol_scale_ / kHimetricPerHundredthInch;
  // The same zoom in the other symbol form's units: a tile is authored in
  // pixels, so the user's symbol scale IS its scale factor.
  const double pixmap_scale = symbol_scale_;
  std::vector<SurfacePoint> proj_part;

  const std::vector<GeoPoint>& pts = scene_.points();
  const std::vector<uint32_t>& part_first = scene_.part_first();

  for (const SceneItem& it : scene_.items()) {
    const StyleResult& sr = scene_.styles()[it.style];

    for (size_t p = 0; p < it.part_count; ++p) {
      const uint32_t k = it.first_part + static_cast<uint32_t>(p);
      const uint32_t begin = part_first[k], end = part_first[k + 1];
      if (end <= begin) continue;
      if (!ProjectPart(proj, &pts[begin], end - begin, &proj_part)) continue;

      if (it.type == VectorGeometryType::kArea &&
          (sr.fill.valid || sr.area_pattern.valid) && p == 0) {
        // Outer ring only for now; holes arrive with V5c's face topology.
        std::vector<PixelPoint> ring =
            ClipPolygon(proj_part, size.width, size.height);
        if (ring.size() >= 3) {
          std::vector<std::vector<PixelPoint>> rings{std::move(ring)};
          // A patterned area may carry a boundary pen and no fill at all
          // (S-52's MARCUL row is AP(MARCUL02);LS(DASH,1,CHGRD)), so the pen
          // is drawn whether or not there is a brush — otherwise entering
          // this branch for the pattern would silently swallow the boundary
          // the stroke branch below would have drawn.
          if (sr.fill.valid || sr.stroke.valid) {
            canvas->DrawPolyPolygon(rings, sr.fill.valid ? &sr.fill.brush
                                                         : nullptr,
                                    sr.stroke.valid ? &sr.stroke.pen : nullptr);
            ++draws_emitted_;
          }
          if (sr.area_pattern.valid) {
            // Resolved once, before the placement walk: an unresolvable
            // pattern must not cost a grid of stamps that all fail, and a
            // resolvable one must not cost a lookup per stamp.
            const ResolvedSymbol pat =
                ResolveSymbol(style_.get(), sr.area_pattern.symbol_id);
            if (pat.drawable()) {
              for (const SurfacePoint& at :
                   PlaceOverArea(rings[0], sr.area_pattern.spacing_x,
                                 sr.area_pattern.spacing_y,
                                 sr.area_pattern.staggered)) {
                DrawResolvedSymbol(
                    canvas, pat, at.x, at.y,
                    px_per_himetric * sr.area_pattern.symbol_scale,
                    pixmap_scale * sr.area_pattern.symbol_scale, 0.0, nullptr);
                ++draws_emitted_;
              }
            }
          }
          if (pick_enabled_) pick_.AddFill(it.ref, sr.priority, rings[0]);
        }
      } else if (sr.stroke.valid && proj_part.size() >= 2) {
        const double half = std::max(0.5, sr.stroke.pen.width / 2.0);
        for (auto& run : ClipPolyline(proj_part, size.width, size.height)) {
          canvas->DrawLines(run, sr.stroke.pen);
          ++draws_emitted_;
          if (pick_enabled_) pick_.AddStroke(it.ref, sr.priority, run, half);
        }
      }

      // A patterned line (GeoSym SAMI, S-52 LC) is laid along the WHOLE
      // projected path and clipped afterwards, not along the clipped runs:
      // clipping first would restart the pattern at the canvas edge, so a
      // dash would jump every time the map panned by one pixel.
      if (sr.line_pattern.valid && !sr.line_pattern.runs.empty() &&
          proj_part.size() >= 2) {
        const PathPlacement placed =
            PlaceAlongPath(proj_part, sr.line_pattern.runs,
                           sr.line_pattern.phase);
        const double half = (std::max)(0.5, sr.line_pattern.pen.width / 2.0);
        for (const std::vector<SurfacePoint>& dash : placed.dashes) {
          for (auto& run : ClipPolyline(dash, size.width, size.height)) {
            canvas->DrawLines(run, sr.line_pattern.pen);
            ++draws_emitted_;
            if (pick_enabled_) pick_.AddStroke(it.ref, sr.priority, run, half);
          }
        }
        for (const PlacedSymbol& ps : placed.symbols) {
          if (ps.x < -1e3 || ps.y < -1e3 || ps.x > size.width + 1e3 ||
              ps.y > size.height + 1e3)
            continue;
          InkBox ink;
          // Resolved per stamp: a pattern's runs can name different symbols.
          if (!DrawResolvedSymbol(canvas,
                                  ResolveSymbol(style_.get(), ps.symbol_id),
                                  ps.x, ps.y, px_per_himetric * ps.scale,
                                  pixmap_scale * ps.scale,
                                  ps.rotation_deg * kPi / 180.0,
                                  pick_enabled_ ? &ink : nullptr))
            continue;
          ++draws_emitted_;
          if (pick_enabled_) {
            ink.Add(ps.x, ps.y);
            pick_.AddBox(it.ref, sr.priority, ink.ToRect(1.0));
          }
        }
      }

      // A single point symbol anchors at the first vertex of each part (a
      // line's symbol, when a row carries one, marks its start). Repeated
      // symbology along the path is line_pattern, above.
      if (sr.symbol.valid) {
        const SurfacePoint& a = proj_part.front();
        if (a.x >= -1e4 && a.y >= -1e4 && a.x <= size.width + 1e4 &&
            a.y <= size.height + 1e4) {
          InkBox ink;
          if (DrawResolvedSymbol(
                  canvas, ResolveSymbol(style_.get(), sr.symbol.symbol_id),
                  a.x, a.y, px_per_himetric * sr.symbol.scale,
                  pixmap_scale * sr.symbol.scale,
                  sr.symbol.rotation_deg * kPi / 180.0,
                  pick_enabled_ ? &ink : nullptr)) {
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
              pick_.AddBox(it.ref, sr.priority, box);
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
              pick_.AddBox(it.ref, sr.priority, box);
            }
          }
        }
      }
    }
  }
  draw_ms_ = MsSince(t_draw);
  return Status::Ok();
}

}  // namespace fv
