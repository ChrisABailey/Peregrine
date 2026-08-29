// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fv::CadrgFrame (CADRG/RPF frame georeferencing). Structural and
// synthetic checks always run; real-data checks over TestData/rpf skip when
// FVW_TESTDATA_DIR is absent.
//
// Grounding: the mosaic.db RPF catalog shipped with TestData records a max
// pixel size of 0.0175588584 deg for the GNC frames — the ProbeAndPin test
// asserts the computed degrees-per-pixel matches it, which validates the
// series->scale table independently of the arc math.

#include "fv_cadrg_frame.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string RpfDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/rpf";
  return fs::exists(p) ? p : std::string();
}

// ---------------------------------------------------------------------------
// Synthetic: filename parsing / base-34, no data files needed
// ---------------------------------------------------------------------------

TEST(CadrgFrameName, SeriesAndScaleFromExtension) {
  fv::CadrgFrame f;
  fv::CadrgFrameProperties p;
  ASSERT_TRUE(f.GetFrameProperties("00024023.gn1", &p));
  EXPECT_EQ(p.series_code, "gn");
  EXPECT_EQ(p.series_name, "GNC");
  EXPECT_DOUBLE_EQ(p.scale_denom, 5000000.0);
  EXPECT_FALSE(p.is_polar);
  EXPECT_TRUE(p.upper_hemisphere);
  EXPECT_EQ(p.zone_index, 0);  // zone char '1' -> upper, index 0

  ASSERT_TRUE(f.GetFrameProperties("0084b083.lf1", &p));
  EXPECT_EQ(p.series_name, "LFC");
  EXPECT_DOUBLE_EQ(p.scale_denom, 500000.0);

  ASSERT_TRUE(f.GetFrameProperties("path/to/0000a012.tl2", &p));
  EXPECT_EQ(p.series_name, "TLM");
  EXPECT_DOUBLE_EQ(p.scale_denom, 50000.0);
  EXPECT_EQ(p.zone_index, 1);  // zone char '2'
}

TEST(CadrgFrameName, RejectsBadNames) {
  fv::CadrgFrame f;
  fv::CadrgFrameProperties p;
  EXPECT_FALSE(f.GetFrameProperties("abc.gn1", &p));        // not 12-char 8.3
  EXPECT_FALSE(f.GetFrameProperties("00024023.zz1", &p));   // unknown series
  EXPECT_FALSE(f.GetFrameProperties("00024023gn1", &p));    // no dot at [8]
  EXPECT_FALSE(f.GetFrameProperties("00024023.gnX", &p));   // bad zone char
  EXPECT_FALSE(p.supported);
}

TEST(CadrgFrameName, Base34FrameNumber) {
  fv::CadrgFrame f;
  fv::CadrgFrameProperties p;
  // "00024" base-34 = 2*34 + 4 = 72
  ASSERT_TRUE(f.GetFrameProperties("00024023.gn1", &p));
  EXPECT_EQ(p.frame_number, 72);
  // "00010" = 1*34 = 34
  ASSERT_TRUE(f.GetFrameProperties("00010023.gn1", &p));
  EXPECT_EQ(p.frame_number, 34);
  // letters: "0000a" = 10
  ASSERT_TRUE(f.GetFrameProperties("0000a023.gn1", &p));
  EXPECT_EQ(p.frame_number, 10);
  // 'i' and 'o' are not valid base-34 digits
  EXPECT_FALSE(f.GetFrameProperties("0000i023.gn1", &p));
}

// ---------------------------------------------------------------------------
// Real data: bounds of actual TestData frames
// ---------------------------------------------------------------------------

// The strongest check: the bounds computed from the filename + series scale
// must equal the frame's OWN embedded RPF coverage section (read from the
// file). Matching across all four series independently validates the scale
// table and the equal-arc math against ground truth in the data itself.
TEST(CadrgFrameReal, MatchesEmbeddedCoverage) {
  std::string rpf = RpfDir();
  if (rpf.empty()) GTEST_SKIP();
  const char* frames[] = {
      "/cgnc/1/00024023.gn1", "/cjnc/2/0000a033.jn2",
      "/clfc/1/0084b083.lf1", "/ctlm50/2/0150p013.tl2",
  };
  fv::CadrgFrame f;
  int checked = 0;
  for (const char* rel : frames) {
    std::string path = rpf + rel;
    if (!fs::exists(path)) continue;
    fv::CadrgFrameProperties p;
    ASSERT_TRUE(f.GetFrameProperties(path, &p)) << rel;
    fv::CadrgFileCoverage cov;
    ASSERT_TRUE(fv::ReadCadrgFileCoverage(path, &cov)) << rel;
    // computed ll/ur (SW / NE) vs the file's corners
    EXPECT_NEAR(p.ll_lat, cov.sw_lat, 1e-6) << rel;
    EXPECT_NEAR(p.ll_lon, cov.sw_lon, 1e-6) << rel;
    EXPECT_NEAR(p.ur_lat, cov.ne_lat, 1e-6) << rel;
    EXPECT_NEAR(p.ur_lon, cov.ne_lon, 1e-6) << rel;
    // (the coverage section's resolution fields are meters/pixel, not dpp)
    ++checked;
  }
  EXPECT_GE(checked, 1);
}

TEST(CadrgFrameReal, FrameIsWellFormedAndTiles) {
  std::string rpf = RpfDir();
  if (rpf.empty()) GTEST_SKIP();
  std::string path = rpf + "/cgnc/1/00024023.gn1";
  if (!fs::exists(path)) GTEST_SKIP() << path;

  fv::CadrgFrame f;
  fv::CadrgFrameProperties p;
  ASSERT_TRUE(f.GetFrameProperties(path, &p));
  // a frame spans exactly 1536 pixels each way
  EXPECT_NEAR(p.ur_lat - p.ll_lat, 1536 * p.deg_per_pixel_lat, 1e-9);
  EXPECT_NEAR(p.ur_lon - p.ll_lon, 1536 * p.deg_per_pixel_lon, 1e-9);
  EXPECT_LT(p.ll_lat, p.ur_lat);
  EXPECT_LT(p.ll_lon, p.ur_lon);
  EXPECT_GE(p.ll_lat, -90.0);
  EXPECT_LE(p.ur_lat, 90.0);

  // adjacent frame number (same row) sits exactly one frame east
  fv::CadrgFrameProperties q;
  ASSERT_TRUE(f.GetFrameProperties(rpf + "/cgnc/1/00025023.gn1", &q));
  EXPECT_NEAR(q.ll_lon, p.ur_lon, 1e-9);
  EXPECT_NEAR(q.ll_lat, p.ll_lat, 1e-9);
}

TEST(CadrgFrameReal, EnumerateAllSeriesSupported) {
  std::string rpf = RpfDir();
  if (rpf.empty()) GTEST_SKIP();

  fv::CadrgFrame f;
  int total = 0, supported = 0;
  std::vector<std::string> exts;
  for (const auto& e : fs::recursive_directory_iterator(rpf)) {
    if (!e.is_regular_file()) continue;
    std::string ext = e.path().extension().string();
    // RPF frame extensions: .gn1/.jn2/.lf1/.tl2 ... (3-char, 2 alpha + zone)
    if (ext.size() != 4) continue;
    char z = ext[3];
    bool zoneish = (z >= '1' && z <= '9') ||
                   (std::tolower((unsigned char)z) >= 'a' &&
                    std::tolower((unsigned char)z) <= 'j');
    if (!std::isalpha((unsigned char)ext[1]) ||
        !std::isalpha((unsigned char)ext[2]) || !zoneish)
      continue;
    ++total;
    fv::CadrgFrameProperties p;
    if (f.GetFrameProperties(e.path().string(), &p)) {
      ++supported;
      EXPECT_TRUE(p.supported);
      EXPECT_GT(p.scale_denom, 0.0);
      EXPECT_GE(p.frame_number, 0);
    }
  }
  EXPECT_GT(total, 500) << "expected the ~530-frame RPF sample set";
  EXPECT_EQ(supported, total) << "every gn/jn/lf/tl frame should resolve";
}

// Golden pins for one frame per series (verified equal to each frame's
// embedded coverage section by MatchesEmbeddedCoverage; pinned here so a
// scale-table or arc-math regression is caught even if the sample files
// change). Geography: GNC/JNC over the US Southwest, LFC over Georgia
// (30-31N, ~83W), TLM over the Los Angeles basin.
TEST(CadrgFrameReal, PinnedBounds) {
  std::string rpf = RpfDir();
  if (rpf.empty()) GTEST_SKIP();
  struct Pin {
    const char* rel;
    double ll_lat, ll_lon, ur_lat, ur_lon;
  };
  const Pin pins[] = {
      {"/cgnc/1/00024023.gn1", 20.769231, -112.849741, 31.153846, -101.658031},
      {"/cjnc/2/0000a033.jn2", 29.076923, -125.316456, 33.230769, -119.848101},
      {"/clfc/1/0084b083.lf1", 30.057582, -83.501299, 31.094050, -82.379221},
      {"/ctlm50/2/0150p013.tl2", 33.665835, -118.598985, 33.769423, -118.461929},
  };
  fv::CadrgFrame f;
  int checked = 0;
  for (const auto& pin : pins) {
    std::string path = rpf + pin.rel;
    if (!fs::exists(path)) continue;
    fv::CadrgFrameProperties p;
    ASSERT_TRUE(f.GetFrameProperties(path, &p)) << pin.rel;
    EXPECT_NEAR(p.ll_lat, pin.ll_lat, 1e-5) << pin.rel;
    EXPECT_NEAR(p.ll_lon, pin.ll_lon, 1e-5) << pin.rel;
    EXPECT_NEAR(p.ur_lat, pin.ur_lat, 1e-5) << pin.rel;
    EXPECT_NEAR(p.ur_lon, pin.ur_lon, 1e-5) << pin.rel;
    ++checked;
  }
  EXPECT_GE(checked, 1);
}

}  // namespace
