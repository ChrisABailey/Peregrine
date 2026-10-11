// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/points_layer.h — a `.fvpoints` document as a feature layer
/// (port/feature-overlay-plan.md §6, FO-D6).
#pragma once

#include <memory>

#include "fvkit/feature/layer.h"

namespace fv {
class PointOverlay;

/// The point set's rows under a fixed schema (name, category, elevation_ft,
/// remarks, phone, url), feature ids being point ids. Edits go through the
/// overlay's `PointEditSession`, so they dirty the document and are undoable;
/// what the schema leaves out (shape, size, colour, symbol) is kept.
class PointsFeatureLayer : public IEditableFeatureLayer {
 public:
  /// Field indexes in `PointsSchema()`.
  enum Field : int { kName = 0, kCategory, kElevation, kRemarks, kPhone, kUrl };

  explicit PointsFeatureLayer(std::shared_ptr<PointOverlay> points);

  static const FeatureSchema& PointsSchema();
  const std::shared_ptr<PointOverlay>& points() const { return points_; }

  std::string Name() const override;
  const FeatureSchema& Schema() const override { return PointsSchema(); }
  size_t Count() const override;
  Status Extent(GeoRect* out) const override;
  Status All(std::vector<Feature>* out) const override;
  Status Query(const GeoRect& box, std::vector<Feature>* out) const override;
  Status Get(int64_t id, Feature* out) const override;
  /// The geometry must be a non-empty point; the new point is selected.
  Status Insert(Feature* feature) override;
  /// A non-empty point geometry moves the point; a null one leaves it.
  Status Update(const Feature& feature) override;
  Status Delete(int64_t id) override;

 private:
  std::shared_ptr<PointOverlay> points_;
};

}  // namespace fv
