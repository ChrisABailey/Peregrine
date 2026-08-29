// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// CadrgRasterSource + CadrgFrameCache implementation — see fvkit/formats/
// cadrg.h. C++14 TU: includes the decoder (imgdisp.h, CString-based) and
// fv::CadrgFrame. Legacy types stay inside Impl.

#include "fvkit/formats/cadrg.h"

#include <algorithm>
#include <cstring>

#include "fv_compat.h"       // BOOL/FALSE etc. for the decoder headers
#include "fv_cstring.h"      // CString (RPFRenderer signatures)
#include "fv_cadrg_frame.h"  // fv::CadrgFrame (port/CadrgMapServer)
#include "imgdisp.h"         // RPFRenderer (port/CadrgDecoder)

namespace fv {

namespace {
constexpr int kFrameSize = 1536;  // CADRG_FRAME_WIDTH_IN_PIX
}

struct CadrgRasterSource::Impl {
  std::string path;
  CadrgFrameProperties props;
  bool is_open = false;
  bool decoded = false;
  std::vector<unsigned char> rgba;  // kFrameSize^2 * 4, lazily filled

  // Decode the whole frame once (the decoder always expands all 1536^2).
  Status Decode() {
    if (decoded) return Status::Ok();
    std::vector<unsigned char> rgb((size_t)kFrameSize * kFrameSize * 3);
    RPFRenderer renderer;
    int rc = renderer.get_rgb_image(path.c_str(), FALSE /*CADRG, not CIB*/, 0,
                                    0, kFrameSize, kFrameSize, rgb.data());
    if (rc != 0)
      return Status::Error(kIoError,
                           "CADRG decode failed (" + std::to_string(rc) +
                               "): " + path);
    rgba.resize((size_t)kFrameSize * kFrameSize * 4);
    for (size_t i = 0, n = (size_t)kFrameSize * kFrameSize; i < n; ++i) {
      rgba[4 * i + 0] = rgb[3 * i + 0];
      rgba[4 * i + 1] = rgb[3 * i + 1];
      rgba[4 * i + 2] = rgb[3 * i + 2];
      rgba[4 * i + 3] = 255;
    }
    decoded = true;
    return Status::Ok();
  }
};

CadrgRasterSource::CadrgRasterSource() : impl_(new Impl) {}
CadrgRasterSource::~CadrgRasterSource() = default;

Status CadrgRasterSource::Open(const std::string& path) {
  if (impl_->is_open) return Status::Error(kInvalidArg, "already open");
  fv::CadrgFrame frame;
  if (!frame.GetFrameProperties(path, &impl_->props) || !impl_->props.supported)
    return Status::Error(kUnsupported, "not a recognized CADRG frame: " + path);
  impl_->path = path;
  impl_->is_open = true;
  return Status::Ok();
}

GeoRect CadrgRasterSource::Bounds() const {
  if (!impl_->is_open) return GeoRect{};
  const auto& p = impl_->props;
  return GeoRect{{p.ll_lat, p.ll_lon}, {p.ur_lat, p.ur_lon}};
}

Status CadrgRasterSource::Info(ImageInfo* info) const {
  if (info == nullptr) return Status::Error(kInvalidArg, "info is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  info->size = PixelSize{kFrameSize, kFrameSize};
  info->bounds = Bounds();
  return Status::Ok();
}

Status CadrgRasterSource::ReadBlock(const PixelRect& r, PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 ||
      r.x + r.width > kFrameSize || r.y + r.height > kFrameSize)
    return Status::Error(kInvalidArg, "block rect outside frame");

  Status s = impl_->Decode();
  if (!s.ok()) return s;

  *out = PixelBuffer(r.width, r.height);
  for (int y = 0; y < r.height; ++y) {
    const unsigned char* src =
        impl_->rgba.data() + ((size_t)(r.y + y) * kFrameSize + r.x) * 4;
    std::memcpy(out->Row(y), src, (size_t)r.width * 4);
  }
  return Status::Ok();
}

// Equal-arc frames: pixel (0,0) is the NW corner; lon increases east with
// column, lat decreases south with row. Polar frames are non-linear -> not
// supported here (none in CONUS/TestData).
Status CadrgRasterSource::PixelToGeo(double px, double py, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  const auto& pr = impl_->props;
  if (pr.is_polar)
    return Status::Error(kUnsupported, "polar CADRG transform not implemented");
  p->lon = pr.ll_lon + px * pr.deg_per_pixel_lon;
  p->lat = pr.ur_lat - py * pr.deg_per_pixel_lat;
  return Status::Ok();
}

Status CadrgRasterSource::GeoToPixel(const GeoPoint& p, double* px,
                                     double* py) const {
  if (px == nullptr || py == nullptr)
    return Status::Error(kInvalidArg, "px/py is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  const auto& pr = impl_->props;
  if (pr.is_polar)
    return Status::Error(kUnsupported, "polar CADRG transform not implemented");
  *px = (p.lon - pr.ll_lon) / pr.deg_per_pixel_lon;
  *py = (pr.ur_lat - p.lat) / pr.deg_per_pixel_lat;
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// CadrgFrameCache
// ---------------------------------------------------------------------------

CadrgFrameCache::CadrgFrameCache(size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}
CadrgFrameCache::~CadrgFrameCache() = default;

std::shared_ptr<CadrgRasterSource> CadrgFrameCache::Get(const std::string& path,
                                                        Status* status) {
  Status ignored;
  Status& st = status ? *status : ignored;

  auto it = std::find_if(entries_.begin(), entries_.end(),
                         [&](const Entry& e) { return e.path == path; });
  if (it != entries_.end()) {
    Entry hit = std::move(*it);      // move to front (most-recently-used)
    entries_.erase(it);
    entries_.insert(entries_.begin(), std::move(hit));
    st = Status::Ok();
    return entries_.front().source;
  }

  auto source = std::make_shared<CadrgRasterSource>();
  st = source->Open(path);
  if (!st.ok()) return nullptr;

  entries_.insert(entries_.begin(), Entry{path, source});
  if (entries_.size() > capacity_) entries_.pop_back();  // evict LRU
  return source;
}

size_t CadrgFrameCache::Size() const { return entries_.size(); }

}  // namespace fv
