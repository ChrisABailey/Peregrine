// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for fv::DtedShadedRenderer over real TestData DTED cells (skipped if
// FVW_TESTDATA_DIR / the sample tree is absent, like the other real-data
// suites). Pins geometry, the equal-arc transforms, determinism, that the
// display modes and lighting actually change the output, and a regression
// checksum of the default shaded-relief render.

#include "fv_dted_shaded_renderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string TestDataDir() {
  const char* d = std::getenv("FVW_TESTDATA_DIR");
  return d ? d : "";
}

// w082/n31.dt1: DTED1 cell, SW corner (31N, 82W) -> spans lat[31,32],
// lon[-82,-81]. Central Georgia; present in TestData.
std::string CellPath() {
  if (TestDataDir().empty()) return "";
  fs::path p = fs::path(TestDataDir()) / "dted" / "w082" / "n31.dt1";
  return fs::exists(p) ? p.string() : "";
}

// w084/n35.dt1: the southern Appalachians (~300-1700 m). The cell above is
// tidal Georgia — flat enough that EVERY elevation lands in band 0 whatever
// the breakpoints are, which is exactly how the 2026-07-25 elevation-band unit
// bug passed a visual check and a pinned checksum. Band behaviour has to be
// asserted over terrain with real relief.
std::string ReliefCellPath() {
  if (TestDataDir().empty()) return "";
  fs::path p = fs::path(TestDataDir()) / "dted" / "w084" / "n35.dt1";
  return fs::exists(p) ? p.string() : "";
}

uint64_t Fnv1a(const fv::PixelBuffer& b) {
  uint64_t h = 1469598103934665603ull;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width() * 4; ++x) {
      h ^= row[x];
      h *= 1099511628211ull;
    }
  }
  return h;
}

class DtedShaded : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = CellPath();
    if (path_.empty()) GTEST_SKIP() << "TestData DTED cell not present";
  }
  std::string path_;
};

TEST_F(DtedShaded, OpensAndReportsGeometry) {
  fv::DtedShadedRenderer r;
  ASSERT_TRUE(r.Open(path_).ok());
  EXPECT_TRUE(r.IsOpen());
  // DTED1 = 1201x1201 posts; the north row + east column are trimmed -> 1200.
  EXPECT_EQ(r.RenderedWidth(), 1200);
  EXPECT_EQ(r.RenderedHeight(), 1200);

  fv::GeoRect b = r.ImageBounds();
  EXPECT_NEAR(b.ll.lon, -82.0, 0.01);
  EXPECT_NEAR(b.ur.lon, -81.0, 0.01);
  EXPECT_NEAR(b.ll.lat, 31.0, 0.01);
  EXPECT_NEAR(b.ur.lat, 32.0, 0.01);
}

TEST_F(DtedShaded, PixelGeoRoundTrip) {
  fv::DtedShadedRenderer r;
  ASSERT_TRUE(r.Open(path_).ok());
  // Top-left rendered pixel is north-west; row increases south.
  fv::GeoPoint nw, se;
  ASSERT_TRUE(r.PixelToGeo(0, 0, &nw));
  ASSERT_TRUE(r.PixelToGeo(r.RenderedWidth() - 1, r.RenderedHeight() - 1, &se));
  EXPECT_GT(nw.lat, se.lat);   // north above south
  EXPECT_LT(nw.lon, se.lon);   // west left of east

  double px, py;
  ASSERT_TRUE(r.GeoToPixel(nw, &px, &py));
  EXPECT_NEAR(px, 0.0, 1e-6);
  EXPECT_NEAR(py, 0.0, 1e-6);

  fv::GeoPoint mid;
  ASSERT_TRUE(r.PixelToGeo(500.0, 700.0, &mid));
  ASSERT_TRUE(r.GeoToPixel(mid, &px, &py));
  EXPECT_NEAR(px, 500.0, 1e-6);
  EXPECT_NEAR(py, 700.0, 1e-6);
}

TEST_F(DtedShaded, RendersOpaqueDeterministicRelief) {
  fv::DtedShadedRenderer r;
  ASSERT_TRUE(r.Open(path_).ok());
  fv::PixelBuffer a, b;
  ASSERT_TRUE(r.Render(&a).ok());
  ASSERT_TRUE(r.Render(&b).ok());
  ASSERT_EQ(a.Width(), 1200);
  ASSERT_EQ(a.Height(), 1200);

  // Every pixel opaque (Windows drew DTED non-transparent).
  for (int y = 0; y < a.Height(); y += 37)
    for (int x = 0; x < a.Width(); ++x) EXPECT_EQ(a.Row(y)[4 * x + 3], 255);

  // Deterministic.
  EXPECT_EQ(Fnv1a(a), Fnv1a(b));

  // Not a blank/all-black image: some non-black pixel exists.
  bool any_color = false;
  for (int y = 0; y < a.Height() && !any_color; y += 11)
    for (int x = 0; x < a.Width(); ++x)
      if (a.Row(y)[4 * x] || a.Row(y)[4 * x + 1] || a.Row(y)[4 * x + 2]) {
        any_color = true;
        break;
      }
  EXPECT_TRUE(any_color);

  // Regression pin (deterministic over this cell + default config). If TestData
  // changes this cell, re-pin after a visual check.
  EXPECT_EQ(Fnv1a(a), 7626760159377150599ull) << "actual = " << Fnv1a(a);
}

TEST_F(DtedShaded, DisplayModesAndLightingChangeOutput) {
  fv::DtedShadedRenderer color;
  ASSERT_TRUE(color.Open(path_).ok());
  color.SetDisplayMode(fv::kDtedRenderedColor);
  fv::PixelBuffer relief;
  ASSERT_TRUE(color.Render(&relief).ok());

  fv::DtedShadedRenderer elev;
  ASSERT_TRUE(elev.Open(path_).ok());
  elev.SetDisplayMode(fv::kDtedElevationColor);
  fv::PixelBuffer bands;
  ASSERT_TRUE(elev.Render(&bands).ok());
  EXPECT_NE(Fnv1a(relief), Fnv1a(bands)) << "elevation banding == shaded relief";

  fv::DtedShadedRenderer slope;
  ASSERT_TRUE(slope.Open(path_).ok());
  slope.SetDisplayMode(fv::kDtedSlopeColor);
  fv::PixelBuffer slopes;
  ASSERT_TRUE(slope.Render(&slopes).ok());
  EXPECT_NE(Fnv1a(relief), Fnv1a(slopes));

  // A different sun angle re-shades the relief.
  fv::DtedShadedRenderer se_sun;
  ASSERT_TRUE(se_sun.Open(path_).ok());
  se_sun.SetSunAzimuthElevation(135.0, 30.0);  // SE, low
  fv::PixelBuffer se_relief;
  ASSERT_TRUE(se_sun.Render(&se_relief).ok());
  EXPECT_NE(Fnv1a(relief), Fnv1a(se_relief)) << "sun direction ignored";
}

TEST_F(DtedShaded, ContourAndTimeShadingRunAndDiffer) {
  fv::DtedShadedRenderer base;
  ASSERT_TRUE(base.Open(path_).ok());
  fv::PixelBuffer plain;
  ASSERT_TRUE(base.Render(&plain).ok());

  fv::DtedShadedRenderer cont;
  ASSERT_TRUE(cont.Open(path_).ok());
  cont.SetContourLinesOn(true);
  cont.SetContourIntervalFeet(500.0);
  fv::PixelBuffer contoured;
  ASSERT_TRUE(cont.Render(&contoured).ok());
  EXPECT_NE(Fnv1a(plain), Fnv1a(contoured));

  // Time-of-day sun (noon over the cell) renders without SLAC.
  fv::DtedShadedRenderer tod;
  ASSERT_TRUE(tod.Open(path_).ok());
  tod.SetTimeShadingUtc(2021, 6, 21, 17.0);  // ~local noon at 81.5W
  fv::PixelBuffer sun;
  ASSERT_TRUE(tod.Render(&sun).ok());
  EXPECT_EQ(sun.Width(), 1200);
}

// --- elevation bands over real relief (regression, 2026-07-25) -------------
//
// CDtedReader's constructor seeds m_elev_breakpts with {2500..12500} but skips
// the feet->metres conversion its set_elevation_bands() setter applies, so the
// raw defaults behave as METRES. On Windows the options page always calls the
// setter; headless, the facade must supply FalconView's defaults itself or
// band 0 swallows all of CONUS and the map renders as one flat green hue.

// Within one band the engine varies only BRIGHTNESS (setup_color_table walks
// hsb2rgb across a single hue/sat pair), so HUE identifies the band and is
// invariant to the shade ramp. The six band colours are >25 degrees apart, so
// 15-degree buckets separate them cleanly while absorbing the 8-bit rounding
// noise a per-channel ratio would trip over. Near-black (deep shade / missing
// data) and the pure-blue sea-level entry carry no band information.
std::set<int> BandHueBuckets(const fv::PixelBuffer& b) {
  std::set<int> out;
  for (int y = 0; y < b.Height(); ++y) {
    const unsigned char* row = b.Row(y);
    for (int x = 0; x < b.Width(); ++x) {
      const int r = row[4 * x], g = row[4 * x + 1], bl = row[4 * x + 2];
      const int mx = std::max({r, g, bl}), mn = std::min({r, g, bl});
      if (mx < 40) continue;                            // black / deep shade
      if (mx == mn) continue;                           // grey: no hue
      if (r == 0 && g == 0 && bl == 255) continue;      // sea level
      if (r == 255 && g == 255 && bl == 255) continue;  // background
      const double d = mx - mn;
      double h;
      if (mx == r) h = 60.0 * std::fmod((g - bl) / d, 6.0);
      else if (mx == g) h = 60.0 * ((bl - r) / d + 2.0);
      else h = 60.0 * ((r - g) / d + 4.0);
      if (h < 0) h += 360.0;
      out.insert(static_cast<int>(h / 15.0));
    }
  }
  return out;
}

class DtedShadedRelief : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = ReliefCellPath();
    if (path_.empty()) GTEST_SKIP() << "TestData relief DTED cell not present";
  }
  std::string path_;
};

TEST_F(DtedShadedRelief, DefaultBandsAreFalconViewsNotTheReadersRawDefaults) {
  // The defaults the facade applies must be identical to asking for
  // FalconView's own {2500,5000,7500,10000,12500} FEET. Before the fix the
  // reader's unconverted constructor values were used instead and this cell
  // rendered ~3x fewer colours, all in band 0.
  fv::DtedShadedRenderer implicit;
  ASSERT_TRUE(implicit.Open(path_).ok());
  fv::PixelBuffer a;
  ASSERT_TRUE(implicit.Render(&a).ok());

  fv::DtedShadedRenderer explicit_ft;
  ASSERT_TRUE(explicit_ft.Open(path_).ok());
  explicit_ft.SetElevationBands({2500, 5000, 7500, 10000, 12500});
  fv::PixelBuffer b;
  ASSERT_TRUE(explicit_ft.Render(&b).ok());

  EXPECT_EQ(Fnv1a(a), Fnv1a(b))
      << "default render != FalconView's default elevation bands in feet";

  // Bands treated as metres would put this whole cell (max ~1700 m) in band 0.
  // With the feet defaults it spans green/olive/yellow.
  EXPECT_GE(BandHueBuckets(a).size(), 2u)
      << "only one elevation band painted over 300-1700 m of relief";
}

TEST_F(DtedShadedRelief, BandBreakpointsMoveTheColourRamp) {
  // Bands an order of magnitude too high collapse the ramp to one hue; that is
  // precisely the failure the metres/feet mix-up produced.
  fv::DtedShadedRenderer high;
  ASSERT_TRUE(high.Open(path_).ok());
  high.SetElevationBands({25000, 50000, 75000, 100000, 125000});
  fv::PixelBuffer coarse;
  ASSERT_TRUE(high.Render(&coarse).ok());
  EXPECT_EQ(BandHueBuckets(coarse).size(), 1u)
      << "unreachable breakpoints should leave everything in band 0";

  fv::DtedShadedRenderer low;
  ASSERT_TRUE(low.Open(path_).ok());
  low.SetElevationBands({1000, 2000, 3000, 4000, 5000});
  fv::PixelBuffer fine;
  ASSERT_TRUE(low.Render(&fine).ok());
  EXPECT_GT(BandHueBuckets(fine).size(), BandHueBuckets(coarse).size());
}

TEST_F(DtedShadedRelief, ShadingVariesAcrossSlopesWithinABand) {
  // The hillshade itself: a NW sun must leave NE- and SW-facing slopes at
  // different brightness. Flat ground sits at dot(normal, light) = 0.577, so a
  // working shader spreads brightness both above and below that.
  fv::DtedShadedRenderer r;
  ASSERT_TRUE(r.Open(path_).ok());
  fv::PixelBuffer img;
  ASSERT_TRUE(r.Render(&img).ok());

  int min_lum = 255, max_lum = 0;
  for (int y = 0; y < img.Height(); y += 7) {
    const unsigned char* row = img.Row(y);
    for (int x = 0; x < img.Width(); ++x) {
      const int r8 = row[4 * x], g8 = row[4 * x + 1], b8 = row[4 * x + 2];
      if (r8 == 0 && g8 == 0 && b8 == 255) continue;  // sea level
      const int lum = (r8 * 30 + g8 * 59 + b8 * 11) / 100;
      min_lum = std::min(min_lum, lum);
      max_lum = std::max(max_lum, lum);
    }
  }
  EXPECT_GT(max_lum - min_lum, 60)
      << "shaded relief is nearly uniform (min " << min_lum << ", max "
      << max_lum << ")";
}

/// The posts of a DTED cell, top-down (row 0 = north), as float metres.
/// Reads the UHL counts and the signed-magnitude big-endian data records.
bool ReadPosts(const std::string& path, int* nlon, int* nlat, double* lon_sec,
               double* lat_sec, std::vector<float>* posts) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::vector<unsigned char> bytes;
  unsigned char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
  std::fclose(f);
  auto num = [&](size_t at, size_t len) {
    return std::stoi(std::string(bytes.begin() + at, bytes.begin() + at + len));
  };
  *lon_sec = num(20, 4) / 10.0;
  *lat_sec = num(24, 4) / 10.0;
  *nlon = num(47, 4);
  *nlat = num(51, 4);
  posts->assign(static_cast<size_t>(*nlon) * *nlat, 0.0f);
  for (int c = 0; c < *nlon; ++c) {
    const size_t col = 80 + 648 + 2700 + static_cast<size_t>(c) * (12 + 2 * *nlat) + 8;
    for (int r = 0; r < *nlat; ++r) {  // r = 0 is the south post
      const unsigned char hi = bytes[col + 2 * r], lo = bytes[col + 2 * r + 1];
      int v = ((hi & 0x7f) << 8) | lo;
      if (hi & 0x80) v = -v;
      (*posts)[static_cast<size_t>(*nlat - 1 - r) * *nlon + c] = static_cast<float>(v);
    }
  }
  return true;
}

TEST_F(DtedShadedRelief, ShadeGridMatchesTheCellRender) {
  // The cell's own posts through ShadeGrid reproduce Render, bands and all.
  fv::DtedShadedRenderer r;
  ASSERT_TRUE(r.Open(path_).ok());
  r.SetElevationBands({1000, 2000, 3000, 4000, 5000});
  fv::PixelBuffer cell;
  ASSERT_TRUE(r.Render(&cell).ok());

  int nlon = 0, nlat = 0;
  double lon_sec = 0, lat_sec = 0;
  std::vector<float> posts;
  ASSERT_TRUE(ReadPosts(path_, &nlon, &nlat, &lon_sec, &lat_sec, &posts));
  ASSERT_EQ(nlon - 1, cell.Width());
  ASSERT_EQ(nlat - 1, cell.Height());

  const double kPi = 4.0 * std::atan(1.0), kR = 6378137.0;
  fv::GeoPoint nw;
  ASSERT_TRUE(r.PixelToGeo(0, -1, &nw));
  const double mid_lat = nw.lat - (nlat - 1) * (lat_sec / 3600.0) / 2;
  const double m_lat = lat_sec / 3600.0 * kR * kPi / 180.0;
  const double m_lon = lon_sec / 3600.0 * kR * std::cos(mid_lat * kPi / 180.0) * kPi / 180;
  fv::PixelBuffer grid;
  ASSERT_TRUE(r.ShadeGrid(posts.data(), nlon - 1, nlat - 1, m_lat, m_lon, {mid_lat, nw.lon},
                          &grid).ok());
  ASSERT_EQ(grid.Width(), cell.Width());
  ASSERT_EQ(grid.Height(), cell.Height());

  size_t same = 0, total = 0;
  for (int y = 0; y < cell.Height(); ++y) {
    for (int x = 0; x < cell.Width(); ++x, ++total) {
      if (std::memcmp(cell.Row(y) + 4 * x, grid.Row(y) + 4 * x, 4) == 0) ++same;
    }
  }
  EXPECT_EQ(same, total) << (total - same) << " pixels differ";
}

TEST(DtedShadedGrid, VoidPostsAreTransparent) {
  fv::DtedShadedRenderer r;
  const float nan = std::nanf("");
  // 2x2 pixels from 3x3 posts; the north-west post is void.
  const std::vector<float> posts = {nan, 300, 310, 305, 300, 320, 310, 315, 330};
  fv::PixelBuffer out;
  ASSERT_TRUE(r.ShadeGrid(posts.data(), 2, 2, 30.0, 25.0, {34.0, -84.0}, &out).ok());
  EXPECT_EQ(out.Row(0)[3], 0);
  EXPECT_EQ(out.Row(0)[7], 255);
  EXPECT_EQ(out.Row(1)[3], 255);
  EXPECT_EQ(out.Row(1)[7], 255);
}

}  // namespace
