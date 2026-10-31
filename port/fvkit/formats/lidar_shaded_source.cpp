// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// LidarShadedRasterSource implementation — see fvkit/formats/lidar_shaded.h.

#include "fvkit/formats/lidar_shaded.h"

#include <cmath>
#include <limits>
#include <vector>

#include "fv_dted_shaded_renderer.h"  // port/DtedShadedRenderer
#include "fvkit/formats/dted_shaded.h"
#include "fvkit/formats/geotiff_dem.h"

namespace fv {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6378137.0;  // DTED_READER_EARTH_RADIUS_IN_METERS

}  // namespace

struct LidarShadedRasterSource::Impl {
  std::unique_ptr<GeoTiffDem> dem;
  DtedShadedRenderer renderer;
  GeoRect bounds{};
  int width = 0, height = 0;
  double dlat = 0, dlon = 0;  // degrees per pixel
};

LidarShadedRasterSource::LidarShadedRasterSource() : impl_(new Impl) {}
LidarShadedRasterSource::~LidarShadedRasterSource() = default;

Status LidarShadedRasterSource::Open(const std::string& path) {
  if (impl_->dem) return Status::Error(kInvalidArg, "already open");
  std::unique_ptr<GeoTiffDem> dem;
  Status s = GeoTiffDem::Open(path, &dem);
  if (!s.ok()) return s;
  const GeoRect b = dem->Bounds();
  const GeoPoint mid{(b.ll.lat + b.ur.lat) / 2, (b.ll.lon + b.ur.lon) / 2};
  double post_lat = 0, post_lon = 0;
  if (!dem->PostSpacing(mid, &post_lat, &post_lon) || !(post_lat > 0) || !(post_lon > 0))
    return Status::Error(kUnsupported, "DEM has no post spacing: " + path);
  const int w = static_cast<int>(std::lround((b.ur.lon - b.ll.lon) / post_lon));
  const int h = static_cast<int>(std::lround((b.ur.lat - b.ll.lat) / post_lat));
  if (w < 1 || h < 1) return Status::Error(kUnsupported, "DEM too small to render: " + path);

  impl_->bounds = b;
  impl_->width = w;
  impl_->height = h;
  impl_->dlon = (b.ur.lon - b.ll.lon) / w;
  impl_->dlat = (b.ur.lat - b.ll.lat) / h;
  const std::vector<int> bands = DtedShadedElevationBands();
  if (!bands.empty()) impl_->renderer.SetElevationBands(bands);
  impl_->dem = std::move(dem);
  return Status::Ok();
}

GeoRect LidarShadedRasterSource::Bounds() const {
  return impl_->dem ? impl_->bounds : GeoRect{};
}

Status LidarShadedRasterSource::Info(ImageInfo* info) const {
  if (info == nullptr) return Status::Error(kInvalidArg, "info is null");
  if (!impl_->dem) return Status::Error(kInvalidArg, "not open");
  info->size = PixelSize{impl_->width, impl_->height};
  info->bounds = impl_->bounds;
  return Status::Ok();
}

Status LidarShadedRasterSource::ReadBlock(const PixelRect& r, PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->dem) return Status::Error(kInvalidArg, "not open");
  if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 || r.x + r.width > impl_->width ||
      r.y + r.height > impl_->height)
    return Status::Error(kInvalidArg, "block rect outside image");

  // One extra post row south and column east: each pixel is lit from the
  // posts below and below-right of it.
  const int pw = r.width + 1, ph = r.height + 1;
  std::vector<float> posts(static_cast<size_t>(pw) * ph);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (int y = 0; y < ph; ++y) {
    for (int x = 0; x < pw; ++x) {
      GeoPoint g;
      PixelToGeo(r.x + x, r.y + y, &g);
      float m = nan;
      if (!impl_->dem->GetElevation(g, &m).ok()) m = nan;
      posts[static_cast<size_t>(y) * pw + x] = m;
    }
  }

  GeoPoint mid;
  PixelToGeo(r.x + r.width / 2.0, r.y + r.height / 2.0, &mid);
  const double m_lat = impl_->dlat * kEarthRadiusM * kPi / 180.0;
  const double m_lon = impl_->dlon * kEarthRadiusM * std::cos(mid.lat * kPi / 180.0) * kPi / 180.0;
  return impl_->renderer.ShadeGrid(posts.data(), r.width, r.height, m_lat, m_lon, mid, out);
}

Status LidarShadedRasterSource::PixelToGeo(double px, double py, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!impl_->dem) return Status::Error(kInvalidArg, "not open");
  p->lon = impl_->bounds.ll.lon + (px + 0.5) * impl_->dlon;
  p->lat = impl_->bounds.ur.lat - (py + 0.5) * impl_->dlat;
  return Status::Ok();
}

Status LidarShadedRasterSource::GeoToPixel(const GeoPoint& p, double* px, double* py) const {
  if (px == nullptr || py == nullptr) return Status::Error(kInvalidArg, "px/py is null");
  if (!impl_->dem) return Status::Error(kInvalidArg, "not open");
  *px = (p.lon - impl_->bounds.ll.lon) / impl_->dlon - 0.5;
  *py = (impl_->bounds.ur.lat - p.lat) / impl_->dlat - 0.5;
  return Status::Ok();
}

}  // namespace fv
