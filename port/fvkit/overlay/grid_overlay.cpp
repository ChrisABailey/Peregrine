// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// GridOverlay — see fvkit/overlay/grid.h.

#include "fvkit/overlay/grid.h"

#include <cmath>
#include <vector>

namespace fv {

namespace {
const double kIntervals[] = {30, 10, 5, 1, 0.5, 0.25, 0.1, 0.05, 0.025, 0.01};
}  // namespace

Status GridOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  // pick the smallest interval that still spaces lines >= min_spacing_px
  double interval = kIntervals[0];
  for (double iv : kIntervals)
    if (iv / proj.DegPerPixelLat() >= min_spacing_px_) interval = iv;

  const GeoRect b = proj.VmapBounds();
  const Pen pen{color_, 1, {}};
  PixelSize surf = proj.SurfaceSize();
  Status s;

  // parallels (constant lat): horizontal lines
  double lat0 = std::ceil(b.ll.lat / interval) * interval;
  for (double lat = lat0; lat <= b.ur.lat + 1e-12; lat += interval) {
    double sx, sy;
    if (!proj.GeoToSurface({lat, proj.Center().lon}, &sx, &sy).ok()) continue;
    int y = (int)std::lround(sy);
    if (y < 0 || y >= surf.height) continue;
    s = canvas.DrawLines({{0, y}, {surf.width - 1, y}}, pen);
    if (!s.ok()) return s;
  }

  // meridians (constant lon): vertical lines. Work in a lon window
  // unwrapped around the center so antimeridian viewports draw correctly.
  double west = UnwrapLonNear(b.ll.lon, proj.Center().lon);
  double east = UnwrapLonNear(b.ur.lon, proj.Center().lon);
  if (east < west) east += 360.0;
  double lon0 = std::ceil(west / interval) * interval;
  for (double lon = lon0; lon <= east + 1e-12; lon += interval) {
    double sx, sy;
    if (!proj.GeoToSurface({proj.Center().lat, NormalizeLon(lon)}, &sx, &sy)
             .ok())
      continue;
    int x = (int)std::lround(sx);
    if (x < 0 || x >= surf.width) continue;
    s = canvas.DrawLines({{x, 0}, {x, surf.height - 1}}, pen);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

}  // namespace fv
