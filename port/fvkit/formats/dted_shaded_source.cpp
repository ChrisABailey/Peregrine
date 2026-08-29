// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DtedShadedRasterSource implementation — see fvkit/formats/dted_shaded.h.

#include "fvkit/formats/dted_shaded.h"

#include <cstring>

#include "fv_dted_shaded_renderer.h"  // port/DtedShadedRenderer

namespace fv {

struct DtedShadedRasterSource::Impl {
  DtedShadedRenderer renderer;
  bool is_open = false;
  bool rendered = false;
  PixelBuffer cache;  // whole cell, top-down RGBA8, lazily filled

  Status Decode() {
    if (rendered) return Status::Ok();
    Status s = renderer.Render(&cache);
    if (!s.ok()) return s;
    rendered = true;
    return Status::Ok();
  }
};

DtedShadedRasterSource::DtedShadedRasterSource() : impl_(new Impl) {}
DtedShadedRasterSource::~DtedShadedRasterSource() = default;

Status DtedShadedRasterSource::Open(const std::string& path) {
  if (impl_->is_open) return Status::Error(kInvalidArg, "already open");
  Status s = impl_->renderer.Open(path);
  if (!s.ok()) return s;
  impl_->is_open = true;
  return Status::Ok();
}

GeoRect DtedShadedRasterSource::Bounds() const {
  return impl_->is_open ? impl_->renderer.ImageBounds() : GeoRect{};
}

Status DtedShadedRasterSource::Info(ImageInfo* info) const {
  if (info == nullptr) return Status::Error(kInvalidArg, "info is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  info->size = PixelSize{impl_->renderer.RenderedWidth(),
                         impl_->renderer.RenderedHeight()};
  info->bounds = impl_->renderer.ImageBounds();
  return Status::Ok();
}

Status DtedShadedRasterSource::ReadBlock(const PixelRect& r, PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  const int w = impl_->renderer.RenderedWidth();
  const int h = impl_->renderer.RenderedHeight();
  if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 ||
      r.x + r.width > w || r.y + r.height > h)
    return Status::Error(kInvalidArg, "block rect outside image");

  Status s = impl_->Decode();
  if (!s.ok()) return s;

  *out = PixelBuffer(r.width, r.height);
  for (int y = 0; y < r.height; ++y) {
    const unsigned char* src = impl_->cache.Row(r.y + y) + (size_t)r.x * 4;
    std::memcpy(out->Row(y), src, (size_t)r.width * 4);
  }
  return Status::Ok();
}

Status DtedShadedRasterSource::PixelToGeo(double px, double py,
                                          GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  impl_->renderer.PixelToGeo(px, py, p);
  return Status::Ok();
}

Status DtedShadedRasterSource::GeoToPixel(const GeoPoint& p, double* px,
                                          double* py) const {
  if (px == nullptr || py == nullptr)
    return Status::Error(kInvalidArg, "px/py is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  impl_->renderer.GeoToPixel(p, px, py);
  return Status::Ok();
}

DtedShadedRenderer* DtedShadedRasterSource::RendererForConfig() {
  return impl_->is_open ? &impl_->renderer : nullptr;
}

}  // namespace fv
