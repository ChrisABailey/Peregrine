// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/vector/renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

// G2: DrawSymbolAt / DrawPixmapSymbolAt / ResolveSymbol / DrawResolvedSymbol
// and InkBox used to be defined below, in the anonymous namespace. They now
// live here so an overlay can reach them too.
#include "fvkit/vector/symbol_draw.h"
#include "fvkit/vector/text_draw.h"

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

bool PointInRing(const std::vector<SurfacePoint>& ring, double x, double y) {
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

std::vector<PlacedTextRun> PlaceTextAlongPath(
    const std::vector<SurfacePoint>& path, const std::vector<double>& advances,
    double spacing_px, double max_angle_deg, double offset_px) {
  std::vector<PlacedTextRun> out;
  if (path.size() < 2 || advances.empty()) return out;

  double text_w = 0.0;
  for (double a : advances) text_w += (std::max)(0.0, a);
  if (!(text_w > 0.0)) return out;

  // The walk runs on a possibly REVERSED copy, because reading direction is a
  // property of the whole path: a road digitised east-to-west would otherwise
  // carry its name upside down. Decided once, from the chord of the path, so
  // every run on one part reads the same way and a wiggle in the middle cannot
  // flip a single word.
  const double chord_x = path.back().x - path.front().x;
  const double chord_y = path.back().y - path.front().y;
  // Ties (a due-north/south road) read UPWARD, the cartographic convention;
  // screen y grows downward, so upward is a negative chord_y.
  const bool reverse = chord_x < 0.0 || (chord_x == 0.0 && chord_y > 0.0);
  std::vector<SurfacePoint> fwd;
  if (reverse) fwd.assign(path.rbegin(), path.rend());
  const std::vector<SurfacePoint>& p = reverse ? fwd : path;

  std::vector<double> cum(p.size(), 0.0);
  for (size_t i = 1; i < p.size(); ++i)
    cum[i] = cum[i - 1] +
             std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
  const double total = cum.back();
  if (total < text_w) return out;  // does not fit: draw nothing, not half a name

  // Run starts, centred on the path as a block. One run when no spacing was
  // asked for; the count is capped for the same reason PlaceAlongPath caps
  // cycles — a projection can hand this a path a million pixels long.
  std::vector<double> starts;
  if (spacing_px > 0.0) {
    long n = static_cast<long>(std::floor((total - text_w) / spacing_px)) + 1;
    n = (std::max)(1L, (std::min)(n, static_cast<long>(kMaxPatternCycles)));
    const double span = (n - 1) * spacing_px + text_w;
    const double first = (total - span) / 2.0;
    for (long k = 0; k < n; ++k) starts.push_back(first + k * spacing_px);
  } else {
    starts.push_back((total - text_w) / 2.0);
  }

  const double max_turn = max_angle_deg > 0.0 ? max_angle_deg * kPi / 180.0
                                              : kPi;  // <= 0 = no limit
  size_t seg = 0;
  for (double start : starts) {
    PlacedTextRun run;
    run.glyphs.reserve(advances.size());
    bool ok = true;
    double d = start;
    double prev_angle = 0.0;
    for (size_t g = 0; g < advances.size() && ok; ++g) {
      const double adv = (std::max)(0.0, advances[g]);
      SurfacePoint at, next;
      double tx = 0.0, ty = 0.0;
      seg = SampleAt(p, cum, d, seg, &at, &tx, &ty);
      // The angle comes from the CHORD across this glyph, not the tangent at
      // its origin: the next glyph starts where this one's advance ends, so
      // using the chord is what keeps a curved run's glyphs touching instead
      // of drifting apart on the outside of the bend.
      SampleAt(p, cum, d + adv, seg, &next, &tx, &ty);
      double ax = next.x - at.x, ay = next.y - at.y;
      if (ax == 0.0 && ay == 0.0) {  // zero-advance glyph: fall back to tangent
        SampleAt(p, cum, d, seg, &at, &tx, &ty);
        ax = tx;
        ay = ty;
      }
      const double angle = std::atan2(-ay, ax);
      if (g > 0) {
        double turn = angle - prev_angle;
        while (turn > kPi) turn -= 2.0 * kPi;
        while (turn < -kPi) turn += 2.0 * kPi;
        if (std::fabs(turn) > max_turn) ok = false;
      }
      prev_angle = angle;

      PlacedGlyph pg;
      pg.index = g;
      // Left of travel, the same normal PlaceAlongPath offsets a symbol by.
      const double nlen = std::hypot(ax, ay);
      const double ux = nlen > 0.0 ? ax / nlen : 1.0;
      const double uy = nlen > 0.0 ? ay / nlen : 0.0;
      pg.x = at.x + offset_px * uy;
      pg.y = at.y - offset_px * ux;
      pg.angle_rad = angle;
      run.glyphs.push_back(pg);
      d += adv;
    }
    // A rejected run does not shift the others: the starts are fixed, so the
    // surviving runs stay where a straight stretch put them.
    if (ok && !run.glyphs.empty()) out.push_back(std::move(run));
  }
  return out;
}

std::vector<SurfacePoint> PlaceOverArea(const std::vector<SurfacePoint>& ring,
                                        double spacing_x, double spacing_y,
                                        bool staggered, double anchor_x,
                                        double anchor_y, double clip_w,
                                        double clip_h) {
  std::vector<SurfacePoint> out;
  if (ring.size() < 3 || !(spacing_x > 0.0) || !(spacing_y > 0.0)) return out;

  double minx = ring[0].x, maxx = ring[0].x;
  double miny = ring[0].y, maxy = ring[0].y;
  for (const SurfacePoint& p : ring) {
    minx = (std::min)(minx, p.x);
    maxx = (std::max)(maxx, p.x);
    miny = (std::min)(miny, p.y);
    maxy = (std::max)(maxy, p.y);
  }

  // The ring is the EXACT projected outline, so it can run far off-canvas; walk
  // only the part that can put ink down. Without this a coastline polygon
  // spanning a whole cell would step a lattice across millions of pixels and
  // spend the stamp budget before reaching the viewport.
  if (clip_w > 0.0 && clip_h > 0.0) {
    minx = (std::max)(minx, 0.0);
    miny = (std::max)(miny, 0.0);
    maxx = (std::min)(maxx, clip_w - 1.0);
    maxy = (std::min)(maxy, clip_h - 1.0);
    if (minx > maxx || miny > maxy) return out;
  }

  // The grid is anchored to a point the CALLER pins, not to the ring's own
  // corner, so two adjacent areas sharing a pattern line up instead of each
  // starting its own grid.
  //
  // R3c: that anchor is a GEOGRAPHIC reference projected into pixels, where
  // before it was the canvas origin — which meant the whole lattice moved with
  // the canvas and the stamps crawled inside their own regions as the map
  // panned. Anchoring on the ground makes the stamp positions a function of
  // WHERE ON EARTH the area is, so a pan slides the ring and its pattern
  // together. Which ground point is only a convention; see
  // VectorRenderer::PatternAnchor for why it has to be a NEARBY one.
  const double x0 =
      anchor_x + std::floor((minx - anchor_x) / spacing_x) * spacing_x;
  const double y0 =
      anchor_y + std::floor((miny - anchor_y) / spacing_y) * spacing_y;

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

// Resolves one pattern spacing's stamp lattice into this frame's pixels, and
// remembers it as a GROUND point for the next frame.
//
// WHY THIS IS NOT JUST THE SEED (the R3c bug, found by Chris in the viewer).
// R3c anchored the lattice at lat/lon 0,0 and recomputed the anchor pixel
// every frame as -lon/dpp_lon. That is genuinely fixed to the ground while dpp
// holds still, and panning east/west it is: measured over 40 one-pixel pans of
// the Charleston chart, not one stamp moved.
//
// But MapProjection derives dpp from the CENTRE LATITUDE in its scale and
// physical-scale modes (equal-arc, square ground cells at the centre), which
// is the mode the viewer runs in — so every north/south pan changes dpp_lon
// slightly. At Charleston the 0,0 anchor sits ~600,000 px off-screen, and a
// one-pixel north pan moves dpp_lon by 1.26e-6 of itself: 600,000 x 1.26e-6 =
// 0.75 px of lattice slip per pixel of vertical pan. With 29.5 px cells, ~39
// px of drag slips the lattice a whole cell and every tuft of marsh grass
// re-lands on a different site. That is the "tufts appear and disappear"
// Chris saw, and it is why panning only in longitude did not catch it.
//
// The lever arm is the whole problem, so the fix is to keep the anchor within
// ONE CELL of the viewport centre instead of 600,000 px away. Each frame the
// retained ground point is projected, then walked to the nearest lattice site
// to the centre — a WHOLE NUMBER OF CELLS, so this re-centring cannot move
// the lattice, only re-describe it — and the walked point is stored back. The
// residual slip is now 30 px x 1.26e-6 = 4e-5 px per pixel of pan.
//
// One anchor per (spacing_x, spacing_y): the whole-cell walk is only exact for
// the spacing it was computed with, and patterns on one chart do not share a
// spacing. There are a handful of distinct spacings in a presentation library.
//
// CONSEQUENCE. The lattice is now renderer state, so it is stable across the
// frames of ONE renderer, which is how an interactive caller draws. Two
// renderers seeded at different centres can land on lattices offset by a
// sub-cell amount (the seed carries the full lever arm). A caller that builds
// a fresh renderer per frame therefore still crawls — the viewer does not, and
// neither should any pan test.
void VectorRenderer::PatternAnchor(const MapProjection& proj, double seed_x,
                                   double seed_y, double spacing_x,
                                   double spacing_y, double* ax, double* ay) {
  const PixelSize surface = proj.SurfaceSize();
  const double cx = (surface.width - 1) / 2.0;
  const double cy = (surface.height - 1) / 2.0;

  double x = seed_x, y = seed_y;
  const auto it = pattern_anchors_.find(std::make_pair(spacing_x, spacing_y));
  if (it != pattern_anchors_.end()) {
    double px = 0.0, py = 0.0;
    if (proj.GeoToSurface(it->second, &px, &py).ok()) {
      x = px;
      y = py;
    }
  }

  if (spacing_x > 0.0) x += std::round((cx - x) / spacing_x) * spacing_x;
  if (spacing_y > 0.0) y += std::round((cy - y) / spacing_y) * spacing_y;

  GeoPoint g;
  if (proj.SurfaceToGeo(x, y, &g).ok())
    pattern_anchors_[std::make_pair(spacing_x, spacing_y)] = g;

  *ax = x;
  *ay = y;
}

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

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

}  // namespace

Status VectorRenderer::Render(const MapProjection& proj, ICanvas* canvas) {
  features_queried_ = 0;
  draws_emitted_ = 0;
  halo_draws_ = 0;
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

  // SEED for an area pattern's stamp lattice: where lat/lon 0,0 lands in this
  // frame's pixels. Written out from the projection's own linear relation
  // rather than fetched through GeoToSurface, which unwraps longitude toward
  // the centre — an unwrap that would make the seed jump as a pan crossed
  // +/-90 degrees from the centre.
  //
  // This is only the seed: it fixes WHICH lattice a renderer starts on, and
  // PatternAnchor below then carries that lattice forward from frame to frame
  // without ever evaluating this expression again. See PatternAnchor for why
  // using it every frame (R3c) was wrong.
  const GeoPoint pat_center = proj.Center();
  const PixelSize surface = proj.SurfaceSize();  // GeoToSurface's frame, not
                                                 // the canvas's, if they differ
  const double pattern_seed_x =
      (surface.width - 1) / 2.0 - pat_center.lon / proj.DegPerPixelLon();
  const double pattern_seed_y =
      (surface.height - 1) / 2.0 + pat_center.lat / proj.DegPerPixelLat();
  // Ground metres per pixel, for labels sized in ground units. The latitude
  // axis, because it is the one an equal-arc projection keeps uniform.
  const double meters_per_pixel = proj.DegPerPixelLat() * kMetersPerDegreeLat;
  // The product's own symbol grid, not the renderer's — a display list is
  // sized so that it comes out the same as the TILE of the same symbol, and
  // only the engine knows what grid its artists drew on (25.4 for GeoSym's
  // 1/100 inch, 32 for S-52's 0.32 mm nominal pixel).
  const double units_per_symbol_px =
      style_ != nullptr && style_->himetric_per_symbol_pixel() > 0.0
          ? style_->himetric_per_symbol_pixel()
          : kHimetricPerHundredthInch;
  const double px_per_himetric = symbol_scale_ / units_per_symbol_px;
  // The same zoom in the other symbol form's units: a tile is authored in
  // pixels, so the user's symbol scale IS its scale factor.
  const double pixmap_scale = symbol_scale_;
  // PR2: the chart's own turn, taken out of every NORTH-UP symbol angle below.
  // Read once per frame — the projection cannot turn mid-render, and a point
  // symbol per feature is the wrong place to ask.
  const double chart_rotation = proj.Rotation();
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
              double anchor_x = 0.0, anchor_y = 0.0;
              PatternAnchor(proj, pattern_seed_x, pattern_seed_y,
                            sr.area_pattern.spacing_x,
                            sr.area_pattern.spacing_y, &anchor_x, &anchor_y);
              // The EXACT projected ring, not the clipped integer one the fill
              // is drawn from: ClipPolygon rounds every vertex to a whole pixel
              // and drops vertices that round together, so its boundary moves
              // by up to half a pixel — differently at every pan — and a stamp
              // sitting near an edge flips in and out. On a marsh shredded by
              // tidal channels that is most of them. Found alongside the anchor
              // lever arm and visible on an EAST pan, where the anchor is
              // blameless. Membership is now a question about the geometry, not
              // about how the geometry happened to round.
              for (const SurfacePoint& at :
                   PlaceOverArea(proj_part, sr.area_pattern.spacing_x,
                                 sr.area_pattern.spacing_y,
                                 sr.area_pattern.staggered, anchor_x, anchor_y,
                                 size.width, size.height)) {
                // PR2 leaves this 0.0. An area pattern is a screen-space FILL
                // TEXTURE — PlaceOverArea lays its stamps on a grid aligned to
                // the surface axes, not to the geography — so turning the
                // stamps while their grid stayed put would be half a rotation
                // and would read worse than none. The ring being filled turns
                // with the chart because it is projected; the hatch inside it
                // stays upright, which is what a hatch does.
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
                  SymbolAngleOnChart(sr.symbol.rotation_deg, chart_rotation) *
                      kPi / 180.0,
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
        TextStyle ts = sr.label.style;
        ts.size = LabelPixelSize(sr.label, ctx.scale_denominator,
                                 meters_per_pixel, label_ref_scale_);
        // Below the floor the text is illegible, and drawing it anyway is how
        // a zoomed-out chart fills with grey mush.
        const double halo_px = HaloPixels(sr.label, ts.size);
        double hdx[kMaxHaloOffsets], hdy[kMaxHaloOffsets];
        const int halo_n =
            halo_px > 0.0 ? HaloOffsets(halo_px, hdx, hdy) : 0;
        TextStyle hs = ts;
        hs.color = sr.label.halo_color;

        if (ts.size >= kMinLabelPx) {
          if (sr.label.placement == LabelPlacement::kAlongPath &&
              it.type != VectorGeometryType::kPoint && proj_part.size() >= 2) {
            // Placed along the WHOLE projected part, then each glyph clipped
            // by being on the canvas or not — the same reason a line pattern
            // is laid before clipping, and here it also decides whether the
            // name FITS, which the clipped fragment cannot answer.
            std::vector<double> adv;
            if (GlyphAdvances(canvas, sr.label.text, ts, &adv)) {
              for (const PlacedTextRun& run : PlaceTextAlongPath(
                       proj_part, adv, sr.label.spacing_px,
                       sr.label.max_angle_deg,
                       sr.label.offset_px +
                           AlongPathAnchorShift(sr.label, ts.size))) {
                InkBox ink;
                bool drew = false;
                // The WHOLE run's halo goes down before ANY of its fill. Per
                // glyph would be wrong at a tight bend: glyph N's halo lands on
                // glyph N-1's face and eats it from the trailing edge. The
                // offsets stay in SCREEN space rather than turning with the
                // glyph — a ring is a ring at any angle.
                if (halo_n > 0) {
                  for (const PlacedGlyph& g : run.glyphs) {
                    if (g.x < -ts.size * 2 || g.y < -ts.size * 2 ||
                        g.x > size.width + ts.size * 2 ||
                        g.y > size.height + ts.size * 2)
                      continue;
                    const std::string gs = sr.label.text.substr(g.index, 1);
                    for (int i = 0; i < halo_n; ++i)
                      canvas->DrawRotatedTextString(gs, g.x + hdx[i],
                                                    g.y + hdy[i], g.angle_rad,
                                                    hs);
                    halo_draws_ += static_cast<size_t>(halo_n);
                  }
                }
                for (const PlacedGlyph& g : run.glyphs) {
                  if (g.x < -ts.size * 2 || g.y < -ts.size * 2 ||
                      g.x > size.width + ts.size * 2 ||
                      g.y > size.height + ts.size * 2)
                    continue;
                  canvas->DrawRotatedTextString(
                      sr.label.text.substr(g.index, 1), g.x, g.y, g.angle_rad,
                      ts);
                  drew = true;
                  if (pick_enabled_) {
                    // One box over the whole run, grown by the em: glyph
                    // quads are rotated and the pick index takes rectangles,
                    // so this is deliberately a little generous.
                    ink.Add(g.x, g.y);
                  }
                }
                if (drew) {
                  ++draws_emitted_;
                  if (pick_enabled_)
                    pick_.AddBox(it.ref, sr.priority, ink.ToRect(ts.size));
                }
              }
            }
          } else {
            const SurfacePoint& a = proj_part.front();
            int lx = static_cast<int>(std::lround(a.x)) + sr.label.dx;
            int ly = static_cast<int>(std::lround(a.y)) + sr.label.dy;

            // The canvas draws baseline-left, so the default alignment needs no
            // measurement at all — and measuring costs a pass over the string.
            // Only a product that actually asks for alignment pays for it.
            const bool aligned = sr.label.halign != LabelHAlign::kLeft ||
                                 sr.label.valign != LabelVAlign::kBaseline;
            PixelSize ext{0, 0};
            bool have_ext = false;
            if (aligned || pick_enabled_) {
              have_ext = canvas->GetTextExtent(sr.label.text, ts, &ext).ok() &&
                         ext.width > 0 && ext.height > 0;
            }
            if (aligned && have_ext) {
              switch (sr.label.halign) {
                case LabelHAlign::kLeft: break;
                case LabelHAlign::kCenter: lx -= ext.width / 2; break;
                case LabelHAlign::kRight: lx -= ext.width; break;
              }
              // The box runs from baseline-height to baseline, so a bottom
              // alignment is the baseline itself and the others push the
              // baseline DOWN from the anchor by part of the height.
              switch (sr.label.valign) {
                case LabelVAlign::kBaseline:
                case LabelVAlign::kBottom: break;
                case LabelVAlign::kCenter: ly += ext.height / 2; break;
                case LabelVAlign::kTop: ly += ext.height; break;
              }
            }

            if (lx > -1000 && ly > -1000 && lx < size.width + 1000 &&
                ly < size.height + 1000) {
              for (int i = 0; i < halo_n; ++i) {
                canvas->DrawTextString(
                    sr.label.text,
                    lx + static_cast<int>(std::lround(hdx[i])),
                    ly + static_cast<int>(std::lround(hdy[i])), hs);
              }
              halo_draws_ += static_cast<size_t>(halo_n);
              canvas->DrawTextString(sr.label.text, lx, ly, ts);
              ++draws_emitted_;
              if (pick_enabled_ && have_ext) {
                // Text draws from its BASELINE-left, so the box runs upward.
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
  }
  draw_ms_ = MsSince(t_draw);
  return Status::Ok();
}

}  // namespace fv
