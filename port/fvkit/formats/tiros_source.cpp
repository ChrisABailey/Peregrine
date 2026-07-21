// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// TirosRasterSource implementation — see fvkit/formats/tiros.h. Includes the
// CJpeg reader (CString-based) so legacy types stay inside Impl.

#include "fvkit/formats/tiros.h"

#include <cstring>
#include <vector>

#include "fv_compat.h"
#include "fv_cstring.h"
#include "jpeg.h"            // CJpeg (fv_imagelib_jpeg_wrapper)
#include "fv_tiros_frame.h"  // fv::TirosFrame (port/TirosMapServer)

namespace fv {

struct TirosRasterSource::Impl {
  std::string path;
  TirosFrameProperties props;
  CJpeg jpeg;       // load() in Open reads the header (real dims); get_jpeg_
                    // image() in Decode() runs on the same object
  int width = 0;    // actual JPEG dimensions (not assumed 1350)
  int height = 0;
  bool is_open = false;
  bool decoded = false;
  std::vector<unsigned char> rgba;  // width*height*4, lazily filled

  // Decode the whole tile once into interleaved RGBA (CJpeg returns planes).
  Status Decode() {
    if (decoded) return Status::Ok();
    CString err;
    const size_t n = (size_t)width * height;
    std::vector<unsigned char> r(n), g(n), b(n);
    if (jpeg.get_jpeg_image(0, 0, width, height, r.data(), g.data(), b.data(),
                            err) != 0)
      return Status::Error(kIoError, "CJpeg decode failed: " +
                                         std::string((LPCSTR)err));
    rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
      rgba[4 * i + 0] = r[i];
      rgba[4 * i + 1] = g[i];
      rgba[4 * i + 2] = b[i];
      rgba[4 * i + 3] = 255;
    }
    decoded = true;
    return Status::Ok();
  }
};

TirosRasterSource::TirosRasterSource() : impl_(new Impl) {}
TirosRasterSource::~TirosRasterSource() = default;

Status TirosRasterSource::Open(const std::string& path) {
  if (impl_->is_open) return Status::Error(kInvalidArg, "already open");
  fv::TirosFrame frame;
  if (!frame.GetFrameProperties(path, &impl_->props) ||
      !impl_->props.supported || impl_->props.whole_world)
    return Status::Error(kUnsupported, "not a georeferenced TIROS tile: " + path);
  // Read the JPEG header for the real dimensions (cheap; no full decode) so
  // ReadBlock's stride is correct even if a tile isn't the nominal 1350^2.
  CString err;
  int w = 0, h = 0;
  if (impl_->jpeg.load(path.c_str(), w, h, err) != 0)
    return Status::Error(kIoError,
                         "CJpeg load failed: " + std::string((LPCSTR)err));
  impl_->width = w;
  impl_->height = h;
  impl_->path = path;
  impl_->is_open = true;
  return Status::Ok();
}

GeoRect TirosRasterSource::Bounds() const {
  if (!impl_->is_open) return GeoRect{};
  const auto& p = impl_->props;
  return GeoRect{{p.ll_lat, p.ll_lon}, {p.ur_lat, p.ur_lon}};
}

Status TirosRasterSource::Info(ImageInfo* info) const {
  if (info == nullptr) return Status::Error(kInvalidArg, "info is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  info->size = PixelSize{impl_->width, impl_->height};
  info->bounds = Bounds();
  return Status::Ok();
}

Status TirosRasterSource::ReadBlock(const PixelRect& r, PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 ||
      r.x + r.width > impl_->width || r.y + r.height > impl_->height)
    return Status::Error(kInvalidArg, "block rect outside tile");

  Status s = impl_->Decode();
  if (!s.ok()) return s;

  *out = PixelBuffer(r.width, r.height);
  for (int y = 0; y < r.height; ++y) {
    const unsigned char* src =
        impl_->rgba.data() + ((size_t)(r.y + y) * impl_->width + r.x) * 4;
    std::memcpy(out->Row(y), src, (size_t)r.width * 4);
  }
  return Status::Ok();
}

// Equal-arc: pixel (0,0) is the NW corner; lon east with column, lat south
// with row (same convention as the CADRG adapter).
Status TirosRasterSource::PixelToGeo(double px, double py, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  p->lon = impl_->props.ll_lon + px * impl_->props.deg_per_pixel_lon;
  p->lat = impl_->props.ur_lat - py * impl_->props.deg_per_pixel_lat;
  return Status::Ok();
}

Status TirosRasterSource::GeoToPixel(const GeoPoint& p, double* px,
                                     double* py) const {
  if (px == nullptr || py == nullptr)
    return Status::Error(kInvalidArg, "px/py is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  *px = (p.lon - impl_->props.ll_lon) / impl_->props.deg_per_pixel_lon;
  *py = (impl_->props.ur_lat - p.lat) / impl_->props.deg_per_pixel_lat;
  return Status::Ok();
}

}  // namespace fv
