// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// png_io.h — RGBA8 PixelBuffer <- PNG over the ported libpng (fv_png).
//
// The writer half is png_write.h. This is the READER, factored out on its
// third consumer (the GeoPackage tile store decoding a tile blob, canvas_test
// reading a golden back, and now the S-52 raster symbol sheet). Every caller
// wants the same thing — whatever the file says, hand me RGBA8 — so the
// expansion transforms are set once, here, rather than remembered three times.
//
// Anything libpng can read becomes RGBA8: palette and low-bit-depth greys are
// expanded, greyscale is promoted to RGB, tRNS becomes a real alpha channel,
// 16-bit samples are stripped to 8. Interlaced files are NOT handled (a
// progressive read needs the whole image at once); they are rejected rather
// than silently decoded wrong.

#pragma once

#include <png.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fvkit/raster.h"

namespace fv {

namespace detail {

struct PngReadCursor {
  const unsigned char* data;
  size_t size, pos;
};

inline void PngReadFromMemory(png_structp png, png_bytep out, png_size_t len) {
  auto* c = static_cast<PngReadCursor*>(png_get_io_ptr(png));
  if (c->pos + len > c->size) png_error(png, "png blob truncated");
  std::memcpy(out, c->data + c->pos, len);
  c->pos += len;
}

}  // namespace detail

// Decodes an in-memory PNG to RGBA8.
inline Status DecodePng(const void* data, size_t size, PixelBuffer* out) {
  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png == nullptr) return Status::Error(kInternal, "libpng alloc failed");
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    return Status::Error(kInternal, "libpng alloc failed");
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    return Status::Error(kIoError, "libpng decode failed");
  }
  detail::PngReadCursor cur{static_cast<const unsigned char*>(data), size, 0};
  png_set_read_fn(png, &cur, detail::PngReadFromMemory);
  png_read_info(png, info);
  const int w = static_cast<int>(png_get_image_width(png, info));
  const int h = static_cast<int>(png_get_image_height(png, info));
  if (png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) {
    png_destroy_read_struct(&png, &info, nullptr);
    return Status::Error(kUnsupported, "interlaced PNG");
  }
  png_set_expand(png);      // palette / low-bit grey / tRNS -> 8-bit channels
  png_set_strip_16(png);    // 16-bit samples -> 8
  png_set_packing(png);
  png_set_gray_to_rgb(png);
  png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);  // no-op when already RGBA
  png_read_update_info(png, info);
  if (png_get_rowbytes(png, info) != static_cast<png_size_t>(w) * 4) {
    png_destroy_read_struct(&png, &info, nullptr);
    return Status::Error(kUnsupported, "PNG did not expand to RGBA8");
  }
  *out = PixelBuffer(w, h);
  for (int y = 0; y < h; ++y) png_read_row(png, out->Row(y), nullptr);
  png_destroy_read_struct(&png, &info, nullptr);
  return Status::Ok();
}

// Reads a PNG file to RGBA8.
inline Status ReadPngFile(const std::string& path, PixelBuffer* out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return Status::Error(kIoError, "cannot read " + path);
  std::vector<unsigned char> bytes;
  unsigned char chunk[64 * 1024];
  for (;;) {
    const size_t n = std::fread(chunk, 1, sizeof(chunk), f);
    if (n == 0) break;
    bytes.insert(bytes.end(), chunk, chunk + n);
  }
  const bool bad = std::ferror(f) != 0;
  std::fclose(f);
  if (bad) return Status::Error(kIoError, "read error on " + path);
  Status s = DecodePng(bytes.data(), bytes.size(), out);
  if (!s.ok()) return Status::Error(s.code, s.message + " (" + path + ")");
  return s;
}

}  // namespace fv
