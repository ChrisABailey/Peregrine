// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fvkit/formats/elevation_stack.h — several elevation sources answered in
 * priority order, e.g. lidar DEMs over a DTED tree.
 */

#pragma once

#include <memory>
#include <vector>

#include "fvkit/formats/source.h"

namespace fv {

/// Asks each layer in order; the first finite answer wins. A void (NaN) in
/// one layer falls through to the next, and is the answer only when no later
/// layer has a value. Not thread-safe (nor are the layers).
class ElevationStack : public IElevationSource {
 public:
  /// `layers` highest priority first; null entries are ignored.
  explicit ElevationStack(std::vector<std::shared_ptr<IElevationSource>> layers);

  size_t LayerCount() const { return layers_.size(); }

  GeoRect Bounds() const override { return bounds_; }
  Status GetElevation(const GeoPoint& p, float* elevation_meters) override;
  /// The spacing of the first layer that has a value at `p`.
  bool PostSpacing(const GeoPoint& p, double* lat_deg, double* lon_deg) override;
  /// Each layer's grid fills only the posts the layers before it left empty.
  bool SampleGrid(const GeoRect& bounds, int width, int height, float* meters,
                  unsigned char* covered) override;

 private:
  std::vector<std::shared_ptr<IElevationSource>> layers_;
  GeoRect bounds_{};
};

}  // namespace fv
