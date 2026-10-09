// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_base_map.h — draws the base map of one ladder product at a view.
///
/// Holds the `MapEngine` (and its frame cache) for one catalog, so a render
/// thread keeps one of these for its lifetime. Raster formats draw through
/// the engine; DNC, ENC and OSM through `VectorBaseMap`.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fv_view_ladder.h"
#include "fv_desk_vector_map.h"
#include "fv_view_viewport.h"
#include "fvkit/geo.h"

namespace fv {
class Catalog;
class ICanvas;
class MapEngine;
struct SkippedFrame;

namespace desk {

class BaseMapRenderer {
 public:
  BaseMapRenderer();
  ~BaseMapRenderer();
  BaseMapRenderer(const BaseMapRenderer&) = delete;
  BaseMapRenderer& operator=(const BaseMapRenderer&) = delete;

  /// Replaces the catalog; the engine, its cache and the vector sources are
  /// rebuilt, and the vector products re-read `VectorMapConfig`. Null clears.
  void SetCatalog(std::shared_ptr<Catalog> catalog);
  const std::shared_ptr<Catalog>& catalog() const { return catalog_; }

  /// Whether `format` is one this renderer can draw.
  static bool CanDraw(const std::string& format);

  /// Draws `product` at `view` onto `canvas`, which must be the view's pixel
  /// size. A product this renderer cannot draw, or no catalog, is a no-op.
  /// `interrupted` is polled between raster frames; true stops with
  /// kInterrupted. A vector product draws to the end once started.
  /// Frames whose files cannot be read are left out and listed in `skipped`.
  Status Render(const view::Viewport& view, const view::LadderProduct& product,
                ICanvas& canvas, const std::function<bool()>& interrupted = {},
                std::vector<SkippedFrame>* skipped = nullptr);

 private:
  std::shared_ptr<Catalog> catalog_;
  std::unique_ptr<MapEngine> engine_;
  VectorBaseMap vector_;
};

}  // namespace desk
}  // namespace fv
