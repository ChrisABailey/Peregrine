// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/vector/renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

// G2: DrawSymbolAt / DrawPixmapSymbolAt / ResolveSymbol / DrawResolvedSymbol
// and InkBox used to be defined below, in the anonymous namespace. They now
// live here so an overlay can reach them too.
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/draw_interrupt.h"
#include "fvkit/vector/symbol_draw.h"
#include "fvkit/vector/text_draw.h"

namespace fv {
namespace {

/// Text placed so far in one frame, for VectorRenderer's repeat filter.
class LabelRepeats {
 public:
  LabelRepeats(double min_px, PixelSize canvas)
      : min_sq_(min_px * min_px), w_(canvas.width), h_(canvas.height) {}
  bool enabled() const { return min_sq_ > 0.0; }
  /// True when `text` was already placed within the minimum distance. Only
  /// anchors on the canvas take part: an off-canvas copy must not hide one
  /// that shows.
  bool Near(const std::string& text, double x, double y) const {
    if (!OnCanvas(x, y)) return false;
    auto it = placed_.find(text);
    if (it == placed_.end()) return false;
    for (const SurfacePoint& p : it->second)
      if ((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) < min_sq_) return true;
    return false;
  }
  void Add(const std::string& text, double x, double y) {
    if (OnCanvas(x, y)) placed_[text].push_back(SurfacePoint{x, y});
  }

 private:
  bool OnCanvas(double x, double y) const {
    return x >= 0.0 && y >= 0.0 && x < w_ && y < h_;
  }

  double min_sq_;
  double w_, h_;
  std::unordered_map<std::string, std::vector<SurfacePoint>> placed_;
};

/// Ground already taken by labels in one frame, as one disc per glyph, for
/// VectorRenderer's label collision test. A uniform grid keeps the test local.
class LabelCollisions {
 public:
  struct Disc {
    double x, y, r;
  };
  explicit LabelCollisions(bool enabled) : enabled_(enabled) {}
  bool enabled() const { return enabled_; }
  /// True when any of `discs` overlaps a disc already taken.
  bool Hits(const std::vector<Disc>& discs) const {
    for (const Disc& d : discs) {
      const int x0 = Cell(d.x - d.r - max_r_), x1 = Cell(d.x + d.r + max_r_);
      const int y0 = Cell(d.y - d.r - max_r_), y1 = Cell(d.y + d.r + max_r_);
      for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
          auto it = cells_.find(Key(cx, cy));
          if (it == cells_.end()) continue;
          for (const Disc& o : it->second) {
            const double rr = d.r + o.r;
            if ((d.x - o.x) * (d.x - o.x) + (d.y - o.y) * (d.y - o.y) < rr * rr) return true;
          }
        }
      }
    }
    return false;
  }
  void Take(const std::vector<Disc>& discs) {
    for (const Disc& d : discs) {
      cells_[Key(Cell(d.x), Cell(d.y))].push_back(d);
      max_r_ = std::max(max_r_, d.r);
    }
  }

 private:
  static constexpr double kCellPx = 32.0;
  static int Cell(double v) { return static_cast<int>(std::floor(v / kCellPx)); }
  static int64_t Key(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cy);
  }
  bool enabled_;
  double max_r_ = 0.0;
  std::unordered_map<int64_t, std::vector<Disc>> cells_;
};

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

}  // namespace

std::vector<PathRun> DashRuns(const std::vector<double>& dash) {
  // An odd-length pattern is laid twice, so ink and gap always alternate and
  // the second lap inverts the first. This is what SVG and GL mean by it.
  const size_t n = dash.size() % 2 == 0 ? dash.size() : dash.size() * 2;

  std::vector<PathRun> runs;
  runs.reserve(n);
  bool any_gap = false;
  for (size_t i = 0; i < n; ++i) {
    const double len = dash[i % dash.size()];
    if (!(len > 0.0)) continue;
    const PathRunType t =
        (i % 2 == 0) ? PathRunType::kDash : PathRunType::kGap;
    any_gap = any_gap || t == PathRunType::kGap;
    if (!runs.empty() && runs.back().type == t) {
      runs.back().length += len;
      continue;
    }
    PathRun r;
    r.type = t;
    r.length = len;
    runs.push_back(std::move(r));
  }
  if (!any_gap) runs.clear();  // nothing breaks the line: it is solid

  // The stamped nib covers the pixel it lands on, so a piece drawn from its
  // first to its last point inks one pixel more than its arc length: a 5/3
  // pattern would come out 6 on, 2 off, and a 1/1 pattern would be solid. The
  // boundary between ink and gap moves back a pixel to pay for it, which
  // leaves the cycle length — and so the phase of every later dash — alone.
  for (PathRun& r : runs) {
    if (r.type == PathRunType::kDash) {
      // Never to zero: PlaceAlongPath reads a kDash of length 0 as GeoSym's
      // "run to the end of the line", which would draw the pattern solid.
      r.length = (std::max)(1e-6, r.length - 1.0);
    } else {
      r.length += 1.0;
    }
  }
  return runs;
}

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
      ps.along = s;
      ps.offset = r.offset;
      ps.bend = r.bend;
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
    double spacing_px, double max_angle_deg, double offset_px,
    bool keep_upright) {
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
  const bool reverse =
      keep_upright && (chord_x < 0.0 || (chord_x == 0.0 && chord_y > 0.0));
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

std::vector<SurfacePoint> PlaceOverArea(
    const std::vector<std::vector<SurfacePoint>>& rings, double spacing_x,
    double spacing_y, bool staggered, double anchor_x, double anchor_y,
    double clip_w, double clip_h) {
  std::vector<SurfacePoint> out;
  if (rings.empty() || rings[0].size() < 3 || !(spacing_x > 0.0) ||
      !(spacing_y > 0.0))
    return out;

  double minx = rings[0][0].x, maxx = minx;
  double miny = rings[0][0].y, maxy = miny;
  for (const std::vector<SurfacePoint>& r : rings) {
    for (const SurfacePoint& p : r) {
      minx = (std::min)(minx, p.x);
      maxx = (std::max)(maxx, p.x);
      miny = (std::min)(miny, p.y);
      maxy = (std::max)(maxy, p.y);
    }
  }

  // Walk only the part that can put ink down: a coastline polygon spanning a
  // whole cell would otherwise spend the stamp budget before the viewport.
  if (clip_w > 0.0 && clip_h > 0.0) {
    minx = (std::max)(minx, 0.0);
    miny = (std::max)(miny, 0.0);
    maxx = (std::min)(maxx, clip_w - 1.0);
    maxy = (std::min)(maxy, clip_h - 1.0);
    if (minx > maxx || miny > maxy) return out;
  }

  const double x0 =
      anchor_x + std::floor((minx - anchor_x) / spacing_x) * spacing_x;
  const double first_row = std::floor((miny - anchor_y) / spacing_y);
  const double y0 = anchor_y + first_row * spacing_y;

  // One pass over the edges per lattice row: the row's crossings, sorted, and
  // a site is inside when an odd number of them lie strictly to its right
  // (half-open edges, so a vertex on the row is counted once).
  std::vector<double> cuts;
  // Counted from the anchor, not from the first row walked: the offset rows
  // must not change when the area's top or the canvas edge crosses a row.
  long long row = static_cast<long long>(first_row);
  for (double y = y0; y <= maxy && out.size() < kMaxPatternStamps;
       y += spacing_y, ++row) {
    cuts.clear();
    for (const std::vector<SurfacePoint>& r : rings) {
      if (r.size() < 3) continue;
      for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const double yi = r[i].y, yj = r[j].y;
        if ((yi > y) == (yj > y)) continue;
        const double xi = r[i].x, xj = r[j].x;
        cuts.push_back(xi + (y - yi) * (xj - xi) / (yj - yi));
      }
    }
    if (cuts.size() < 2) continue;
    std::sort(cuts.begin(), cuts.end());
    const double shift = (staggered && (row & 1)) ? spacing_x * 0.5 : 0.0;
    size_t right_of = 0;  // cuts at or left of x
    for (double x = x0 + shift; x <= maxx && out.size() < kMaxPatternStamps;
         x += spacing_x) {
      while (right_of < cuts.size() && !(x < cuts[right_of])) ++right_of;
      if (((cuts.size() - right_of) & 1) != 0) out.push_back(SurfacePoint{x, y});
    }
  }
  return out;
}

std::vector<SurfacePoint> PlaceOverArea(const std::vector<SurfacePoint>& ring,
                                        double spacing_x, double spacing_y,
                                        bool staggered, double anchor_x,
                                        double anchor_y, double clip_w,
                                        double clip_h) {
  return PlaceOverArea(std::vector<std::vector<SurfacePoint>>{ring}, spacing_x,
                       spacing_y, staggered, anchor_x, anchor_y, clip_w, clip_h);
}

// ---------------------------------------------------------------------------

VectorRenderer::VectorRenderer(VectorSourcePtr source, StyleEnginePtr style)
    : source_(std::move(source)), style_(std::move(style)) {}

/// Returns this frame's pixel position of a site of the pattern lattice with
/// the given period, and retains it as a ground point for the next frame.
///
/// The retained point is moved by whole periods to the site nearest the
/// surface centre. Degrees per pixel change with the centre latitude in the
/// scale-driven projection modes, so a lattice hung on a distant point slides
/// as the map pans north or south; one hung within a period of the centre does
/// not. `seed_x`/`seed_y` place the lattice on the first frame only. The
/// lattice is renderer state: two renderers do not share one.
void VectorRenderer::PatternAnchor(const MapProjection& proj, double seed_x,
                                   double seed_y, double period_x,
                                   double period_y, double* ax, double* ay) {
  const PixelSize surface = proj.SurfaceSize();
  const double cx = (surface.width - 1) / 2.0;
  const double cy = (surface.height - 1) / 2.0;

  double x = seed_x, y = seed_y;
  const auto it = pattern_anchors_.find(std::make_pair(period_x, period_y));
  if (it != pattern_anchors_.end()) {
    double px = 0.0, py = 0.0;
    if (proj.GeoToSurface(it->second, &px, &py).ok()) {
      x = px;
      y = py;
    }
  }

  if (period_x > 0.0) x += std::round((cx - x) / period_x) * period_x;
  if (period_y > 0.0) y += std::round((cy - y) / period_y) * period_y;

  GeoPoint g;
  if (proj.SurfaceToGeo(x, y, &g).ok())
    pattern_anchors_[std::make_pair(period_x, period_y)] = g;

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

/// Pixel box of a geographic box under an affine projection. Returns false when
/// the box is not within 180 degrees of the centre on both sides (its vertices
/// would not all unwrap alike) or a corner does not project.
bool BoxOnSurface(const MapProjection& proj, const GeoPoint& center,
                  const GeoRect& b, double* minx, double* miny, double* maxx,
                  double* maxy) {
  if (!(b.ll.lon - center.lon > -179.999 && b.ur.lon - center.lon < 179.999))
    return false;
  if (!proj.GeoToSurface(b.ll, minx, miny).ok()) return false;
  *maxx = *minx;
  *maxy = *miny;
  if (b.ll.lat == b.ur.lat && b.ll.lon == b.ur.lon) return true;
  const GeoPoint rest[3] = {b.ur, {b.ll.lat, b.ur.lon}, {b.ur.lat, b.ll.lon}};
  for (const GeoPoint& g : rest) {
    double x = 0.0, y = 0.0;
    if (!proj.GeoToSurface(g, &x, &y).ok()) return false;
    *minx = std::min(*minx, x);
    *maxx = std::max(*maxx, x);
    *miny = std::min(*miny, y);
    *maxy = std::max(*maxy, y);
  }
  return true;
}

/// Upper bound on the distance from a symbol's anchor to its ink, in pixels at
/// the given scales; infinite when the library states no extent for it.
double SymbolReach(ISymbolLibrary* lib, const std::string& id,
                   double px_per_himetric, double pixmap_scale) {
  const ResolvedSymbol rs = ResolveSymbol(lib, id);
  double r = 0.0;
  if (rs.vec != nullptr && !rs.vec->primitives.empty()) {
    const VectorSymbol& v = *rs.vec;
    const double ex = std::max(std::abs(v.min_x), std::abs(v.max_x));
    const double ey = std::max(std::abs(v.min_y), std::abs(v.max_y));
    if (ex == 0.0 && ey == 0.0) return std::numeric_limits<double>::infinity();
    r = std::hypot(ex, ey) * px_per_himetric;
  }
  if (rs.pix != nullptr && rs.pix->pixel_ratio > 0.0) {
    const SymbolPixmap& p = *rs.pix;
    const double ex = std::max(std::abs(p.pivot_x), std::abs(p.tile.Width() - p.pivot_x));
    const double ey = std::max(std::abs(p.pivot_y), std::abs(p.tile.Height() - p.pivot_y));
    r = std::max(r, std::hypot(ex, ey) * pixmap_scale / p.pixel_ratio);
  }
  // The extent leaves out pen width.
  return r * 1.25 + 8.0;
}

/// One area pattern as drawn in a frame: the tile holding one period of it
/// and the pixel a lattice site falls on.
struct PatternFill {
  const PatternTile* tile = nullptr;
  double anchor_x = 0.0, anchor_y = 0.0;
};

/// Draws one period of an area pattern into a transparent tile: one cell, or
/// one cell wide and two rows deep when staggered, the lower row offset by
/// half a cell. A lattice site is at tile pixel (0,0) and ink that crosses a
/// tile edge wraps to the opposite one. `reach` is the symbol's ink radius.
PixelBuffer BuildPatternTile(const ResolvedSymbol& pat, int cell_w, int cell_h,
                             bool staggered, double px_per_himetric,
                             double pixmap_scale, double reach) {
  const int rows = staggered ? 2 : 1;
  CpuCanvas tile(cell_w, cell_h * rows);
  tile.Clear(FvColor{0, 0, 0, 0});
  // Neighbouring sites whose ink can reach into the tile.
  constexpr int kMaxWrap = 16;
  const auto wrap = [&](int cell) {
    if (!std::isfinite(reach)) return 2;
    return (std::min)(kMaxWrap, static_cast<int>(std::ceil(reach / cell)));
  };
  const int nx = wrap(cell_w), ny = wrap(cell_h);
  for (int r = -ny; r < rows + ny; ++r) {
    const int shift = (staggered && (r & 1)) ? cell_w / 2 : 0;
    for (int i = -nx - 1; i <= nx; ++i)
      DrawResolvedSymbol(&tile, pat, i * cell_w + shift, r * cell_h,
                         px_per_himetric, pixmap_scale, 0.0, nullptr);
  }
  return std::move(tile.Buffer());
}

/// Where a part's point symbol or point label is drawn, and the ground point
/// it stands for. kCentre puts a line's at its middle vertex and an area's once,
/// at the scene's interior centre, on the outer ring's pass; returns false for
/// a part that draws nothing.
bool PartAnchor(LabelAnchor anchor, const SceneItem& it, size_t p,
                const MapProjection& proj, const std::vector<SurfacePoint>& part,
                const GeoPoint* geo, SurfacePoint* at, GeoPoint* at_geo) {
  if (anchor == LabelAnchor::kCentre && it.type == VectorGeometryType::kArea) {
    if (p != 0 || !it.has_centre) return false;
    if (!proj.GeoToSurface(it.centre, &at->x, &at->y).ok()) return false;
    *at_geo = it.centre;
    return true;
  }
  const size_t i = anchor == LabelAnchor::kCentre &&
                           it.type == VectorGeometryType::kLine
                       ? part.size() / 2
                       : 0;
  *at = part[i];
  *at_geo = geo[i];
  return true;
}

/// Splits a projected part into the runs its stroke follows. A ring is closed
/// back to its first vertex. `hidden` (one flag per vertex, or null) marks
/// edges that are not stroked; each one ends a run, and a ring's runs start
/// after a hidden edge so that no run is broken at vertex 0.
void StrokeRuns(const std::vector<SurfacePoint>& part, const uint8_t* hidden,
                bool ring, std::vector<std::vector<SurfacePoint>>* runs) {
  runs->clear();
  const size_t n = part.size();
  if (n < 2) return;
  const bool close = ring && (part.front().x != part.back().x ||
                              part.front().y != part.back().y);
  if (hidden == nullptr) {
    runs->push_back(part);
    if (close) runs->back().push_back(part.front());
    return;
  }
  const size_t m = close ? n : n - 1;  // edges; edge e ends at vertex (e+1)%n
  auto at = [&](size_t v) -> const SurfacePoint& { return part[v % n]; };
  size_t start = 0;
  if (ring) {
    for (size_t e = 0; e < m; ++e)
      if (hidden[e] != 0) {
        start = e + 1;
        break;
      }
  }
  std::vector<SurfacePoint> cur;
  for (size_t i = 0; i < m; ++i) {
    const size_t e = ring ? (start + i) % m : i;
    if (hidden[e] != 0) {
      if (cur.size() >= 2) runs->push_back(std::move(cur));
      cur.clear();
      continue;
    }
    if (cur.empty()) cur.push_back(at(e));
    cur.push_back(at(e + 1));
  }
  if (cur.size() >= 2) runs->push_back(std::move(cur));
}

// Ground metres per pixel where a feature actually sits, for labels sized in
// ground units on a projection whose scale varies across the frame. Falls back
// to the centre scale where the point has no image.
double MetersPerPixelAt(const MapProjection& proj, const GeoPoint& at) {
  MapProjection::LocalScale ls;
  if (!proj.LocalScaleAt(at, &ls).ok())
    return proj.DegPerPixelLat() * kMetersPerDegreeLat;
  return ls.m_per_px_y;
}

// The clockwise screen angle of TRUE north at `at`: the chart's own turn, less
// the projection's convergence there. A symbol authored against a real-world
// bearing is turned by this, not by the chart rotation alone -- off a
// cylindrical projection the two differ by degrees, and a north arrow drawn
// with the wrong one points at nothing.
double NorthOnChartAt(const MapProjection& proj, const GeoPoint& at,
                      double chart_rotation_deg) {
  MapProjection::LocalScale ls;
  if (!proj.LocalScaleAt(at, &ls).ok()) return chart_rotation_deg;
  return chart_rotation_deg - ls.convergence_deg;
}

// Smallest hit box a point symbol gets, whatever it actually inked. A 2-px
// navaid dot is still something a user aims at.
constexpr int kMinPickBox = 9;

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

}  // namespace

double VectorRenderer::InkReach(const StyleResult& sr, double px_per_himetric,
                                double pixmap_scale, double label_px) const {
  const double kUnbounded = std::numeric_limits<double>::infinity();
  double reach = 0.0;
  if (sr.stroke.valid) reach = std::max(reach, sr.stroke.pen.width / 2.0);
  if (sr.symbol.valid)
    reach = std::max(reach, SymbolReach(style_.get(), sr.symbol.symbol_id,
                                        px_per_himetric * sr.symbol.scale,
                                        pixmap_scale * sr.symbol.scale));
  if (sr.line_pattern.valid) {
    reach = std::max(reach, sr.line_pattern.pen.width / 2.0);
    for (const PathRun& run : sr.line_pattern.runs) {
      if (run.type != PathRunType::kSymbol) continue;
      reach = std::max(reach, std::abs(run.offset) +
                                  SymbolReach(style_.get(), run.symbol_id,
                                              px_per_himetric * run.symbol_scale,
                                              pixmap_scale * run.symbol_scale));
    }
  }
  if (sr.label.valid && !sr.label.text.empty()) {
    const double halo = HaloPixels(sr.label, label_px);
    if (sr.label.placement == LabelPlacement::kAlongPath) {
      // A run off the canvas still takes collision discs that can hide a run
      // on it, so with collisions on the part is always drawn.
      if (label_collision_) return kUnbounded;
      reach = std::max(reach, std::abs(sr.label.offset_px) +
                                  std::abs(AlongPathAnchorShift(sr.label, label_px)) +
                                  2.0 * label_px + halo);
    } else {
      // One em a byte is at least the string's width.
      reach = std::max(reach, std::abs(sr.label.dx) + std::abs(sr.label.dy) +
                                  label_px * (sr.label.text.size() + 1.0) + halo);
    }
  }
  // Rounding to whole pixels and the minimum pick box.
  return reach + 8.0;
}

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
  LabelRepeats repeats(label_repeat_px_, size);
  LabelCollisions collisions(label_collision_);
  std::vector<LabelCollisions::Disc> run_discs;

  // First-frame position of the area-pattern lattices: where lat/lon 0,0
  // lands. Written from the projection's linear relation because GeoToSurface
  // unwraps longitude toward the centre. PatternAnchor carries the lattice
  // forward on later frames.
  const GeoPoint pat_center = proj.Center();
  const PixelSize surface = proj.SurfaceSize();  // GeoToSurface's frame, not
                                                 // the canvas's, if they differ
  double pattern_seed_x = (surface.width - 1) / 2.0;
  double pattern_seed_y = (surface.height - 1) / 2.0;
  // A tile holds the palette's colours, so a restyle discards them.
  const uint64_t style_epoch = style_ != nullptr ? style_->style_epoch() : 0;
  if (style_epoch != pattern_tiles_epoch_) {
    pattern_tiles_.clear();
    pattern_tiles_epoch_ = style_epoch;
  }
  // Tiles and anchors of the patterns drawn so far in this frame.
  std::map<PatternTileKey, PatternFill> pattern_fills;
  if (proj.IsAffine()) {
    pattern_seed_x -= pat_center.lon / proj.DegPerPixelLon();
    pattern_seed_y += pat_center.lat / proj.DegPerPixelLat();
  } else {
    // A centre with no image leaves the seed at the surface centre.
    double sx = 0.0, sy = 0.0;
    if (proj.GeoToSurface(GeoPoint{0.0, 0.0}, &sx, &sy).ok()) {
      pattern_seed_x = sx;
      pattern_seed_y = sy;
    }
  }
  // Ground metres per pixel, for labels sized in ground units. The latitude
  // axis, because it is the one an equal-arc projection keeps uniform.
  //
  // Off an affine projection the ground scale varies across the frame, so a
  // ground-sized label is measured at its own anchor (PJ5) rather than at the
  // centre. The affine branch keeps the centre value and the arithmetic that
  // produced it: every pinned golden is equal-arc.
  const double meters_per_pixel = proj.DegPerPixelLat() * kMetersPerDegreeLat;
  const bool affine = proj.IsAffine();
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
  std::vector<std::vector<SurfacePoint>> area_rings;
  std::vector<std::vector<SurfacePoint>> stroke_runs;
  static const std::vector<std::vector<SurfacePoint>> kNoRuns;

  const std::vector<GeoPoint>& pts = scene_.points();
  const std::vector<uint32_t>& part_first = scene_.part_first();
  const std::vector<uint8_t>& unstroked = scene_.unstroked();
  const std::vector<GeoRect>& part_bounds = scene_.part_bounds();
  // A box maps to a parallelogram only under an affine projection.
  const bool cull = screen_cull_ && affine;
  const GeoPoint cull_center = proj.Center();
  parts_culled_ = 0;

  for (size_t item = 0; item < scene_.items().size(); ++item) {
    if (DrawInterruptedAt(item)) return Status::Error(kInterrupted, "vector draw interrupted");
    const SceneItem& it = scene_.items()[item];
    const StyleResult& sr = scene_.styles()[it.style];
    double reach = -1.0;  // InkReach(sr), found for the first part off the canvas

    for (size_t p = 0; p < it.part_count; ++p) {
      const uint32_t k = it.first_part + static_cast<uint32_t>(p);
      const uint32_t begin = part_first[k], end = part_first[k + 1];
      if (end <= begin) continue;
      // An area's holes lie inside part 0's box, and its centred symbol and
      // label are in it, so every part is tested alone.
      double bx0, by0, bx1, by1;
      if (cull && BoxOnSurface(proj, cull_center, part_bounds[k], &bx0, &by0,
                               &bx1, &by1) &&
          (bx1 < 0.0 || by1 < 0.0 || bx0 > size.width || by0 > size.height)) {
        if (reach < 0.0)
          reach = InkReach(sr, px_per_himetric, pixmap_scale,
                           sr.label.valid
                               ? LabelPixelSize(sr.label, ctx.scale_denominator,
                                                meters_per_pixel, label_ref_scale_)
                               : 0.0);
        if (bx1 < -reach || by1 < -reach || bx0 > size.width + reach ||
            by0 > size.height + reach) {
          ++parts_culled_;
          continue;
        }
      }
      if (!ProjectPart(proj, &pts[begin], end - begin, &proj_part)) continue;

      if (it.type == VectorGeometryType::kArea &&
          (sr.fill.valid || sr.area_pattern.valid) && p == 0) {
        // Every ring, clipped alone; the even-odd fill cuts the holes.
        std::vector<std::vector<PixelPoint>> rings;
        std::vector<PixelPoint> ring =
            ClipPolygon(proj_part, size.width, size.height);
        if (ring.size() >= 3) rings.push_back(std::move(ring));
        // The exact projected rings, for the pattern's membership test.
        if (sr.area_pattern.valid) area_rings.assign(1, proj_part);
        std::vector<SurfacePoint> hole;
        for (size_t q = 1; !rings.empty() && q < it.part_count; ++q) {
          const uint32_t hk = it.first_part + static_cast<uint32_t>(q);
          const uint32_t hb = part_first[hk], he = part_first[hk + 1];
          if (he <= hb || !ProjectPart(proj, &pts[hb], he - hb, &hole)) continue;
          std::vector<PixelPoint> h = ClipPolygon(hole, size.width, size.height);
          if (h.size() >= 3) rings.push_back(std::move(h));
          if (sr.area_pattern.valid) area_rings.push_back(hole);
        }
        if (!rings.empty()) {
          // Filled without a pen: the clipped rings run along the canvas
          // edge, and stroking them would draw that edge. The boundary is
          // stroked from the unclipped rings below, once per ring.
          bool filled = !sr.fill.valid;
          const auto fill = [&] {
            if (filled) return;
            canvas->DrawPolyPolygon(rings, &sr.fill.brush, nullptr);
            ++draws_emitted_;
            filled = true;
          };
          if (sr.area_pattern.valid) {
            const ResolvedSymbol pat =
                ResolveSymbol(style_.get(), sr.area_pattern.symbol_id);
            if (pat.drawable()) {
              const AreaPatternStyle& ap = sr.area_pattern;
              // Whole-pixel cells, so the tile repeats exactly.
              const int cell_w =
                  (std::max)(1, static_cast<int>(std::lround(ap.spacing_x)));
              const int cell_h =
                  (std::max)(1, static_cast<int>(std::lround(ap.spacing_y)));
              const double pat_himetric = px_per_himetric * ap.symbol_scale;
              const double pat_pixmap = pixmap_scale * ap.symbol_scale;
              const PatternTileKey key(ap.symbol_id, cell_w, cell_h,
                                       ap.staggered, pat_himetric, pat_pixmap);
              PatternFill& pf = pattern_fills[key];
              if (pf.tile == nullptr) {
                // A staggered pattern repeats every two rows.
                PatternAnchor(proj, pattern_seed_x, pattern_seed_y, cell_w,
                              cell_h * (ap.staggered ? 2 : 1), &pf.anchor_x,
                              &pf.anchor_y);
                auto cached = pattern_tiles_.find(key);
                if (cached == pattern_tiles_.end()) {
                  cached = pattern_tiles_
                               .emplace(key, PatternTile(BuildPatternTile(
                                                 pat, cell_w, cell_h,
                                                 ap.staggered, pat_himetric,
                                                 pat_pixmap,
                                                 SymbolReach(style_.get(),
                                                             ap.symbol_id,
                                                             pat_himetric,
                                                             pat_pixmap))))
                               .first;
                }
                pf.tile = &cached->second;
              }
              // The pattern is a screen-aligned texture: it does not turn with
              // the chart, and it is clipped to the area like the solid fill.
              // The solid fill goes down in the same pass over the rings.
              const Status tiled = canvas->FillPolyPolygonTiled(
                  rings, *pf.tile,
                  static_cast<int>(std::lround(pf.anchor_x)),
                  static_cast<int>(std::lround(pf.anchor_y)),
                  filled ? nullptr : &sr.fill.brush);
              if (tiled.code != kUnsupported) {
                draws_emitted_ += filled ? 1 : 2;
                filled = true;
              } else {
                // A canvas with no tiled fill gets whole stamps at the sites
                // inside the exact rings.
                fill();
                for (const SurfacePoint& at :
                     PlaceOverArea(area_rings, cell_w, cell_h, ap.staggered,
                                   pf.anchor_x, pf.anchor_y, size.width,
                                   size.height)) {
                  DrawResolvedSymbol(canvas, pat, at.x, at.y, pat_himetric,
                                     pat_pixmap, 0.0, nullptr);
                  ++draws_emitted_;
                }
              }
            }
          }
          fill();
          if (pick_enabled_) pick_.AddFill(it.ref, sr.priority, rings[0]);
        }
      }

      const bool ring = it.type == VectorGeometryType::kArea;
      if ((sr.stroke.valid || sr.line_pattern.valid) && proj_part.size() >= 2)
        StrokeRuns(proj_part, unstroked.empty() ? nullptr : &unstroked[begin],
                   ring, &stroke_runs);
      else
        stroke_runs.clear();

      for (const std::vector<SurfacePoint>& path :
           sr.stroke.valid ? stroke_runs : kNoRuns) {
        const double half = std::max(0.5, sr.stroke.pen.width / 2.0);
        // A dashed stroke is placed along the WHOLE projected path and clipped
        // afterwards, for the same reason the pattern branch below is: the
        // rasterizer would start its cycle at the first point it is handed,
        // which on a line that leaves the canvas is wherever the clipper cut
        // it, so every pan of one pixel would move every dash. Placed here,
        // the cycle is measured from the path's own first vertex — a property
        // of the geometry, not of the viewport — and two styles over the same
        // geometry with the same pattern produce the same dash pieces, which
        // is what lets a wider pen case a narrower one dash for dash.
        const std::vector<PathRun> dash = DashRuns(sr.stroke.pen.dash);
        if (dash.empty()) {
          for (auto& run : ClipPolyline(path, size.width, size.height)) {
            canvas->DrawLines(run, sr.stroke.pen);
            ++draws_emitted_;
            if (pick_enabled_) pick_.AddStroke(it.ref, sr.priority, run, half);
          }
        } else {
          Pen solid = sr.stroke.pen;
          solid.dash.clear();
          const PathPlacement placed =
              PlaceAlongPath(path, dash, sr.stroke.dash_phase);
          for (const std::vector<SurfacePoint>& d : placed.dashes) {
            for (auto& run : ClipPolyline(d, size.width, size.height)) {
              canvas->DrawLines(run, solid);
              ++draws_emitted_;
            }
          }
          // Picked along the whole line, not dash by dash: a tap that lands in
          // a gap is still a tap on the road.
          if (pick_enabled_) {
            for (auto& run : ClipPolyline(path, size.width, size.height))
              pick_.AddStroke(it.ref, sr.priority, run, half);
          }
        }
      }

      // A patterned line (GeoSym SAMI, S-52 LC) is laid along the WHOLE
      // projected path and clipped afterwards, not along the clipped runs:
      // clipping first would restart the pattern at the canvas edge, so a
      // dash would jump every time the map panned by one pixel.
      const bool patterned =
          sr.line_pattern.valid && !sr.line_pattern.runs.empty();
      for (const std::vector<SurfacePoint>& path :
           patterned ? stroke_runs : kNoRuns) {
        const PathPlacement placed =
            PlaceAlongPath(path, sr.line_pattern.runs,
                           sr.line_pattern.phase);
        const double half = (std::max)(0.5, sr.line_pattern.pen.width / 2.0);
        for (const std::vector<SurfacePoint>& dash : placed.dashes) {
          for (auto& run : ClipPolyline(dash, size.width, size.height)) {
            canvas->DrawLines(run, sr.line_pattern.pen);
            ++draws_emitted_;
            if (pick_enabled_) pick_.AddStroke(it.ref, sr.priority, run, half);
          }
        }
        std::unique_ptr<PathArc> arc;  // built on the first bent stamp
        for (const PlacedSymbol& ps : placed.symbols) {
          if (ps.x < -1e3 || ps.y < -1e3 || ps.x > size.width + 1e3 ||
              ps.y > size.height + 1e3)
            continue;
          InkBox ink;
          // Resolved per stamp: a pattern's runs can name different symbols.
          const ResolvedSymbol rs = ResolveSymbol(style_.get(), ps.symbol_id);
          if (ps.bend && rs.vec != nullptr) {
            if (!arc) arc = std::make_unique<PathArc>(path);
            DrawSymbolAlongPath(canvas, *rs.vec, *arc, ps.along, ps.offset,
                                px_per_himetric * ps.scale,
                                pick_enabled_ ? &ink : nullptr);
          } else if (!DrawResolvedSymbol(canvas, rs, ps.x, ps.y,
                                         px_per_himetric * ps.scale,
                                         pixmap_scale * ps.scale,
                                         ps.rotation_deg * kPi / 180.0,
                                         pick_enabled_ ? &ink : nullptr)) {
            continue;
          }
          ++draws_emitted_;
          if (pick_enabled_) {
            ink.Add(ps.x, ps.y);
            pick_.AddBox(it.ref, sr.priority, ink.ToRect(1.0));
          }
        }
      }

      // A single point symbol, placed by PartAnchor. Repeated symbology along
      // a path is line_pattern, above.
      SurfacePoint a;
      GeoPoint a_geo;
      if (sr.symbol.valid && PartAnchor(sr.symbol.anchor, it, p, proj, proj_part,
                                        &pts[begin], &a, &a_geo)) {
        if (a.x >= -1e4 && a.y >= -1e4 && a.x <= size.width + 1e4 &&
            a.y <= size.height + 1e4) {
          InkBox ink;
          if (DrawResolvedSymbol(
                  canvas, ResolveSymbol(style_.get(), sr.symbol.symbol_id),
                  a.x, a.y, px_per_himetric * sr.symbol.scale,
                  pixmap_scale * sr.symbol.scale,
                  (sr.symbol.turns_with_chart
                       ? SymbolAngleOnChart(
                             sr.symbol.rotation_deg,
                             affine ? chart_rotation
                                    : NorthOnChartAt(proj, a_geo,
                                                     chart_rotation))
                       : sr.symbol.rotation_deg) *
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
                                 affine ? meters_per_pixel
                                        : MetersPerPixelAt(proj, pts[begin]),
                                 label_ref_scale_);
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
            const std::vector<std::string> glyphs = SplitGlyphs(sr.label.text);
            if (GlyphAdvances(canvas, sr.label.text, ts, &adv)) {
              for (const PlacedTextRun& run : PlaceTextAlongPath(
                       proj_part, adv, sr.label.spacing_px,
                       sr.label.max_angle_deg,
                       sr.label.offset_px +
                           AlongPathAnchorShift(sr.label, ts.size),
                       sr.label.keep_upright)) {
                if (run.glyphs.empty()) continue;
                const PlacedGlyph& mid = run.glyphs[run.glyphs.size() / 2];
                if (repeats.enabled() && repeats.Near(sr.label.text, mid.x, mid.y)) continue;
                if (collisions.enabled()) {
                  // One disc per glyph, centred on its face: baseline-left
                  // origin, half an advance along the run, half a cap height up.
                  run_discs.clear();
                  const double cap = ts.size * kCapHeightEm;
                  for (const PlacedGlyph& g : run.glyphs) {
                    const double w = adv[g.index];
                    const double c = std::cos(g.angle_rad), sn = std::sin(g.angle_rad);
                    run_discs.push_back({g.x + c * w / 2 - sn * cap / 2,
                                         g.y - sn * w / 2 - c * cap / 2,
                                         std::max(w, cap) / 2 + halo_px});
                  }
                  if (collisions.Hits(run_discs)) continue;
                  collisions.Take(run_discs);
                }
                if (repeats.enabled()) repeats.Add(sr.label.text, mid.x, mid.y);
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
                    const std::string& gs = glyphs[g.index];
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
                      glyphs[g.index], g.x, g.y, g.angle_rad,
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
          } else if (PartAnchor(sr.label.anchor, it, p, proj, proj_part,
                                &pts[begin], &a, &a_geo)) {
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

            const bool filtered = repeats.enabled() && !sr.label.skip_repeat_filter;
            const bool repeated =
                filtered && repeats.Near(sr.label.text, a.x, a.y);
            if (!repeated && lx > -1000 && ly > -1000 &&
                lx < size.width + 1000 && ly < size.height + 1000) {
              if (filtered) repeats.Add(sr.label.text, a.x, a.y);
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
