// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// MapEngine — see fvkit/engine.h.

#include "fvkit/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "fv_map_enums.h"  // MapScaleUnitsEnum
#include "fvkit/formats/registry.h"

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
  // frame lon span: for a crossing frame the east edge unwraps to
  // ll.lon + width (e.g. ll=175, ur=-178 -> width 7, east edge 182)
  double f_west = UnwrapLonNear(row.bounds.ll.lon, ref);
  double f_width = row.bounds.CrossesAntimeridian()
                       ? (row.bounds.ur.lon + 360.0) - row.bounds.ll.lon
                       : row.bounds.ur.lon - row.bounds.ll.lon;
  double f_east = f_west + f_width;

  double o_top = std::min(view.ur.lat, row.bounds.ur.lat);
  double o_bot = std::max(view.ll.lat, row.bounds.ll.lat);
  double o_west = std::max(v_west, f_west);
  double o_east = std::min(v_east, f_east);
  if (o_top <= o_bot || o_east <= o_west) return Status::Ok();  // no overlap

  // target region on the surface (rounded overlap corners), clipped
  double sx0, sy0, sx1, sy1;
  s = proj_.GeoToSurface({o_top, NormalizeLon(o_west)}, &sx0, &sy0);
  if (!s.ok()) return s;
  s = proj_.GeoToSurface({o_bot, NormalizeLon(o_east)}, &sx1, &sy1);
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
  s = src->GeoToPixel(nw, &fx0, &fy0);
  if (!s.ok()) return s;
  s = src->GeoToPixel(se, &fx1, &fy1);
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
