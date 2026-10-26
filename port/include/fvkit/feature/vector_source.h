// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/vector_source.h — a feature layer as an IVectorSource, so a
/// MapLibre style renders it through VectorRenderer like any other vector
/// product.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"
#include "fvkit/vector/vector.h"

namespace fv {

/// The one source-layer a feature layer's features carry.
constexpr const char kFeatureSourceLayer[] = "features";

/// Appends `f` as vector features: one per point, line or polygon. Multi*
/// and collections split into their members, all carrying `f`'s id. Each
/// non-null field becomes an attribute holding `FeatureCategoryKey` of its
/// value; blobs are left out. With `fields`, only the fields it marks true
/// become attributes.
void FeatureToVector(const Feature& f, const FeatureSchema& schema,
                     std::vector<VectorFeature>* out, const std::vector<bool>* fields = nullptr);

class FeatureVectorSource : public IVectorSource {
 public:
  explicit FeatureVectorSource(std::shared_ptr<const IFeatureLayer> layer);

  /// No-op: the source reads the layer it was built with.
  Status Open(const std::string& path) override;
  bool IsOpen() const override { return layer_ != nullptr; }
  GeoRect Bounds() const override;
  std::vector<std::string> Layers() const override { return {kFeatureSourceLayer}; }
  /// Features whose envelope meets `q.area` and that pass the filter;
  /// `q.max_features` is not applied.
  Status Query(const VectorQuery& q, std::vector<VectorFeature>* out) override;

  /// Leaves out features `keep` rejects; an empty function keeps all.
  void SetFilter(std::function<bool(const Feature&)> keep) { keep_ = std::move(keep); }
  /// Only the fields marked true, by schema index, are read and become
  /// attributes; empty gives every field. The filter sees the others as null.
  void SetAttributeFields(std::vector<bool> fields) { fields_ = std::move(fields); }

  /// Distinct features the last Query returned.
  size_t features_returned() const { return features_returned_; }

 private:
  std::shared_ptr<const IFeatureLayer> layer_;
  std::function<bool(const Feature&)> keep_;
  std::vector<bool> fields_;
  size_t features_returned_ = 0;
};

}  // namespace fv
