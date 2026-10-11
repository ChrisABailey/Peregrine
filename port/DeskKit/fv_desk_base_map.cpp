// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_base_map.h"

#include <filesystem>
#include <system_error>

#include "fv_map_enums.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"

namespace fv {
namespace desk {

const std::string& HostLabelFont() {
  static const std::string path = [] {
    const char* candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (const char* f : candidates) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(f, ec)) return std::string(f);
    }
    return std::string();
  }();
  return path;
}

BaseMapRenderer::BaseMapRenderer() = default;
BaseMapRenderer::~BaseMapRenderer() = default;

void BaseMapRenderer::SetCatalog(std::shared_ptr<Catalog> catalog) {
  engine_.reset();
  catalog_ = std::move(catalog);
  if (catalog_) engine_ = std::make_unique<MapEngine>(catalog_);
  vector_.Reset(catalog_);
}

bool BaseMapRenderer::CanDraw(const std::string& format) {
  if (VectorBaseMap::CanDraw(format)) return true;
  const FormatFactories* f = FindFormat(format);
  return f != nullptr && static_cast<bool>(f->make_raster_source);
}

Status BaseMapRenderer::Render(const view::Viewport& v, const view::LadderProduct& product,
                               ICanvas& canvas,
                               const std::function<bool()>& interrupted,
                               std::vector<SkippedFrame>* skipped) {
  last_vector_ = false;
  if (!engine_ || !CanDraw(product.format)) return Status::Ok();
  if (VectorBaseMap::CanDraw(product.format)) {
    last_vector_ = true;
    return vector_.Render(v, product, canvas);
  }
  Status s = engine_->SetSurfaceDimensions(v.PixelWidth(), v.PixelHeight());
  if (s.ok()) s = engine_->SetProjectionType(v.Type());
  if (s.ok()) s = engine_->SetCenter(v.Center());
  // The engine's pitch is per pixel; the viewport's is per point.
  if (s.ok())
    s = engine_->SetPhysicalScale(v.ScaleDenom(), MAP_SCALE_DENOMINATOR,
                                  v.MmPerPoint() / v.DisplayScale());
  if (s.ok()) s = engine_->SetRotation(v.Rotation());
  if (s.ok()) s = engine_->RenderBaseMap(canvas, product.series_id, interrupted, nullptr, skipped);
  return s;
}

}  // namespace desk
}  // namespace fv
