// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// png_write.h — tiny RGBA8 PixelBuffer -> PNG writer over the ported libpng
// (fv_png). Shared by fvrender and tests.

#pragma once

#include <png.h>

#include <cstdio>
#include <string>

#include "fvkit/raster.h"

namespace fv {

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
