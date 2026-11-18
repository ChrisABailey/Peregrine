// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The feature-source registry and the read-only layer view.

#include "fvkit/feature/feature_source.h"

#include <algorithm>
#include <mutex>

namespace fv {
namespace {

struct Registry {
  std::mutex mu;
  std::vector<FeatureSourceDesc> sources;
};

Registry& Sources() {
  static Registry* r = new Registry;  // never destroyed: package code may run during static teardown
  return *r;
}

}  // namespace

Status RegisterFeatureSource(FeatureSourceDesc desc) {
  if (desc.name.empty() || !desc.matches || !desc.open)
    return Status::Error(kInvalidArg, "a feature source needs a name, a matcher and an opener");
  Registry& r = Sources();
  std::lock_guard<std::mutex> lock(r.mu);
  for (const FeatureSourceDesc& d : r.sources)
    if (d.name == desc.name) return Status::Error(kInvalidArg, "feature source '" + desc.name + "' is already registered");
  r.sources.push_back(std::move(desc));
  return Status::Ok();
}

void UnregisterFeatureSource(const std::string& name) {
  Registry& r = Sources();
  std::lock_guard<std::mutex> lock(r.mu);
  r.sources.erase(std::remove_if(r.sources.begin(), r.sources.end(),
                                 [&name](const FeatureSourceDesc& d) { return d.name == name; }),
                  r.sources.end());
}

namespace {

/// A copy of the source claiming `spec`, so it is called outside the lock.
bool Claimant(const std::string& spec, FeatureSourceDesc* out) {
  Registry& r = Sources();
  std::lock_guard<std::mutex> lock(r.mu);
  for (const FeatureSourceDesc& d : r.sources)
    if (d.matches(spec)) {
      *out = d;
      return true;
    }
  return false;
}

}  // namespace

bool IsFeatureSourceSpec(const std::string& spec) {
  FeatureSourceDesc d;
  return Claimant(spec, &d);
}

Status OpenFeatureSource(const std::string& spec, FeatureSourceResult* out) {
  FeatureSourceDesc d;
  if (!Claimant(spec, &d)) return Status::Error(kNotFound, "no feature source opens '" + spec + "'");
  *out = FeatureSourceResult();
  Status s = d.open(spec, out);
  if (s.ok() && !out->layer) return Status::Error(kInternal, d.name + " opened '" + spec + "' without a layer");
  return s;
}

bool AnyFeatureSourceLists() {
  Registry& r = Sources();
  std::lock_guard<std::mutex> lock(r.mu);
  return std::any_of(r.sources.begin(), r.sources.end(), [](const FeatureSourceDesc& d) { return bool(d.list); });
}

Status ListFeatureSourceLayers(const std::string& spec, std::vector<FeatureSourceLayer>* out,
                               const std::atomic<bool>* cancel) {
  out->clear();
  FeatureSourceDesc d;
  if (!Claimant(spec, &d)) return Status::Error(kNotFound, "no feature source opens '" + spec + "'");
  if (!d.list) return Status::Error(kUnsupported, d.name + " cannot list its layers");
  return d.list(spec, out, cancel);
}

ReadOnlyFeatureLayer::ReadOnlyFeatureLayer(std::shared_ptr<IFeatureLayer> inner, std::string reason)
    : inner_(std::move(inner)), reason_(std::move(reason)) {}

}  // namespace fv
