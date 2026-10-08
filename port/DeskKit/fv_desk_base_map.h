// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_base_map.h — draws the base map of one ladder product at a view.
///
/// Holds the `MapEngine` (and its frame cache) for one catalog, so a render
/// thread keeps one of these for its lifetime. Formats with a raster source
/// are drawn; vector formats (OSM, ENC, DNC) are not yet, and leave the
/// canvas as it was.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "fv_view_ladder.h"
#include "fv_view_viewport.h"
#include "fvkit/geo.h"

namespace fv {
class Catalog;
class ICanvas;
class MapEngine;

namespace desk {

class BaseMapRenderer {
 public:
  BaseMapRenderer();
  ~BaseMapRenderer();
  BaseMapRenderer(const BaseMapRenderer&) = delete;
  BaseMapRenderer& operator=(const BaseMapRenderer&) = delete;

  /// Replaces the catalog; the engine and its cache are rebuilt. Null clears.
  void SetCatalog(std::shared_ptr<Catalog> catalog);
  const std::shared_ptr<Catalog>& catalog() const { return catalog_; }

  /// Whether `format` is one this renderer can draw.
  static bool CanDraw(const std::string& format);

  /// Draws `product` at `view` onto `canvas`, which must be the view's pixel
  /// size. A product this renderer cannot draw, or no catalog, is a no-op.
  /// `interrupted` is polled between frames; true stops with kInterrupted.
  Status Render(const view::Viewport& view, const view::LadderProduct& product,
                ICanvas& canvas, const std::function<bool()>& interrupted = {});

 private:
  std::shared_ptr<Catalog> catalog_;
  std::unique_ptr<MapEngine> engine_;
};

}  // namespace desk
}  // namespace fv
