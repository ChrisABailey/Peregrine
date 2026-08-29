// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::OsmFrameEnumerator tests (OSM phase O3) — the catalog's view of a tile
// pyramid. Short, because the enumerator is: one file is one row. What is
// worth pinning is the three decisions that row encodes (the header explains
// each): the key is the file STEM, the scale is 0 because a pyramid is a scale
// RANGE, and the bounds come from the tile index rather than the file's own
// (untrustworthy) `bounds` metadata.

#include "fv_osm_format.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "fvkit/formats/registry.h"

namespace fs = std::filesystem;

namespace {

std::string MbtilesDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/mbtiles";
  return fs::is_directory(p) ? p : std::string();
}

}  // namespace

#define SKIP_WITHOUT_MBTILES()            \
  const std::string dir = MbtilesDir();   \
  if (dir.empty()) GTEST_SKIP() << "no OSM mbtiles test data"

TEST(OsmFormat, OneFileIsOneFrameAndTheKeyIsTheStem) {
  SKIP_WITHOUT_MBTILES();
  fv::OsmFrameEnumerator e;
  const fv::Status s = e.Begin(dir);
  ASSERT_TRUE(s.ok()) << s.message;

  std::vector<fv::FrameInfo> frames;
  fv::FrameInfo f;
  while (e.Next(&f)) frames.push_back(f);
  ASSERT_EQ(frames.size(), 1u);

  EXPECT_EQ(frames[0].series_key, "us-south");
  EXPECT_EQ(fs::path(frames[0].path).filename().string(), "us-south.mbtiles");
  EXPECT_GT(frames[0].size_bytes, 0);

  // Scale 0 = "not applicable", the same answer DTED gives — and it is what
  // makes PythonView frame the pyramid instead of jumping to a nominal scale,
  // and keeps it off the PageUp/PageDown ladder.
  EXPECT_EQ(frames[0].scale, 0.0);

  // Bounds are the DERIVED coverage: Atlanta is inside, and the box is a
  // region rather than the whole world. Its east edge is the Greenwich
  // meridian as of the 2026-08-17 re-cut (-74.685 before it, when the ocean
  // merge stopped short of the Atlantic's far side), so the standing claim is
  // the LATITUDE band — the pyramid is the US South's, not a global one.
  EXPECT_TRUE(frames[0].bounds.Contains(fv::GeoPoint{33.749, -84.388}));
  EXPECT_LE(frames[0].bounds.ur.lon, 0.0);
  EXPECT_GT(frames[0].bounds.ll.lon, -110.0);
  EXPECT_GT(frames[0].bounds.ll.lat, 20.0);
  EXPECT_LT(frames[0].bounds.ur.lat, 45.0);
}

TEST(OsmFormat, TheFileItselfCanBeTheScanRoot) {
  SKIP_WITHOUT_MBTILES();
  fv::OsmFrameEnumerator e;
  ASSERT_TRUE(e.Begin(dir + "/us-south.mbtiles").ok());
  fv::FrameInfo f;
  ASSERT_TRUE(e.Next(&f));
  EXPECT_EQ(f.series_key, "us-south");
  EXPECT_FALSE(e.Next(&f));
}

TEST(OsmFormat, ADirectoryWithNoPyramidsIsUnsupportedNotEmpty) {
  const fs::path tmp =
      fs::temp_directory_path() / "fv_osm_format_empty_dir_test";
  fs::create_directories(tmp);
  fv::OsmFrameEnumerator e;
  // A real directory that holds no .mbtiles: the scan must say so rather than
  // report success with zero rows, which a caller reads as "no data here".
  const fv::Status s = e.Begin(tmp.string());
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code, fv::kUnsupported);
  fs::remove_all(tmp);
}

TEST(OsmFormat, AMissingDirectoryIsNotFound) {
  fv::OsmFrameEnumerator e;
  const fv::Status s = e.Begin("/definitely/not/here");
  EXPECT_EQ(s.code, fv::kNotFound);
}

TEST(OsmFormat, RegistrationIsIdempotentAndAddsOnlyTheEnumerator) {
  fv::RegisterOsmFormat();
  fv::RegisterOsmFormat();
  const fv::FormatFactories* ff = fv::FindFormat("osm");
  ASSERT_NE(ff, nullptr);
  EXPECT_TRUE(static_cast<bool>(ff->make_enumerator));
  // OSM is vector data: it is drawn through OsmVectorSource + OsmStyleEngine,
  // not through MapEngine's raster compositor, exactly like vpf and enc.
  EXPECT_FALSE(static_cast<bool>(ff->make_raster_source));

  const std::vector<std::string> keys = fv::RegisteredFormatKeys();
  EXPECT_NE(std::find(keys.begin(), keys.end(), "osm"), keys.end());
}
