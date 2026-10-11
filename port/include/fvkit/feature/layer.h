// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/layer.h — the feature layer interfaces and an in-memory layer
/// (port/feature-overlay-plan.md §2).
///
/// GeoPackage, GeoJSON and the `.fvpoints` adapter implement these; overlays,
/// forms and the attribute table see nothing else. Held as shared_ptr
/// (contracts D1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fvkit/feature/schema.h"
#include "fvkit/geo.h"

namespace fv {

class IFeatureLayer {
 public:
  virtual ~IFeatureLayer() = default;
  IFeatureLayer(const IFeatureLayer&) = delete;
  IFeatureLayer& operator=(const IFeatureLayer&) = delete;

  virtual std::string Name() const = 0;
  virtual const FeatureSchema& Schema() const = 0;
  virtual size_t Count() const = 0;
  /// The box around every feature's geometry. kNotFound when none has one.
  virtual Status Extent(GeoRect* out) const = 0;
  /// Every feature, in id order.
  virtual Status All(std::vector<Feature>* out) const = 0;
  /// Features whose envelope intersects `box` (which may cross ±180), in id
  /// order. Features with no geometry are never returned.
  virtual Status Query(const GeoRect& box, std::vector<Feature>* out) const = 0;
  /// kNotFound for an id the layer does not hold.
  virtual Status Get(int64_t id, Feature* out) const = 0;

 protected:
  IFeatureLayer() = default;
};

class IEditableFeatureLayer : public IFeatureLayer {
 public:
  /// Stores `feature` under a new id (its own `id` is ignored) and writes the
  /// id back. Values are checked against the schema; a missing trailing value
  /// is null, an extra one is kInvalidArg.
  virtual Status Insert(Feature* feature) = 0;
  /// Replaces the feature with `feature.id`. kNotFound when there is none.
  virtual Status Update(const Feature& feature) = 0;
  virtual Status Delete(int64_t id) = 0;
  /// Stores `feature` under its own id, which must be positive and unused, so
  /// an undone delete comes back as the same feature. kUnsupported by default.
  virtual Status InsertWithId(const Feature& feature) {
    (void)feature;
    return Status::Error(kUnsupported, "this layer cannot insert with a given id");
  }
  /// Replaces the schema, carrying every feature's values across as
  /// `RemapFeature` does; ids and geometry are kept. kInvalidArg from
  /// `ValidateSchemaChange`, or when a required field would be left empty;
  /// nothing changes on failure. kUnsupported by default.
  virtual Status ChangeSchema(const SchemaChange& change) {
    (void)change;
    return Status::Error(kUnsupported, "this layer's fields cannot be changed");
  }
};

/// A layer held entirely in memory, ids in insertion order from 1.
class MemoryFeatureLayer : public IEditableFeatureLayer {
 public:
  MemoryFeatureLayer(std::string name, FeatureSchema schema);

  std::string Name() const override { return name_; }
  const FeatureSchema& Schema() const override { return schema_; }
  size_t Count() const override { return features_.size(); }
  Status Extent(GeoRect* out) const override;
  Status All(std::vector<Feature>* out) const override;
  Status Query(const GeoRect& box, std::vector<Feature>* out) const override;
  Status Get(int64_t id, Feature* out) const override;
  Status Insert(Feature* feature) override;
  Status Update(const Feature& feature) override;
  Status Delete(int64_t id) override;

  /// Also used by a reader keeping the ids a file gives.
  Status InsertWithId(const Feature& feature) override;
  Status ChangeSchema(const SchemaChange& change) override;

 private:
  /// Pads `values` to the schema with nulls and checks each one.
  Status Conform(Feature* feature) const;

  std::string name_;
  FeatureSchema schema_;
  std::map<int64_t, Feature> features_;
  int64_t next_id_ = 1;
};

}  // namespace fv
