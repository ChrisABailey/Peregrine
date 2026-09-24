// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MapEngine — see fvkit/engine.h.

#include "fvkit/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "fv_map_enums.h"  // MapScaleUnitsEnum
#include "fvkit/formats/registry.h"
#include "fvkit/raster_warp.h"

namespace fv {

MapEngine::MapEngine(std::shared_ptr<Catalog> catalog,
                     size_t source_cache_capacity)
    : catalog_(std::move(catalog)),
      cache_capacity_(source_cache_capacity == 0 ? 1 : source_cache_capacity) {}
MapEngine::~MapEngine() = default;

Status MapEngine::SetSurfaceDimensions(int width, int height) {
  return proj_.SetSurfaceSize(width, height);
}
Status MapEngine::SetCenter(const GeoPoint& center) {
  return proj_.SetCenter(center);
}
Status MapEngine::SetScale(double scale_denominator) {
  return proj_.SetScale(scale_denominator);
}
Status MapEngine::SetResolution(double dpp_lat, double dpp_lon) {
  return proj_.SetResolution(dpp_lat, dpp_lon);
}

Status MapEngine::SetPhysicalScale(double series_scale, int series_scale_units,
                                   double mm_per_pixel) {
  if (series_scale <= 0)
    return Status::Error(kInvalidArg, "series scale must be positive");

  double denominator = series_scale;
  if (series_scale_units != MAP_SCALE_DENOMINATOR) {
    // A ground-resolution series (imagery). Convert to metres/pixel, then to
    // the denominator that makes it 100% (one source pixel per screen pixel)
    // at the reference display pitch: 1 source px covers `metres` of ground,
    // shown across kNativeDisplayMmPerPixel of screen, i.e. scale =
    // metres / (kNativeDisplayMmPerPixel / 1000). mm_per_pixel then scales
    // from there, so at the reference pitch imagery lands exactly at 100%.
    double metres = series_scale;
    if (series_scale_units == MAP_SCALE_KILOMETER)
      metres = series_scale * 1000.0;
    else if (series_scale_units != MAP_SCALE_METERS)
      return Status::Error(kInvalidArg, "unsupported series scale units");
    denominator = metres * 1000.0 / kNativeDisplayMmPerPixel;
  }
  return proj_.SetPhysicalScale(denominator, mm_per_pixel);
}

Status MapEngine::SetRotation(double degrees) {
  return proj_.SetRotation(degrees);
}

Status MapEngine::SetProjectionType(ProjectionType type) {
  return proj_.SetProjectionType(type);
}

void MapEngine::SetElevationSource(std::shared_ptr<IElevationSource> src) {
  elevation_ = std::move(src);
}

Status MapEngine::GetElevation(const GeoPoint& p, float* elevation_meters) {
  if (elevation_ == nullptr)
    return Status::Error(kNotFound, "no elevation source attached");
  return elevation_->GetElevation(p, elevation_meters);
}

std::shared_ptr<IRasterSource> MapEngine::SourceFor(const CoverageRow& row,
                                                    Status* s) {
  for (auto it = source_cache_.begin(); it != source_cache_.end(); ++it) {
    if (it->path == row.path) {
      source_cache_.splice(source_cache_.begin(), source_cache_, it);
      *s = Status::Ok();
      return source_cache_.front().source;
    }
  }
  const FormatFactories* fmt = FindFormat(row.format);
  if (fmt == nullptr || !fmt->make_raster_source) {
    *s = Status::Error(kUnsupported, "no raster source for format: " + row.format);
    return nullptr;
  }
  auto src = fmt->make_raster_source();
  *s = src->Open(row.path);
  if (!s->ok()) return nullptr;
  source_cache_.push_front(CacheEntry{row.path, src});
  if (source_cache_.size() > cache_capacity_) source_cache_.pop_back();
  return src;
}

namespace {

/// Returns `g` with its longitude moved to the frame's side of the
/// antimeridian. SurfaceToGeo normalizes to (-180, 180], so a point on the
/// west edge of a -180..0 frame can come back as +180; the source would read
/// that as 360 degrees east of its origin. Frames that cross the antimeridian
/// are passed through as before.
GeoPoint SourceGeo(GeoPoint g, const GeoRect& frame) {
  if (!frame.CrossesAntimeridian())
    g.lon = UnwrapLonNear(g.lon, (frame.ll.lon + frame.ur.lon) / 2.0);
  return g;
}

}  // namespace

Status MapEngine::OverlapToSurface(double lat, double lon, double* sx,
                                   double* sy) const {
  const double ref = proj_.Center().lon;
  if (lon - ref >= 180.0 || lon - ref <= -180.0)
    return proj_.GeoToSurfaceUnwrapped({lat, lon}, sx, sy);
  return proj_.GeoToSurface({lat, NormalizeLon(lon)}, sx, sy);
}

Status MapEngine::CompositeRow(const CoverageRow& row, ICanvas& canvas) {
  Status s;
  auto src = SourceFor(row, &s);
  if (src == nullptr) return s;
  ImageInfo info;
  s = src->Info(&info);
  if (!s.ok()) return s;

  const GeoRect view = proj_.VmapBounds();
  const double ref = proj_.Center().lon;

  // geographic overlap, in a longitude frame unwrapped around the center
  double v_west = UnwrapLonNear(view.ll.lon, ref);
  double v_east = UnwrapLonNear(view.ur.lon, ref);
  if (v_east < v_west) v_east += 360.0;  // view crossed the antimeridian
  // A view 360 degrees wide or wider has lost its extent in VmapBounds'
  // -180..180; take it unwrapped from the projection instead.
  double uw_west = 0, uw_east = 0;
  s = proj_.VmapLonRange(&uw_west, &uw_east);
  if (!s.ok()) return s;
  if (uw_east - uw_west >= 360.0) {
    v_west = uw_west;
    v_east = uw_east;
  }
  // frame lon span: for a crossing frame the east edge unwraps to
  // ll.lon + width (e.g. ll=175, ur=-178 -> width 7, east edge 182)
  double f_west = UnwrapLonNear(row.bounds.ll.lon, ref);
  double f_width = row.bounds.CrossesAntimeridian()
                       ? (row.bounds.ur.lon + 360.0) - row.bounds.ll.lon
                       : row.bounds.ur.lon - row.bounds.ll.lon;
  double f_east = f_west + f_width;

  double o_top = std::min(view.ur.lat, row.bounds.ur.lat);
  double o_bot = std::max(view.ll.lat, row.bounds.ll.lat);
  if (o_top <= o_bot) return Status::Ok();  // no overlap

  // The copy nearest the centre (k = 0) is drawn first, then any copy 360
  // degrees away that the view also shows: a wide frame beside a wide view,
  // or any frame when the view wraps the world.
  const int k_lo = (int)std::floor((v_west - f_east) / 360.0);
  const int k_hi = (int)std::ceil((v_east - f_west) / 360.0);
  for (int pass = 0; pass <= k_hi - k_lo + 1; ++pass) {
    const int k = pass == 0 ? 0 : k_lo + pass - 1;
    if (pass > 0 && k == 0) continue;
    const double shift = 360.0 * k;  // exactly 0.0 for k = 0
    double o_west = std::max(v_west, f_west + shift);
    double o_east = std::min(v_east, f_east + shift);
    if (o_east <= o_west) continue;
    // A TURNED CHART IS ITS OWN PATH, and the gate is what keeps rotation 0
    // byte-identical to the projection that had no rotation at all (PR1's
    // rule, and every pinned raster golden is under it).
    if (force_projected_ || !proj_.IsAffine())
      s = CompositeRowProjected(*src, info, row.bounds, o_top, o_bot, o_west,
                                o_east, canvas);
    else
      s = proj_.Rotation() != 0.0
              ? CompositeRowTurned(*src, info, row.bounds, o_top, o_bot,
                                   o_west, o_east, canvas)
              : CompositeRowStraight(*src, info, row.bounds, o_top, o_bot,
                                     o_west, o_east, canvas);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

Status MapEngine::CompositeRowStraight(IRasterSource& src_ref,
                                       const ImageInfo& info,
                                       const GeoRect& frame, double o_top,
                                       double o_bot, double o_west,
                                       double o_east, ICanvas& canvas) {
  IRasterSource* src = &src_ref;
  Status s;
  // target region on the surface (rounded overlap corners), clipped
  double sx0, sy0, sx1, sy1;
  s = OverlapToSurface(o_top, o_west, &sx0, &sy0);
  if (!s.ok()) return s;
  s = OverlapToSurface(o_bot, o_east, &sx1, &sy1);
  if (!s.ok()) return s;
  PixelSize surf = proj_.SurfaceSize();
  int tx = std::max(0, (int)std::lround(sx0));
  int ty = std::max(0, (int)std::lround(sy0));
  int tx1 = std::min(surf.width - 1, (int)std::lround(sx1) - 1);
  int ty1 = std::min(surf.height - 1, (int)std::lround(sy1) - 1);
  int tw = tx1 - tx + 1, th = ty1 - ty + 1;
  if (tw <= 0 || th <= 0) return Status::Ok();

  // source pixel coords at the CLIPPED target corners (exact at corners;
  // linear in between — see header note)
  GeoPoint nw, se;
  s = proj_.SurfaceToGeo(tx, ty, &nw);
  if (!s.ok()) return s;
  s = proj_.SurfaceToGeo(tx1, ty1, &se);
  if (!s.ok()) return s;
  double fx0, fy0, fx1, fy1;
  s = src->GeoToPixel(SourceGeo(nw, frame), &fx0, &fy0);
  if (!s.ok()) return s;
  s = src->GeoToPixel(SourceGeo(se, frame), &fx1, &fy1);
  if (!s.ok()) return s;

  int rx = std::max(0, (int)std::floor(std::min(fx0, fx1)));
  int ry = std::max(0, (int)std::floor(std::min(fy0, fy1)));
  int rx1 = std::min(info.size.width - 1, (int)std::ceil(std::max(fx0, fx1)));
  int ry1 = std::min(info.size.height - 1, (int)std::ceil(std::max(fy0, fy1)));
  int rw = rx1 - rx + 1, rh = ry1 - ry + 1;
  if (rw <= 0 || rh <= 0) return Status::Ok();

  PixelBuffer block;
  s = src->ReadBlock({rx, ry, rw, rh}, &block);
  if (!s.ok()) return s;

  // nearest-neighbor resample into a target-sized buffer
  PixelBuffer out(tw, th);
  for (int y = 0; y < th; ++y) {
    double v = th > 1 ? (double)y / (th - 1) : 0.0;
    int sy = (int)std::lround(fy0 + v * (fy1 - fy0)) - ry;
    sy = std::min(std::max(sy, 0), rh - 1);
    const unsigned char* srow = block.Row(sy);
    unsigned char* drow = out.Row(y);
    for (int x = 0; x < tw; ++x) {
      double u = tw > 1 ? (double)x / (tw - 1) : 0.0;
      int sx = (int)std::lround(fx0 + u * (fx1 - fx0)) - rx;
      sx = std::min(std::max(sx, 0), rw - 1);
      std::memcpy(drow + 4 * x, srow + 4 * sx, 4);
    }
  }
  return canvas.DrawPixmap(out, tx, ty);
}

// The turned blit (PR3). Same trade as the unturned path — an affine fit
// through corner samples of the source's exact transforms, nearest-neighbour
// sampling — with two differences that rotation forces:
//
//   1. THE TARGET REGION IS THE BOX OF A QUAD, so all FOUR overlap corners
//      are projected rather than two. Geo->surface is affine here (a scaling
//      then a rotation), so a geographic rectangle maps to a parallelogram
//      and the box of its corners contains every pixel that can carry frame
//      data. Under no rotation the quad IS the box and the two corners the
//      unturned path takes are enough, which is why it takes two.
//
//   2. THE BLIT IS MASKED, NOT CLAMPED. See the header: the box has corners
//      outside the frame, and a clamp would fill them with the frame's edge
//      pixels smeared out to the corner. An unmapped target pixel is left at
//      alpha 0 — PixelBuffer zero-fills, and DrawPixmap's src-over blend
//      drops a fully transparent pixel — so the frame keeps its own edges.
Status MapEngine::CompositeRowTurned(IRasterSource& src, const ImageInfo& info,
                                     const GeoRect& frame, double o_top,
                                     double o_bot, double o_west,
                                     double o_east, ICanvas& canvas) {
  const GeoPoint quad[4] = {
      {o_top, o_west}, {o_top, o_east}, {o_bot, o_west}, {o_bot, o_east}};
  double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
  for (int i = 0; i < 4; ++i) {
    double sx = 0, sy = 0;
    Status s = OverlapToSurface(quad[i].lat, quad[i].lon, &sx, &sy);
    if (!s.ok()) return s;
    if (i == 0) {
      lo_x = hi_x = sx;
      lo_y = hi_y = sy;
    } else {
      lo_x = std::min(lo_x, sx);
      hi_x = std::max(hi_x, sx);
      lo_y = std::min(lo_y, sy);
      hi_y = std::max(hi_y, sy);
    }
  }
  PixelSize surf = proj_.SurfaceSize();
  int tx = std::max(0, (int)std::floor(lo_x));
  int ty = std::max(0, (int)std::floor(lo_y));
  int tx1 = std::min(surf.width - 1, (int)std::ceil(hi_x));
  int ty1 = std::min(surf.height - 1, (int)std::ceil(hi_y));
  int tw = tx1 - tx + 1, th = ty1 - ty + 1;
  if (tw <= 0 || th <= 0) return Status::Ok();

  // Source pixel coords at three corners of the target box, then the affine
  // that steps between them. Three and not four: the fourth is determined,
  // and taking it from the source's transform as well would let a projected
  // image's non-affine residual make the mapping non-invertible in the
  // corner. Two adds per pixel from here.
  auto src_px = [&](double dx, double dy, double* fx, double* fy) -> Status {
    GeoPoint g;
    Status s = proj_.SurfaceToGeo(dx, dy, &g);
    if (!s.ok()) return s;
    return src.GeoToPixel(SourceGeo(g, frame), fx, fy);
  };
  double f00x, f00y, f10x, f10y, f01x, f01y;
  Status s = src_px(tx, ty, &f00x, &f00y);
  if (!s.ok()) return s;
  s = src_px(tx1, ty, &f10x, &f10y);
  if (!s.ok()) return s;
  s = src_px(tx, ty1, &f01x, &f01y);
  if (!s.ok()) return s;
  const double ax = tw > 1 ? (f10x - f00x) / (tw - 1) : 0.0;
  const double ay = tw > 1 ? (f10y - f00y) / (tw - 1) : 0.0;
  const double bx = th > 1 ? (f01x - f00x) / (th - 1) : 0.0;
  const double by = th > 1 ? (f01y - f00y) / (th - 1) : 0.0;

  // The block to read is the box of the four mapped corners of the target
  // box, taken through that same affine so it cannot disagree with the loop.
  const double f11x = f00x + ax * (tw - 1) + bx * (th - 1);
  const double f11y = f00y + ay * (tw - 1) + by * (th - 1);
  const double xs[4] = {f00x, f10x, f01x, f11x};
  const double ys[4] = {f00y, f10y, f01y, f11y};
  double fx_lo = xs[0], fx_hi = xs[0], fy_lo = ys[0], fy_hi = ys[0];
  for (int i = 1; i < 4; ++i) {
    fx_lo = std::min(fx_lo, xs[i]);
    fx_hi = std::max(fx_hi, xs[i]);
    fy_lo = std::min(fy_lo, ys[i]);
    fy_hi = std::max(fy_hi, ys[i]);
  }
  int rx = std::max(0, (int)std::floor(fx_lo));
  int ry = std::max(0, (int)std::floor(fy_lo));
  int rx1 = std::min(info.size.width - 1, (int)std::ceil(fx_hi));
  int ry1 = std::min(info.size.height - 1, (int)std::ceil(fy_hi));
  int rw = rx1 - rx + 1, rh = ry1 - ry + 1;
  if (rw <= 0 || rh <= 0) return Status::Ok();

  PixelBuffer block;
  s = src.ReadBlock({rx, ry, rw, rh}, &block);
  if (!s.ok()) return s;

  PixelBuffer out(tw, th);  // zero-filled: alpha 0 everywhere until written
  for (int y = 0; y < th; ++y) {
    double fx = f00x + bx * y;
    double fy = f00y + by * y;
    unsigned char* drow = out.Row(y);
    for (int x = 0; x < tw; ++x, fx += ax, fy += ay) {
      int sx = (int)std::lround(fx) - rx;
      int sy = (int)std::lround(fy) - ry;
      if (sx < 0 || sy < 0 || sx >= rw || sy >= rh) continue;  // outside: mask
      std::memcpy(drow + 4 * x, block.Row(sy) + 4 * sx, 4);
    }
  }
  return canvas.DrawPixmap(out, tx, ty);
}

Status MapEngine::CompositeRowProjected(IRasterSource& src,
                                        const ImageInfo& info,
                                        const GeoRect& frame, double o_top,
                                        double o_bot, double o_west,
                                        double o_east, ICanvas& canvas) {
  // The box of the overlap's projected edges. A projected rectangle is not a
  // quad, so each edge is walked; the one-pixel pad covers bulges between
  // samples. An edge point with no image makes the box the whole surface.
  constexpr int kEdgeSteps = 64;
  const PixelSize surf = proj_.SurfaceSize();
  double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
  bool first = true, whole = false;
  for (int i = 0; i <= kEdgeSteps && !whole; ++i) {
    const double t = (double)i / kEdgeSteps;
    const double lat = o_bot + t * (o_top - o_bot);
    const double lon = o_west + t * (o_east - o_west);
    const GeoPoint edge[4] = {
        {o_top, lon}, {o_bot, lon}, {lat, o_west}, {lat, o_east}};
    for (const GeoPoint& g : edge) {
      double sx = 0, sy = 0;
      Status s = OverlapToSurface(g.lat, g.lon, &sx, &sy);
      if (s.code == kNotProjectable) {
        whole = true;
        break;
      }
      if (!s.ok()) return s;
      if (first) {
        lo_x = hi_x = sx;
        lo_y = hi_y = sy;
        first = false;
      } else {
        lo_x = std::min(lo_x, sx);
        hi_x = std::max(hi_x, sx);
        lo_y = std::min(lo_y, sy);
        hi_y = std::max(hi_y, sy);
      }
    }
  }
  // An interior point the projection blows up into an arc is not covered by
  // the image of the boundary, so the box above misses everything it fills:
  // the whole west hemisphere of an Azimuthal Equidistant world lies outside
  // the image of its own two bounding meridians.
  GeoPoint singular;
  if (!whole && proj_.SingularPoint(&singular).ok()) {
    const double lon = UnwrapLonNear(singular.lon, (o_west + o_east) / 2.0);
    whole = singular.lat >= o_bot && singular.lat <= o_top && lon >= o_west &&
            lon <= o_east;
  }
  int tx = 0, ty = 0, tx1 = surf.width - 1, ty1 = surf.height - 1;
  if (!whole) {
    tx = std::max(tx, (int)std::floor(lo_x) - 1);
    ty = std::max(ty, (int)std::floor(lo_y) - 1);
    tx1 = std::min(tx1, (int)std::ceil(hi_x) + 1);
    ty1 = std::min(ty1, (int)std::ceil(hi_y) + 1);
  }
  const int tw = tx1 - tx + 1, th = ty1 - ty + 1;
  if (tw <= 0 || th <= 0) return Status::Ok();

  WarpIndex idx;
  AdaptiveWarp(
      tw, th,
      [&](int x, int y, double* fx, double* fy) {
        GeoPoint g;
        if (!proj_.SurfaceToGeo(tx + x, ty + y, &g).ok()) return false;
        return src.GeoToPixel(SourceGeo(g, frame), fx, fy).ok();
      },
      &idx);

  // Mask to the frame image, and read only the block the kept samples need.
  int rx = info.size.width, ry = info.size.height, rx1 = -1, ry1 = -1;
  for (size_t k = 0; k < idx.sx.size(); ++k) {
    const int sx = idx.sx[k], sy = idx.sy[k];
    if (sx < 0 || sy < 0 || sx >= info.size.width || sy >= info.size.height) {
      idx.sx[k] = kWarpUnmapped;
      continue;
    }
    rx = std::min(rx, sx);
    ry = std::min(ry, sy);
    rx1 = std::max(rx1, sx);
    ry1 = std::max(ry1, sy);
  }
  if (rx1 < rx || ry1 < ry) return Status::Ok();

  PixelBuffer block;
  Status s = src.ReadBlock({rx, ry, rx1 - rx + 1, ry1 - ry + 1}, &block);
  if (!s.ok()) return s;

  PixelBuffer out(tw, th);  // zero-filled: alpha 0 everywhere until written
  size_t k = 0;
  for (int y = 0; y < th; ++y) {
    unsigned char* drow = out.Row(y);
    for (int x = 0; x < tw; ++x, ++k) {
      if (idx.sx[k] == kWarpUnmapped) continue;
      std::memcpy(drow + 4 * x, block.Row(idx.sy[k] - ry) + 4 * (idx.sx[k] - rx),
                  4);
    }
  }
  return canvas.DrawPixmap(out, tx, ty);
}

Status MapEngine::RenderBaseMap(ICanvas& canvas, int64_t series_id,
                                const std::function<bool()>& interrupted,
                                int* frames_drawn) {
  if (frames_drawn != nullptr) *frames_drawn = 0;
  if (!proj_.Ready())
    return Status::Error(kInvalidArg,
                         "set surface size, center, and scale first");
  if (catalog_ == nullptr)
    return Status::Error(kInvalidArg, "no catalog attached");

  std::vector<CoverageRow> rows;
  Status s = catalog_->SelectByGeoRect(proj_.VmapBounds(), &rows, series_id);
  if (!s.ok()) return s;

  int drawn = 0;
  for (const CoverageRow& row : rows) {
    if (interrupted && interrupted())
      return Status::Error(kInterrupted, "render interrupted");
    s = CompositeRow(row, canvas);
    if (!s.ok()) return s;
    ++drawn;
  }
  if (frames_drawn != nullptr) *frames_drawn = drawn;
  return Status::Ok();
}

}  // namespace fv
