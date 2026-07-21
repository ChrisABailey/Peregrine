// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// TIFF round-trip tests, in their own binary because fv_tiff links GDAL's
// C libjpeg whose unmangled symbols collide with FalconView's C++ jpeg
// (separate DLLs on Windows).

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "tiffio.h"

namespace {

std::filesystem::path TempDir() {
  auto d = std::filesystem::temp_directory_path() /
           ("fv_tiff_" + std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d;
}

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

TEST(Codecs, TiffWriteReadRoundTrip) {
  const int W = 64, H = 48;
  auto rgb = MakeRgb(W, H);
  auto path = (TempDir() / "rt.tif").string();

  {  // write LZW-compressed strips
    TIFF* tif = TIFFOpen(path.c_str(), "w");
    ASSERT_NE(tif, nullptr);
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, W);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, H);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    for (int y = 0; y < H; ++y)
      ASSERT_GE(TIFFWriteScanline(tif, &rgb[y * W * 3], y, 0), 0);
    TIFFClose(tif);
  }
  {  // read back (exercises fv_tif_posix.c glue incl. mmap path)
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    ASSERT_NE(tif, nullptr);
    uint32 w = 0, h = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    ASSERT_EQ(w, (uint32)W);
    ASSERT_EQ(h, (uint32)H);
    std::vector<unsigned char> out(W * H * 3);
    for (int y = 0; y < H; ++y)
      ASSERT_GE(TIFFReadScanline(tif, &out[y * W * 3], y, 0), 0);
    TIFFClose(tif);
    EXPECT_EQ(out, rgb);  // LZW lossless
  }
}

TEST(Codecs, TiffVersionIsVendored394) {
  EXPECT_NE(std::string(TIFFGetVersion()).find("3.9.4"), std::string::npos);
}

}  // namespace
