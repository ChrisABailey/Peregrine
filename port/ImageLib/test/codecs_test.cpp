// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Round-trip tests for the vendored ImageLib codecs on macOS: every codec
// encodes a synthetic image and decodes it back, so no external test data is
// required. Byte-exactness vs the Windows build is deferred to Phase 2b
// golden files; here we prove the vendored sources produce self-consistent
// output on this platform.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "zlib.h"
extern "C" {
#include "png.h"
}
#include <csetjmp>
// IJG jpeg: C++-compiled in this tree, no extern "C"
#include "jpeglib.h"

namespace {

std::filesystem::path TempDir() {
  auto d = std::filesystem::temp_directory_path() /
           ("fv_codecs_" + std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d;
}

// 64x48 RGB test pattern with gradients and hard edges
std::vector<unsigned char> MakeRgb(int w, int h) {
  std::vector<unsigned char> v(w * h * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char* p = &v[(y * w + x) * 3];
      p[0] = (unsigned char)(x * 255 / (w - 1));
      p[1] = (unsigned char)(y * 255 / (h - 1));
      p[2] = ((x / 8 + y / 8) & 1) ? 255 : 0;
    }
  return v;
}

TEST(Codecs, ZlibRoundTripAndCrc) {
  std::string text;
  for (int i = 0; i < 200; ++i) text += "FalconView zlib round trip. ";
  // NOTE: this vendored zlib omits compress.c/uncompr.c, so use the core
  // deflate/inflate API directly.
  std::vector<Bytef> comp(deflateBound(nullptr, text.size()) + 64);
  z_stream ds{};
  ASSERT_EQ(Z_OK, deflateInit(&ds, Z_DEFAULT_COMPRESSION));
  ds.next_in = (Bytef*)text.data();
  ds.avail_in = (uInt)text.size();
  ds.next_out = comp.data();
  ds.avail_out = (uInt)comp.size();
  ASSERT_EQ(Z_STREAM_END, deflate(&ds, Z_FINISH));
  uLong clen = ds.total_out;
  deflateEnd(&ds);
  EXPECT_LT(clen, text.size() / 4);  // highly repetitive: must compress well

  std::vector<Bytef> out(text.size());
  z_stream is{};
  ASSERT_EQ(Z_OK, inflateInit(&is));
  is.next_in = comp.data();
  is.avail_in = (uInt)clen;
  is.next_out = out.data();
  is.avail_out = (uInt)out.size();
  ASSERT_EQ(Z_STREAM_END, inflate(&is, Z_FINISH));
  ASSERT_EQ(is.total_out, text.size());
  inflateEnd(&is);
  EXPECT_EQ(0, memcmp(out.data(), text.data(), text.size()));

  // crc32 pinned value for "123456789" (standard check vector)
  EXPECT_EQ(0xCBF43926u, crc32(0, (const Bytef*)"123456789", 9));
  EXPECT_STREQ(zlibVersion(), "1.2.5");
}

TEST(Codecs, PngWriteReadRoundTrip) {
  const int W = 64, H = 48;
  auto rgb = MakeRgb(W, H);
  auto path = (TempDir() / "rt.png").string();

  {  // write
    FILE* fp = fopen(path.c_str(), "wb");
    ASSERT_NE(fp, nullptr);
    png_structp png =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    ASSERT_EQ(setjmp(png_jmpbuf(png)), 0);
    png_init_io(png, fp);
    png_set_IHDR(png, info, W, H, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < H; ++y) png_write_row(png, &rgb[y * W * 3]);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
  }
  {  // read back
    FILE* fp = fopen(path.c_str(), "rb");
    ASSERT_NE(fp, nullptr);
    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    ASSERT_EQ(setjmp(png_jmpbuf(png)), 0);
    png_init_io(png, fp);
    png_read_info(png, info);
    EXPECT_EQ(png_get_image_width(png, info), (png_uint_32)W);
    EXPECT_EQ(png_get_image_height(png, info), (png_uint_32)H);
    EXPECT_EQ(png_get_color_type(png, info), PNG_COLOR_TYPE_RGB);
    std::vector<unsigned char> row(W * 3), all;
    for (int y = 0; y < H; ++y) {
      png_read_row(png, row.data(), nullptr);
      all.insert(all.end(), row.begin(), row.end());
    }
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);
    EXPECT_EQ(all, rgb);  // PNG is lossless: byte-identical
  }
}

TEST(Codecs, JpegCompressDecompressRoundTrip) {
  const int W = 64, H = 48;
  auto rgb = MakeRgb(W, H);
  auto path = (TempDir() / "rt.jpg").string();

  {  // compress at high quality
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    FILE* fp = fopen(path.c_str(), "wb");
    ASSERT_NE(fp, nullptr);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = W;
    cinfo.image_height = H;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
      JSAMPROW row = &rgb[cinfo.next_scanline * W * 3];
      jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(fp);
  }
  {  // decompress and sanity-check pixels (lossy: loose tolerance)
    jpeg_decompress_struct dinfo;
    jpeg_error_mgr jerr;
    dinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&dinfo);
    FILE* fp = fopen(path.c_str(), "rb");
    ASSERT_NE(fp, nullptr);
    jpeg_stdio_src(&dinfo, fp);
    ASSERT_EQ(JPEG_HEADER_OK, jpeg_read_header(&dinfo, TRUE));
    jpeg_start_decompress(&dinfo);
    EXPECT_EQ(dinfo.output_width, (JDIMENSION)W);
    EXPECT_EQ(dinfo.output_height, (JDIMENSION)H);
    EXPECT_EQ(dinfo.output_components, 3);
    std::vector<unsigned char> out(W * H * 3);
    while (dinfo.output_scanline < dinfo.output_height) {
      JSAMPROW row = &out[dinfo.output_scanline * W * 3];
      jpeg_read_scanlines(&dinfo, &row, 1);
    }
    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);
    fclose(fp);

    // gradient channels should survive q95 within a few counts
    long total_err = 0;
    for (size_t i = 0; i < out.size(); ++i)
      total_err += labs((long)out[i] - (long)rgb[i]);
    double mean_err = (double)total_err / out.size();
    // q95 with default 2x2 chroma subsampling blurs the hard checkerboard
    // edges in the blue channel; ~10 mean abs error is normal here.
    EXPECT_LT(mean_err, 12.0) << "mean abs error too high for q95";
  }
}


}  // namespace
