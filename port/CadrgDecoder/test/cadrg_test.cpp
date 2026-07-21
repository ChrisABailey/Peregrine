// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// Tests for the CADRG/RPF frame decoder (ImageLib/cadrg) against real GNC
// frames from TestData/rpf (skipped when absent). A CADRG frame is
// 1536x1536 pixels, VQ-compressed, palette-indexed.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "fv_compat.h"
#include "fv_cstring.h"
#include "imgdisp.h"

namespace {

std::string FramePath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/rpf/cgnc/1/00024023.gn1";
  return std::filesystem::exists(p) ? p : std::string();
}

TEST(CadrgDecode, FrameToRgbPixels) {
  std::string path = FramePath();
  if (path.empty()) GTEST_SKIP() << "no CADRG sample frames";

  const int W = 1536, H = 1536;
  std::vector<unsigned char> rgb(W * H * 3, 0);

  RPFRenderer r;
  ASSERT_EQ(0, r.get_rgb_image(path.c_str(), FALSE /*is_cib -> CADRG*/, 0, 0,
                               W, H, rgb.data()));

  // Decoded chart must have real content: many distinct colors and
  // nonzero variance (an all-black/all-white buffer means decode failed).
  long long sum = 0;
  for (unsigned char v : rgb) sum += v;
  double mean = (double)sum / rgb.size();
  EXPECT_GT(mean, 10.0);
  EXPECT_LT(mean, 245.0);

  std::vector<int> hist(256, 0);
  for (size_t i = 0; i < rgb.size(); i += 3) hist[rgb[i]]++;
  int distinct = 0;
  for (int c : hist) distinct += (c > 0);
  EXPECT_GT(distinct, 8) << "too few distinct red-channel values";
}

TEST(CadrgDecode, DeterministicAcrossRuns) {
  std::string path = FramePath();
  if (path.empty()) GTEST_SKIP() << "no CADRG sample frames";

  const int W = 1536, H = 1536;
  std::vector<unsigned char> a(W * H * 3, 0), b(W * H * 3, 1);
  RPFRenderer r1, r2;
  ASSERT_EQ(0, r1.get_rgb_image(path.c_str(), FALSE, 0, 0, W, H, a.data()));
  ASSERT_EQ(0, r2.get_rgb_image(path.c_str(), FALSE, 0, 0, W, H, b.data()));
  EXPECT_EQ(a, b);

  // Pinned checksum for regression (captured on this port 2026-07-11;
  // cross-check against the Windows build when reference dumps exist).
  unsigned long long checksum = 0;
  for (size_t i = 0; i < a.size(); ++i) checksum = checksum * 131 + a[i];
  EXPECT_EQ(checksum, 16252007341236444774ULL);
}

TEST(CadrgDecode, AllZonesOpenAndDecode) {
  const char* d = getenv("FVW_TESTDATA_DIR");
  // The build always sets FVW_TESTDATA_DIR, but the data itself is optional
  // (not distributed), so skip on a missing directory, not a missing var.
  if (!d || !std::filesystem::is_directory(std::string(d) + "/rpf"))
    GTEST_SKIP() << "no CADRG test data";
  int decoded = 0;
  for (const auto& type :
       std::filesystem::directory_iterator(std::string(d) + "/rpf")) {
    if (!type.is_directory()) continue;
    for (const auto& zone : std::filesystem::directory_iterator(type)) {
      if (!zone.is_directory()) continue;
      // first frame file in this zone
      for (const auto& f : std::filesystem::directory_iterator(zone)) {
        if (!f.is_regular_file()) continue;
        auto ext = f.path().extension().string();
        if (ext == ".DS_Store" || ext.empty()) continue;
        std::vector<unsigned char> rgb(1536 * 1536 * 3);
        RPFRenderer r;
        int rslt = r.get_rgb_image(f.path().string().c_str(), FALSE, 0, 0,
                                   1536, 1536, rgb.data());
        EXPECT_EQ(0, rslt) << f.path();
        if (rslt == 0) ++decoded;
        break;  // one frame per zone is enough
      }
    }
  }
  EXPECT_GT(decoded, 3) << "expected frames from several map types/zones";
}

}  // namespace
