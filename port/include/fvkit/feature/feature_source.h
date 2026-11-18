// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/feature_source.h — sources of feature layers that are not
/// files: a process-wide registry the feature overlay consults before it
/// treats a spec as a path (port/web-features-plan.md WF-D3).
///
/// A package registers a source (`NativeRegistrar::AddFeatureSource`); the
/// web feature package registers one for http(s) URLs. Specs follow
/// `fvkit/app/source_spec.h`: `<source>|layername=<layer>`.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"

namespace fv {

/// The overlay type that opens every feature-source spec (`FeatureOverlay::kTypeId`).
constexpr char kFeatureSourceOverlayType[] = "fv.features";

/// What opening a spec through a source gives the overlay.
struct FeatureSourceResult {
  std::shared_ptr<IFeatureLayer> layer;  ///< read-only; the overlay never edits it
  /// The source's own style (a `WriteFeatureStyleJson` document) for a layer
  /// opened afresh; empty for the default style.
  std::string style_json;
  /// One sentence each on what the source could not carry over (style import).
  std::vector<std::string> notes;
};

/// One layer a source offers, as the Add Web Layer dialog lists it.
struct FeatureSourceLayer {
  std::string spec;   ///< opens this layer (`<source>|layername=<id>`)
  std::string id;     ///< the source's own layer id
  std::string title;  ///< display name; `id` when the source gives none
  std::string description;
  GeometryType geometry_type = GeometryType::kGeometry;  ///< kGeometry when not known
};

struct FeatureSourceDesc {
  std::string name;                                     ///< unique; "peregrine.web"
  std::function<bool(const std::string& spec)> matches;  ///< required; cheap, no I/O
  /// Required. May block on I/O; called on the thread opening the overlay.
  std::function<Status(const std::string& spec, FeatureSourceResult* out)> open;
  /// Optional. The layers of the source `spec` names (its layer part
  /// ignored). May block on I/O; `cancel`, when set, stops it with kInterrupted.
  std::function<Status(const std::string& spec, std::vector<FeatureSourceLayer>* out,
                       const std::atomic<bool>* cancel)>
      list;
};

/// Adds a source. kInvalidArg for an empty name, a missing function or a
/// name already registered.
Status RegisterFeatureSource(FeatureSourceDesc desc);
/// Removes the source `name`; nothing when there is none.
void UnregisterFeatureSource(const std::string& name);
/// True when a registered source claims `spec`.
bool IsFeatureSourceSpec(const std::string& spec);
/// Opens `spec` through the first source that claims it, in registration
/// order. kNotFound when none does.
Status OpenFeatureSource(const std::string& spec, FeatureSourceResult* out);
/// True when some registered source can list its layers.
bool AnyFeatureSourceLists();
/// Lists the layers of `spec`'s source through the source claiming it.
/// kNotFound when none does, kUnsupported when it cannot list.
Status ListFeatureSourceLayers(const std::string& spec, std::vector<FeatureSourceLayer>* out,
                               const std::atomic<bool>* cancel = nullptr);

/// A read-only view of a layer as the editable layer a feature overlay holds:
/// reads go to `inner`, every edit is refused with kUnsupported and `reason`.
class ReadOnlyFeatureLayer : public IEditableFeatureLayer {
 public:
  ReadOnlyFeatureLayer(std::shared_ptr<IFeatureLayer> inner, std::string reason);

  const std::shared_ptr<IFeatureLayer>& inner() const { return inner_; }

  std::string Name() const override { return inner_->Name(); }
  const FeatureSchema& Schema() const override { return inner_->Schema(); }
  size_t Count() const override { return inner_->Count(); }
  Status Extent(GeoRect* out) const override { return inner_->Extent(out); }
  Status All(std::vector<Feature>* out) const override { return inner_->All(out); }
  Status Query(const GeoRect& box, std::vector<Feature>* out) const override {
    return inner_->Query(box, out);
  }
  Status QueryFields(const GeoRect& box, const std::vector<bool>& fields,
                     std::vector<Feature>* out) const override {
    return inner_->QueryFields(box, fields, out);
  }
  Status Get(int64_t id, Feature* out) const override { return inner_->Get(id, out); }
  bool Revision(uint64_t* out) const override { return inner_->Revision(out); }

  Status Insert(Feature*) override { return Refuse(); }
  Status Update(const Feature&) override { return Refuse(); }
  Status Delete(int64_t) override { return Refuse(); }
  Status InsertWithId(const Feature&) override { return Refuse(); }
  Status ChangeSchema(const SchemaChange&) override { return Refuse(); }

 private:
  Status Refuse() const { return Status::Error(kUnsupported, reason_); }

  std::shared_ptr<IFeatureLayer> inner_;
  std::string reason_;
};

}  // namespace fv
