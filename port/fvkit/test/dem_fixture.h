// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Writes GeoTIFF DEM fixtures with libtiff, for the lidar tests.

#pragma once

#include <gtest/gtest.h>
#include <tiffio.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fv::test_dem {

struct GeoKey {
  uint16_t id;
  uint16_t value;
};

/// What to write: a single-band grid plus GeoTIFF tags.
struct DemSpec {
  int width = 40, height = 30;
  bool tiled = true;
  int block = 16;  // tile edge, or rows per strip
  uint16_t format = SAMPLEFORMAT_IEEEFP, bits = 32;
  uint16_t compression = COMPRESSION_LZW, predictor = PREDICTOR_NONE;
  std::vector<double> scale = {0.001, 0.001, 0.0};
  std::vector<double> tiepoint = {0, 0, 0, -80.0, 33.0, 0};
  std::vector<GeoKey> keys = {{1024, 2}, {1025, 1}, {2048, 4269}};
  std::string pcs_citation;  // written as key 3073 when set
  std::string nodata;        // GDAL_NODATA when set
  std::function<double(int c, int r)> value = [](int c, int r) { return 5 + 0.5 * c + 0.25 * r; };
  std::vector<std::pair<int, double>> overviews;  // (shrink factor, constant value)
};

inline void RegisterGeoTags(TIFF* tif) {
  static const TIFFFieldInfo kFields[] = {
      {33550, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, (char*)"ModelPixelScale"},
      {33922, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, (char*)"ModelTiepoint"},
      {34735, -1, -1, TIFF_SHORT, FIELD_CUSTOM, 1, 1, (char*)"GeoKeyDirectory"},
      {34737, -1, -1, TIFF_ASCII, FIELD_CUSTOM, 1, 0, (char*)"GeoASCIIParams"},
      {42113, -1, -1, TIFF_ASCII, FIELD_CUSTOM, 1, 0, (char*)"GDALNoDataValue"},
  };
  TIFFMergeFieldInfo(tif, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

inline void PutSample(const DemSpec& s, double v, unsigned char* p) {
  if (s.format == SAMPLEFORMAT_IEEEFP && s.bits == 32) *reinterpret_cast<float*>(p) = (float)v;
  else if (s.format == SAMPLEFORMAT_IEEEFP) *reinterpret_cast<double*>(p) = v;
  else if (s.format == SAMPLEFORMAT_INT && s.bits == 16) *reinterpret_cast<int16_t*>(p) = (int16_t)v;
  else if (s.format == SAMPLEFORMAT_INT) *reinterpret_cast<int32_t*>(p) = (int32_t)v;
  else if (s.bits == 16) *reinterpret_cast<uint16_t*>(p) = (uint16_t)v;
  else *reinterpret_cast<uint32_t*>(p) = (uint32_t)v;
}

/// Writes one IFD of `w` x `h` posts from `value`.
inline void WriteImage(TIFF* tif, const DemSpec& s, int w, int h, bool reduced,
                const std::function<double(int, int)>& value) {
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, s.bits);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, s.format);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_COMPRESSION, s.compression);
  if (s.predictor != PREDICTOR_NONE) TIFFSetField(tif, TIFFTAG_PREDICTOR, s.predictor);
  if (reduced) TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
  const int bytes = s.bits / 8;
  if (!reduced) {
    TIFFSetField(tif, 33550, 3, s.scale.data());
    TIFFSetField(tif, 33922, 6, s.tiepoint.data());
    std::vector<uint16_t> dir = {1, 1, 0, 0};
    std::vector<GeoKey> keys = s.keys;
    std::string ascii;
    for (const GeoKey& k : keys) dir.insert(dir.end(), {k.id, 0, 1, k.value});
    if (!s.pcs_citation.empty()) {
      ascii = s.pcs_citation + "|";
      dir.insert(dir.end(), {3073, 34737, (uint16_t)ascii.size(), 0});
    }
    dir[3] = (uint16_t)((dir.size() - 4) / 4);
    TIFFSetField(tif, 34735, (uint16_t)dir.size(), dir.data());
    if (!ascii.empty()) TIFFSetField(tif, 34737, ascii.c_str());
    if (!s.nodata.empty()) TIFFSetField(tif, 42113, s.nodata.c_str());
  }
  if (s.tiled) {
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, s.block);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, s.block);
    std::vector<unsigned char> buf((size_t)s.block * s.block * bytes);
    for (int ty = 0; ty < h; ty += s.block)
      for (int tx = 0; tx < w; tx += s.block) {
        std::fill(buf.begin(), buf.end(), 0);
        for (int y = 0; y < s.block; ++y)
          for (int x = 0; x < s.block; ++x)
            if (tx + x < w && ty + y < h)
              PutSample(s, value(tx + x, ty + y), &buf[((size_t)y * s.block + x) * bytes]);
        ASSERT_GE(TIFFWriteTile(tif, buf.data(), tx, ty, 0, 0), 0);
      }
  } else {
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, s.block);
    std::vector<unsigned char> row((size_t)w * bytes);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) PutSample(s, value(x, y), &row[(size_t)x * bytes]);
      ASSERT_GE(TIFFWriteScanline(tif, row.data(), y, 0), 0);
    }
  }
}

inline void WriteDem(const std::string& path, const DemSpec& s) {
  TIFF* tif = TIFFOpen(path.c_str(), "w");
  ASSERT_NE(tif, nullptr);
  RegisterGeoTags(tif);
  WriteImage(tif, s, s.width, s.height, false, s.value);
  for (const auto& [factor, v] : s.overviews) {
    ASSERT_TRUE(TIFFWriteDirectory(tif));
    const double c = v;
    WriteImage(tif, s, s.width / factor, s.height / factor, true, [c](int, int) { return c; });
  }
  TIFFClose(tif);
}

}  // namespace fv::test_dem
