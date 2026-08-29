// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GeoTiffRasterSource implementation — see fvkit/formats/geotiff.h.
// This TU is pinned to C++14: ImageLib's geotiff.h (included below) still
// carries std::auto_ptr members. Keep legacy types inside Impl.

#include "fvkit/formats/geotiff.h"

#include <cmath>
#include <vector>

#include "stdafx.h"
#include "geotiff.h"  // ImageLib's CGeoTiff (via ILC include dirs)

#include "fv_geotiff_frame.h"

namespace fv {

namespace {

// path -> (directory with trailing separator, file name), as
// GeoTiffFrame::GetFrameProperties expects.
void SplitPath(const std::string& path, std::string* dir, std::string* name) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    *dir = "./";
    *name = path;
  } else {
    *dir = path.substr(0, pos + 1);
    *name = path.substr(pos + 1);
  }
}

}  // namespace

struct GeoTiffRasterSource::Impl {
  CGeoTiff gt;
  GeoTiffFrameProperties props;
  int width = 0;
  int height = 0;
  bool is_open = false;
};

GeoTiffRasterSource::GeoTiffRasterSource() : impl_(new Impl) {}
GeoTiffRasterSource::~GeoTiffRasterSource() = default;

Status GeoTiffRasterSource::Open(const std::string& path) {
  if (impl_->is_open) return Status::Error(kInvalidArg, "already open");

  BOOL type_ok = FALSE, geo_present = FALSE, geo_ok = FALSE;
  int image_type = 0;
  CString type_desc, err;
  int rc = impl_->gt.load(path.c_str(), type_ok, image_type, type_desc,
                          impl_->width, impl_->height, geo_present, geo_ok,
                          err);
  if (rc != 0)
    return Status::Error(kIoError, "CGeoTiff load failed (" +
                                       std::to_string(rc) + "): " +
                                       std::string((LPCSTR)err));
  if (!type_ok)
    return Status::Error(kUnsupported, "unsupported TIFF type: " +
                                           std::string((LPCSTR)type_desc));
  if (!geo_present || !geo_ok)
    return Status::Error(kUnsupported, "no usable georeferencing: " + path);

  std::string dir, name;
  SplitPath(path, &dir, &name);
  GeoTiffFrame frame;
  HRESULT hr = frame.GetFrameProperties(dir, name, &impl_->props);
  if (FAILED(hr) || !impl_->props.supported)
    return Status::Error(kUnsupported,
                         "GetFrameProperties rejected frame: " + path);

  impl_->is_open = true;
  return Status::Ok();
}

GeoRect GeoTiffRasterSource::Bounds() const {
  if (!impl_->is_open) return GeoRect{};
  return GeoRect{{impl_->props.ll_lat, impl_->props.ll_lon},
                 {impl_->props.ur_lat, impl_->props.ur_lon}};
}

Status GeoTiffRasterSource::Info(ImageInfo* info) const {
  if (info == nullptr) return Status::Error(kInvalidArg, "info is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  info->size = PixelSize{impl_->width, impl_->height};
  info->bounds = Bounds();
  return Status::Ok();
}

Status GeoTiffRasterSource::ReadBlock(const PixelRect& r, PixelBuffer* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "out is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  if (r.width <= 0 || r.height <= 0 || r.x < 0 || r.y < 0 ||
      r.x + r.width > impl_->width || r.y + r.height > impl_->height)
    return Status::Error(kInvalidArg,
                         "block rect outside image (callers clip first)");

  std::vector<unsigned char> rgb((size_t)r.width * r.height * 3);
  int rc = impl_->gt.get_rgb_subimage(r.x, r.y, r.width, r.height, rgb.data(),
                                      nullptr);
  if (rc != 0)
    return Status::Error(kIoError,
                         "get_rgb_subimage failed: " + std::to_string(rc));

  *out = PixelBuffer(r.width, r.height);
  for (int y = 0; y < r.height; ++y) {
    const unsigned char* src = rgb.data() + (size_t)y * r.width * 3;
    unsigned char* dst = out->Row(y);
    for (int x = 0; x < r.width; ++x) {
      dst[4 * x + 0] = src[3 * x + 0];
      dst[4 * x + 1] = src[3 * x + 1];
      dst[4 * x + 2] = src[3 * x + 2];
      dst[4 * x + 3] = 255;
    }
  }
  return Status::Ok();
}

Status GeoTiffRasterSource::PixelToGeo(double px, double py, GeoPoint* p) const {
  if (p == nullptr) return Status::Error(kInvalidArg, "p is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  // legacy transforms are whole-pixel; nearest pixel center
  int rc = impl_->gt.inv_transform((int)std::lround(px), (int)std::lround(py),
                                   p->lat, p->lon);
  if (rc != 0)
    return Status::Error(kInvalidArg,
                         "inv_transform failed: " + std::to_string(rc));
  return Status::Ok();
}

Status GeoTiffRasterSource::GeoToPixel(const GeoPoint& p, double* px,
                                       double* py) const {
  if (px == nullptr || py == nullptr)
    return Status::Error(kInvalidArg, "px/py is null");
  if (!impl_->is_open) return Status::Error(kInvalidArg, "not open");
  int x = 0, y = 0;
  int rc = impl_->gt.fwd_transform(p.lat, p.lon, x, y);
  if (rc != 0)
    return Status::Error(kInvalidArg,
                         "fwd_transform failed: " + std::to_string(rc));
  *px = x;
  *py = y;
  return Status::Ok();
}

}  // namespace fv
