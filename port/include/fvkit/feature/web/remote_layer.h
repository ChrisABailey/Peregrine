// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/remote_layer.h — a read-only feature layer backed by a
/// web feature service and a local GeoPackage cache (port/web-features-plan.md
/// WF1; decisions WF-D5 to WF-D10).
///
/// Reads answer from the cache at once. `Query` also schedules the tiles of
/// its box that the cache lacks or holds stale; a worker thread fetches them,
/// stores them, moves `Revision()` and calls the change callback. Layers of one
/// service share one cache file (`WebCachePath`) and its lock.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "fvkit/feature/layer.h"
#include "fvkit/feature/web/service.h"

namespace fv {
namespace web {

struct RemoteLayerOptions {
  /// Tiles older than this are fetched again when next queried; 0 = never (WF-D9).
  int refresh_minutes = 0;
  /// Fetching stops once the cache holds this many features (WF-D10).
  size_t max_features = 200000;
  /// A tile needing more pages than this is split into its children (WF-D10).
  int max_pages_per_tile = 20;
  int min_zoom = 4;
  int max_zoom = 14;
  /// Seconds before a tile whose fetch failed is tried again.
  int retry_seconds = 60;
  /// Queued tiles beyond this are dropped, oldest request first.
  size_t max_queued_tiles = 64;
  /// Seconds since the epoch; tests replace it.
  std::function<int64_t()> now;
};

/// What the layer is doing, for the status line.
struct RemoteLayerProgress {
  size_t tiles_queued = 0;
  size_t tiles_loading = 0;
  size_t features_cached = 0;
  bool capped = false;     ///< `max_features` reached; fetching stopped
  std::string last_error;  ///< the latest failed fetch; empty once one succeeds
};

/// `<cache_dir>/web-features/<hash of source_url>.gpkg`.
std::string WebCachePath(const std::string& cache_dir, const std::string& source_url);

class RemoteFeatureLayer : public IFeatureLayer {
 public:
  /// Opens `info.id` of `service` over the cache at `cache_path`, creating the
  /// file or its table as needed. A cached table whose fields differ from
  /// `info.schema` is left in the file and a new one used. Starts the worker.
  static Status Open(std::shared_ptr<IFeatureService> service, const RemoteLayerInfo& info,
                     const std::string& cache_path, RemoteLayerOptions options,
                     std::shared_ptr<RemoteFeatureLayer>* out);
  /// Stops the worker, cancelling a fetch in progress.
  ~RemoteFeatureLayer() override;

  std::string Name() const override;
  const FeatureSchema& Schema() const override;
  size_t Count() const override;
  /// The service's extent for the layer; kNotFound when it gives none.
  Status Extent(GeoRect* out) const override;
  /// Every cached feature; fetches nothing.
  Status All(std::vector<Feature>* out) const override;
  /// The cached features in `box`; schedules what the cache lacks there.
  Status Query(const GeoRect& box, std::vector<Feature>* out) const override;
  Status QueryFields(const GeoRect& box, const std::vector<bool>& fields,
                     std::vector<Feature>* out) const override;
  Status Get(int64_t id, Feature* out) const override;
  bool Revision(uint64_t* out) const override;

  const RemoteLayerInfo& info() const;
  RemoteLayerProgress Progress() const;
  /// Called on the worker thread after features arrive or the progress changes.
  void SetOnChange(std::function<void()> callback);
  /// Forgets every fetched tile, so the next query fetches afresh.
  Status Refresh();
  /// Blocks until nothing is queued or loading, or `timeout_s` passes; true when idle.
  bool WaitIdle(double timeout_s) const;

  struct Impl;

 private:
  explicit RemoteFeatureLayer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace web
}  // namespace fv
