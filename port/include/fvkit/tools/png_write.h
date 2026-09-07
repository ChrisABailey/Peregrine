// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// png_write.h — tiny RGBA8 PixelBuffer -> PNG writer over the ported libpng
// (fv_png). Shared by fvrender and tests.
//
// Two forms of the same encode: to a file, and to a blob. The blob is what a
// caller that STORES a PNG wants — a `.fvpoints` symbol row carries the bytes,
// not a path — and writing a temporary file to read it back would be the same
// work with a filesystem in the middle.

#pragma once

#include <png.h>

#include <cstdio>
#include <string>
#include <vector>

#include "fvkit/raster.h"

namespace fv {

namespace detail {

inline void PngWriteToVector(png_structp png, png_bytep data, png_size_t len) {
  auto* out = static_cast<std::vector<unsigned char>*>(png_get_io_ptr(png));
  out->insert(out->end(), data, data + len);
}

// libpng requires a flush callback when the write is not to a FILE*. A vector
// has nothing to flush.
inline void PngFlushNothing(png_structp /*png*/) {}

}  // namespace detail

// Encodes an RGBA8 buffer as a PNG in memory.
inline Status EncodePng(const PixelBuffer& b,
                        std::vector<unsigned char>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  out->clear();
  if (b.Empty()) return Status::Error(kInvalidArg, "empty image");
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png == nullptr) return Status::Error(kInternal, "libpng alloc failed");
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_write_struct(&png, nullptr);
    return Status::Error(kInternal, "libpng alloc failed");
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    out->clear();
    return Status::Error(kIoError, "libpng failure encoding");
  }
  png_set_write_fn(png, out, detail::PngWriteToVector, detail::PngFlushNothing);
  png_set_IHDR(png, info, b.Width(), b.Height(), 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (int y = 0; y < b.Height(); ++y)
    png_write_row(png, const_cast<unsigned char*>(b.Row(y)));
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  return Status::Ok();
}

inline Status WritePng(const PixelBuffer& b, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return Status::Error(kIoError, "cannot write " + path);
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(png);
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    std::fclose(f);
    return Status::Error(kIoError, "libpng failure writing " + path);
  }
  png_init_io(png, f);
  png_set_IHDR(png, info, b.Width(), b.Height(), 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  for (int y = 0; y < b.Height(); ++y)
    png_write_row(png, const_cast<unsigned char*>(b.Row(y)));
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  std::fclose(f);
  return Status::Ok();
}

}  // namespace fv
