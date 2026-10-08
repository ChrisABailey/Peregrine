// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_base_map.h"

#include "fv_map_enums.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/formats/registry.h"

namespace fv {
namespace desk {

BaseMapRenderer::BaseMapRenderer() = default;
BaseMapRenderer::~BaseMapRenderer() = default;

void BaseMapRenderer::SetCatalog(std::shared_ptr<Catalog> catalog) {
  engine_.reset();
  catalog_ = std::move(catalog);
  if (catalog_) engine_ = std::make_unique<MapEngine>(catalog_);
}

bool BaseMapRenderer::CanDraw(const std::string& format) {
  const FormatFactories* f = FindFormat(format);
  return f != nullptr && static_cast<bool>(f->make_raster_source);
}

Status BaseMapRenderer::Render(const view::Viewport& v, const view::LadderProduct& product,
                               ICanvas& canvas,
                               const std::function<bool()>& interrupted) {
  if (!engine_ || !CanDraw(product.format)) return Status::Ok();
  Status s = engine_->SetSurfaceDimensions(v.PixelWidth(), v.PixelHeight());
  if (s.ok()) s = engine_->SetProjectionType(v.Type());
  if (s.ok()) s = engine_->SetCenter(v.Center());
  // The engine's pitch is per pixel; the viewport's is per point.
  if (s.ok())
    s = engine_->SetPhysicalScale(v.ScaleDenom(), MAP_SCALE_DENOMINATOR,
                                  v.MmPerPoint() / v.DisplayScale());
  if (s.ok()) s = engine_->SetRotation(v.Rotation());
  if (s.ok()) s = engine_->RenderBaseMap(canvas, product.series_id, interrupted);
  return s;
}

}  // namespace desk
}  // namespace fv
