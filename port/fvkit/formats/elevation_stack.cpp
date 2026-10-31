// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// ElevationStack — see fvkit/formats/elevation_stack.h.

#include "fvkit/formats/elevation_stack.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fv {

ElevationStack::ElevationStack(std::vector<std::shared_ptr<IElevationSource>> layers) {
  for (auto& l : layers) {
    if (!l) continue;
    const GeoRect b = l->Bounds();
    if (layers_.empty()) {
      bounds_ = b;
    } else {
      bounds_.ll.lat = std::min(bounds_.ll.lat, b.ll.lat);
      bounds_.ll.lon = std::min(bounds_.ll.lon, b.ll.lon);
      bounds_.ur.lat = std::max(bounds_.ur.lat, b.ur.lat);
      bounds_.ur.lon = std::max(bounds_.ur.lon, b.ur.lon);
    }
    layers_.push_back(std::move(l));
  }
}

Status ElevationStack::GetElevation(const GeoPoint& p, float* elevation_meters) {
  if (elevation_meters == nullptr) return Status::Error(kInvalidArg, "elevation_meters is null");
  Status result = Status::Error(kOutOfCoverage, "no elevation layer covers the point");
  bool void_hit = false;
  for (auto& l : layers_) {
    float v = std::numeric_limits<float>::quiet_NaN();
    Status s = l->GetElevation(p, &v);
    if (s.ok() && !std::isnan(v)) {
      *elevation_meters = v;
      return s;
    }
    if (s.ok()) void_hit = true;
    else if (s.code != kOutOfCoverage) result = s;
  }
  if (void_hit) {
    *elevation_meters = std::numeric_limits<float>::quiet_NaN();
    return Status::Ok();
  }
  return result;
}

bool ElevationStack::PostSpacing(const GeoPoint& p, double* lat_deg, double* lon_deg) {
  for (auto& l : layers_) {
    float v = 0.0f;
    if (l->GetElevation(p, &v).ok() && !std::isnan(v) && l->PostSpacing(p, lat_deg, lon_deg))
      return true;
  }
  for (auto& l : layers_)
    if (l->PostSpacing(p, lat_deg, lon_deg)) return true;
  return false;
}

bool ElevationStack::SampleGrid(const GeoRect& bounds, int width, int height, float* meters,
                                unsigned char* covered) {
  if (meters == nullptr || width < 2 || height < 2) return false;
  const size_t n = static_cast<size_t>(width) * height;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::fill(meters, meters + n, nan);
  if (covered != nullptr) std::fill(covered, covered + n, 0);
  const double dlat = (bounds.ur.lat - bounds.ll.lat) / (height - 1);
  const double dlon = (bounds.ur.lon - bounds.ll.lon) / (width - 1);

  std::vector<float> layer(n);
  std::vector<unsigned char> layer_cov(n);
  for (auto& l : layers_) {
    if (!l->Bounds().Intersects(bounds)) continue;
    if (!l->SampleGrid(bounds, width, height, layer.data(), layer_cov.data())) {
      // Per post, and only where nothing above has answered yet.
      for (size_t i = 0; i < n; ++i) {
        layer[i] = nan;
        layer_cov[i] = 0;
        if (!std::isnan(meters[i])) continue;
        const int r = static_cast<int>(i / width), c = static_cast<int>(i % width);
        GeoPoint p{bounds.ll.lat + r * dlat, bounds.ll.lon + c * dlon};
        p.Normalize();
        float v = nan;
        if (l->GetElevation(p, &v).ok()) {
          layer[i] = v;
          layer_cov[i] = 1;
        }
      }
    }
    bool all = true;
    for (size_t i = 0; i < n; ++i) {
      if (covered != nullptr && layer_cov[i]) covered[i] = 1;
      if (std::isnan(meters[i]) && !std::isnan(layer[i])) meters[i] = layer[i];
      if (std::isnan(meters[i])) all = false;
    }
    if (all) break;
  }
  return true;
}

}  // namespace fv
