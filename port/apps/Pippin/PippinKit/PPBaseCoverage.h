// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PPBaseCoverage.h — may the cached base map be shown for this camera?
// Pure C++ and header-only, like `PPCameraFit.h` and `PPFollowCadence.h`, so
// the mac gtest owns it rather than the simulator.
//
// The base map is drawn into a surface larger than the screen, a guard band,
// and kept; the overlay (route, points, ownship) is drawn every frame into
// its own transparent surface at the live camera; and the display composites
// the two, applying the scale, turn and offset it already applies to the last
// frame as `MapScreen`'s preview transform. So the band needs no blitter of
// its own: the compositor is the blitter, on the GPU, and handles rotation
// and scale for free, which is what lets a band survive course-up.
//
// This file is the hit test, and it asks three questions in order.
//
//   1. Is the scale exactly the same? Both the tile source and the style
//      engine choose a zoom level from the scale, so a base reused across a
//      zoom is a picture the style never asked for; `VectorScene::CanServe`
//      refuses for the same reason. A pinch therefore misses every frame,
//      which is what it did before the cache existed. See `band` in
//      `PPMap.h` for why a pinch must not be charged for the band either.
//
//   2. Has the chart turned too far? A quality limit rather than a coverage
//      one: the corner test below would allow a much bigger angle out of a
//      big band, but every degree of turn is resampled by the compositor and
//      a map is the one thing here nobody wants softened. Compared the short
//      way, since 359.9 and 0.1 are a fifth of a degree apart and a
//      subtraction calls them 360.
//
//   3. Does the band still cover the screen? The four corners of the live
//      surface, put through the live projection into the ground and back
//      through the base's, must land inside the base surface. Both transforms
//      are affine, so four corners settle a rectangle exactly, with no
//      sampling and no margin fraction to tune. Eight multiply-adds, which is
//      why it can be asked every frame.
//
// Under the base sits the underlay: the map two zoom levels out at screen
// size (a square of the long side when tilted), built when the loop is idle, so whatever the band does not reach is
// shown soft rather than blank. `ScreenCoverage` says which layer reaches
// every part of the live screen, and `UnderlayServes` when the underlay is
// due to be rebuilt.
//
// The band is kept at rest. It grows by whole, equal pixels on each side, so a
// band drawn at the live camera lands on the screen's pixels and is shown
// unfiltered (`PixelAligned`); a drag then starts covered. During a gesture
// the shell redraws the band early, when the screen predicted one draw ahead
// comes within a fraction of the edge (`BandHeadroomPx`), and draws it ahead
// of the finger (`BandLead`).
//
// Nothing here is about content. A route replanning, a point being dragged
// and the ownship moving are all overlay changes, and the overlay is redrawn
// every frame regardless. What does invalidate the base from outside is the
// style engine's own epoch — the reference latitude stepping, a mariner
// setting — which is `PPMap`'s business, because only `PPMap` can see it.

#ifndef PIPPIN_PPBASECOVERAGE_H_
#define PIPPIN_PPBASECOVERAGE_H_

#include <algorithm>
#include <cmath>

#include "fvkit/nav/camera_slew.h"  // fv::ShortestRotationDelta
#include "fvkit/proj.h"

namespace pippin {

struct BaseCoverageLimits {
  // Degrees of chart turn the cached base may be shown through. A quality
  // number; the corner test would allow far more.
  double max_rotation_delta_deg = 2.5;

  // How far inside the band a live corner must land, in base-surface pixels.
  // The compositor filters when the transform is not the identity, so a
  // corner exactly on the last row would be sampled half from a row that does
  // not exist. One pixel is enough.
  //
  // Not charged when the transform is the identity, which is the most
  // valuable hit this cache has: a fix arriving under a camera that has not
  // moved is the same base map with a chevron in a new place, and it must be
  // served even at a zero margin, where the screen is the band and every
  // corner sits on an edge.
  double edge_inset_px = 1.0;
};

// The size of the guard band in device pixels. `margin` is the fraction of
// each axis added on each side, so 0.25 is 2.25x the pixels. Each side grows
// by a whole number of pixels and both sides by the same number, so the
// band's centre pixel lands on the screen's and a band drawn at the live
// camera composites without resampling (`PixelAligned`).
//
// `rotation_safe` grows each axis to the screen's diagonal plus half the
// normal margin of the short side, so any turn of the screen inside the band
// keeps its corners covered. At 0.25 on a 393x852 pt screen that is about
// 3.2x the screen's pixels; the diagonal alone would leave 43 pt of headroom
// top and bottom, less than the refresh threshold.
inline void GrownSurfacePixels(int width, int height, double margin,
                               bool rotation_safe, int* out_width,
                               int* out_height) {
  const double m = (margin > 0.0 && std::isfinite(margin)) ? margin : 0.0;
  const double w = width, h = height;
  double gx = std::ceil(w * m);
  double gy = std::ceil(h * m);
  if (rotation_safe) {
    const double reach = std::sqrt(w * w + h * h) / 2.0 +
                         0.5 * std::ceil((w < h ? w : h) * m);
    gx = std::ceil(reach - w / 2.0);
    gy = std::ceil(reach - h / 2.0);
    if (gx < 0.0) gx = 0.0;
    if (gy < 0.0) gy = 0.0;
  }
  *out_width = width + 2 * (int)gx;
  *out_height = height + 2 * (int)gy;
}

/// The four live-surface points the screen shows: `xs`/`ys` when given,
/// otherwise the outer corners of the live surface.
inline void LiveCorners(const fv::MapProjection& live, const double* xs,
                        const double* ys, double out_x[4], double out_y[4]) {
  if (xs != nullptr && ys != nullptr) {
    for (int i = 0; i < 4; ++i) out_x[i] = xs[i], out_y[i] = ys[i];
    return;
  }
  const fv::PixelSize s = live.SurfaceSize();
  const double r = (double)s.width - 0.5, b = (double)s.height - 0.5;
  const double x[4] = {-0.5, r, r, -0.5};
  const double y[4] = {-0.5, -0.5, b, b};
  for (int i = 0; i < 4; ++i) out_x[i] = x[i], out_y[i] = y[i];
}

// True when the base map drawn for `base` can be composited for `live`
// without being drawn again.
//
// `xs`/`ys`, when given, are the four live-surface points the screen actually
// shows (a tilted camera's ground quad, `Perspective::GroundQuad`); otherwise
// the live surface's own corners. The quad is convex, so its corners settle it.
inline bool BaseCovers(const fv::MapProjection& base,
                       const fv::MapProjection& live,
                       const BaseCoverageLimits& limits = {},
                       const double* xs = nullptr, const double* ys = nullptr) {
  if (!base.Ready() || !live.Ready()) return false;

  // 1. The scale, exactly. Both are physical-scale projections, so the pitch
  //    must match too: the same 1:N on a different pitch is a different
  //    number of ground metres per pixel.
  if (!(base.Scale() == live.Scale())) return false;
  if (!(base.MmPerPixel() == live.MmPerPixel())) return false;

  // 2. The turn, the short way.
  const double turn =
      std::fabs(fv::ShortestRotationDelta(base.Rotation(), live.Rotation()));
  if (!(turn <= limits.max_rotation_delta_deg)) return false;

  // 3. The corners. A surface of w pixels covers [-0.5, w-0.5] about its
  //    pixel centres, FalconView's convention which `MapProjection` keeps, so
  //    the extremes are half a pixel outside the first and last centre.
  const fv::PixelSize lsz = live.SurfaceSize();
  const fv::PixelSize bsz = base.SurfaceSize();
  if (lsz.width <= 0 || lsz.height <= 0 || bsz.width <= 0 || bsz.height <= 0)
    return false;

  // Nothing is resampled when the base and the live camera are the same
  // picture, so nothing is inset either. See `edge_inset_px`.
  const bool identity = xs == nullptr && bsz.width == lsz.width &&
                        bsz.height == lsz.height &&
                        base.Center().lat == live.Center().lat &&
                        base.Center().lon == live.Center().lon &&
                        base.Rotation() == live.Rotation();
  const double inset = identity ? 0.0 : limits.edge_inset_px;

  double cx[4], cy[4];
  LiveCorners(live, xs, ys, cx, cy);
  const double lo = -0.5 + inset;
  const double hi_x = (double)bsz.width - 0.5 - inset;
  const double hi_y = (double)bsz.height - 0.5 - inset;
  if (hi_x < lo || hi_y < lo) return false;

  for (int i = 0; i < 4; ++i) {
    fv::GeoPoint g;
    if (!live.SurfaceToGeo(cx[i], cy[i], &g).ok()) return false;
    double bx = 0.0, by = 0.0;
    if (!base.GeoToSurface(g, &bx, &by).ok()) return false;
    if (!std::isfinite(bx) || !std::isfinite(by)) return false;
    if (bx < lo || bx > hi_x || by < lo || by > hi_y) return false;
  }
  return true;
}

// True when the base's pixels land exactly on the live screen's: the same
// scale, pitch and rotation, the screen's pixel centres on whole base pixels,
// and the screen inside the base. Such a base is shown unfiltered and needs no
// settle frame, whatever its size and wherever the screen sits in it.
inline bool PixelAligned(const fv::MapProjection& base,
                         const fv::MapProjection& live) {
  if (!base.Ready() || !live.Ready()) return false;
  if (!(base.Scale() == live.Scale())) return false;
  if (!(base.MmPerPixel() == live.MmPerPixel())) return false;
  if (!(std::fabs(fv::ShortestRotationDelta(base.Rotation(), live.Rotation())) <
        1e-9))
    return false;
  const fv::PixelSize lsz = live.SurfaceSize();
  const fv::PixelSize bsz = base.SurfaceSize();
  if (lsz.width <= 0 || lsz.height <= 0 || bsz.width < lsz.width ||
      bsz.height < lsz.height)
    return false;
  // Two opposite corners pin an affine map with no turn and no scale: both
  // must land on whole pixels a screen apart. The projection's x scale follows
  // the centre latitude, so after a north-south pan the two grids differ by
  // thousandths of a pixel across the screen; unfiltered sampling still picks
  // the same pixel for anything under a tenth.
  constexpr double kTol = 0.1;
  const double lx[2] = {0.0, (double)lsz.width - 1.0};
  const double ly[2] = {0.0, (double)lsz.height - 1.0};
  double bx[2], by[2];
  for (int i = 0; i < 2; ++i) {
    fv::GeoPoint g;
    if (!live.SurfaceToGeo(lx[i], ly[i], &g).ok()) return false;
    if (!base.GeoToSurface(g, &bx[i], &by[i]).ok()) return false;
    if (!std::isfinite(bx[i]) || !std::isfinite(by[i])) return false;
    if (std::fabs(bx[i] - std::round(bx[i])) > kTol ||
        std::fabs(by[i] - std::round(by[i])) > kTol)
      return false;
  }
  if (std::fabs((bx[1] - bx[0]) - lx[1]) > kTol ||
      std::fabs((by[1] - by[0]) - ly[1]) > kTol)
    return false;
  const double ox = std::round(bx[0]);
  const double oy = std::round(by[0]);
  return ox >= 0.0 && oy >= 0.0 && ox + lsz.width <= bsz.width &&
         oy + lsz.height <= bsz.height;
}

// How far the live screen is from the base's edge, in base pixels: the
// smallest distance from any live corner to any edge of the base surface.
// Negative when a corner is already past the edge. Scale and turn are not
// refused; this is geometry only, asked of a camera predicted one draw ahead.
// `xs`/`ys` as in `BaseCovers`.
inline double BandHeadroomPx(const fv::MapProjection& base,
                             const fv::MapProjection& live,
                             const double* xs = nullptr,
                             const double* ys = nullptr) {
  const double kNone = -1e300;
  if (!base.Ready() || !live.Ready()) return kNone;
  const fv::PixelSize lsz = live.SurfaceSize();
  const fv::PixelSize bsz = base.SurfaceSize();
  if (lsz.width <= 0 || lsz.height <= 0 || bsz.width <= 0 || bsz.height <= 0)
    return kNone;
  double cx[4], cy[4];
  LiveCorners(live, xs, ys, cx, cy);
  const double hi_x = (double)bsz.width - 0.5;
  const double hi_y = (double)bsz.height - 0.5;
  double least = 1e300;
  for (int i = 0; i < 4; ++i) {
    fv::GeoPoint g;
    if (!live.SurfaceToGeo(cx[i], cy[i], &g).ok()) return kNone;
    double bx = 0.0, by = 0.0;
    if (!base.GeoToSurface(g, &bx, &by).ok()) return kNone;
    if (!std::isfinite(bx) || !std::isfinite(by)) return kNone;
    const double d[4] = {bx + 0.5, hi_x - bx, by + 0.5, hi_y - by};
    for (double v : d) least = v < least ? v : least;
  }
  return least;
}

// The distance to lead the band by along one screen axis, in points: the
// pan velocity times the expected draw time, capped at `max_fraction` of the
// band's margin on that axis so the band still covers the screen if the
// finger stops before the draw lands.
inline double BandLead(double velocity_pt_s, double draw_seconds,
                       double margin_pt, double max_fraction = 0.75) {
  if (!std::isfinite(velocity_pt_s) || !(draw_seconds > 0.0) ||
      !(margin_pt > 0.0))
    return 0.0;
  const double cap = margin_pt * max_fraction;
  const double lead = velocity_pt_s * draw_seconds;
  return lead > cap ? cap : (lead < -cap ? -cap : lead);
}

// How much further out the underlay is drawn than the camera it serves.
// Four screens of ground on a screen of pixels in each axis: more than the
// half-diagonal any turn reaches, and about a screen and a half of pan either
// way from its centre.
constexpr double kUnderlayZoomOut = 4.0;

// True when the live surface point (x, y) lands on `layer`'s surface. The
// half-pixel convention of `BaseCovers`, with no inset: this is a question
// about what is on screen, not about what the compositor samples.
inline bool LayerReaches(const fv::MapProjection& layer,
                         const fv::MapProjection& live, double x, double y) {
  if (!layer.Ready()) return false;
  const fv::PixelSize sz = layer.SurfaceSize();
  if (sz.width <= 0 || sz.height <= 0) return false;
  fv::GeoPoint g;
  if (!live.SurfaceToGeo(x, y, &g).ok()) return false;
  double lx = 0.0, ly = 0.0;
  if (!layer.GeoToSurface(g, &lx, &ly).ok()) return false;
  if (!std::isfinite(lx) || !std::isfinite(ly)) return false;
  return lx >= -0.5 && lx <= (double)sz.width - 0.5 && ly >= -0.5 &&
         ly <= (double)sz.height - 0.5;
}

// What the live screen shows, from best to worst.
enum class ScreenCover {
  kSharp,       // the base reaches every corner
  kUnderlay,    // some corner is past the base but inside the underlay
  kBackground,  // some corner is past both: the style's background colour
};

// Which layers reach the four corners of the live screen, reported as the
// worst of them. Both layers are rectangles under affine maps, so a layer
// that reaches all four corners covers the whole screen. Scale and turn are
// not refused here, unlike `BaseCovers`: during a pinch the base is scaled
// on screen and still covers what it covers. `underlay` may be null.
// `xs`/`ys` as in `BaseCovers`.
inline ScreenCover ScreenCoverage(const fv::MapProjection& base,
                                  const fv::MapProjection* underlay,
                                  const fv::MapProjection& live,
                                  const double* xs = nullptr,
                                  const double* ys = nullptr) {
  const fv::PixelSize lsz = live.SurfaceSize();
  if (!live.Ready() || lsz.width <= 0 || lsz.height <= 0)
    return ScreenCover::kSharp;
  double cx[4], cy[4];
  LiveCorners(live, xs, ys, cx, cy);
  ScreenCover worst = ScreenCover::kSharp;
  for (int i = 0; i < 4; ++i) {
    if (LayerReaches(base, live, cx[i], cy[i])) continue;
    if (underlay != nullptr && LayerReaches(*underlay, live, cx[i], cy[i])) {
      worst = ScreenCover::kUnderlay;
      continue;
    }
    return ScreenCover::kBackground;
  }
  return worst;
}

// How far inside the underlay's edge a tilted screen's ground quad must stay
// before the underlay counts as stale, as a fraction of its surface. The
// rebuild is started while the quad is still covered, not once it is not.
constexpr double kUnderlayQuadInset = 1.0 / 16.0;

// The underlay's surface, in pixels, for a live screen of `w` x `h`. Flat, it
// is the screen. Tilted (`xs`/`ys` the ground quad, as in `BaseCovers`), it is
// a square of at least the long side, so the quad fits inside it at any turn
// with room to spare: the quad's farthest corner from the screen centre sits
// no more than 0.4 of the side out once scaled down by `zoom_out`.
inline void UnderlaySurfacePixels(int w, int h, const double* xs,
                                  const double* ys, int* out_w, int* out_h,
                                  double zoom_out = kUnderlayZoomOut) {
  *out_w = w;
  *out_h = h;
  if (xs == nullptr || ys == nullptr || w <= 0 || h <= 0 || !(zoom_out > 0.0))
    return;
  const double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
  double reach = 0.0;
  for (int i = 0; i < 4; ++i)
    reach = std::max(reach, std::hypot(xs[i] - cx, ys[i] - cy));
  double side = std::max(w, h);
  if (std::isfinite(reach)) side = std::max(side, std::ceil(2.5 * reach / zoom_out));
  *out_w = *out_h = (int)side;
}

// True while the underlay built for one camera is still good for `live`.
// It is rebuilt when the live centre leaves the middle half of its surface,
// when the live scale has moved more than 2x from the one it was built for
// (`underlay.Scale() / zoom_out`), or when the screen outgrew it. Turning
// never stales a flat screen's underlay: four screens of ground cover any
// rotation. A tilted screen (`xs`/`ys` its ground quad) reaches farther, so
// its quad's corners must also stay `kUnderlayQuadInset` inside the edge.
inline bool UnderlayServes(const fv::MapProjection& underlay,
                           const fv::MapProjection& live,
                           const double* xs = nullptr,
                           const double* ys = nullptr,
                           double zoom_out = kUnderlayZoomOut) {
  if (!underlay.Ready() || !live.Ready() || !(zoom_out > 0.0)) return false;
  const fv::PixelSize usz = underlay.SurfaceSize();
  const fv::PixelSize lsz = live.SurfaceSize();
  // Larger is fine: a tilted screen's square underlay still serves the same
  // screen flattened by a drag.
  if (usz.width < lsz.width || usz.height < lsz.height) return false;
  if (!(underlay.MmPerPixel() == live.MmPerPixel())) return false;

  const double key = underlay.Scale() / zoom_out;
  const double ratio = live.Scale() / key;
  if (!(ratio >= 0.5 && ratio <= 2.0)) return false;

  double cx = 0.0, cy = 0.0;
  if (!underlay.GeoToSurface(live.Center(), &cx, &cy).ok()) return false;
  const double w = (double)usz.width;
  const double h = (double)usz.height;
  if (!(cx >= 0.25 * w && cx <= 0.75 * w && cy >= 0.25 * h && cy <= 0.75 * h))
    return false;
  if (xs == nullptr || ys == nullptr) return true;

  const double ix = kUnderlayQuadInset * w, iy = kUnderlayQuadInset * h;
  for (int i = 0; i < 4; ++i) {
    fv::GeoPoint g;
    if (!live.SurfaceToGeo(xs[i], ys[i], &g).ok()) return false;
    double ux = 0.0, uy = 0.0;
    if (!underlay.GeoToSurface(g, &ux, &uy).ok()) return false;
    if (!(ux >= ix && ux <= w - ix && uy >= iy && uy <= h - iy)) return false;
  }
  return true;
}

}  // namespace pippin

#endif  // PIPPIN_PPBASECOVERAGE_H_
