// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// MBTiles container tests (OSM phase O1), over TestData's real us-south
// pyramid (Tilemaker, OpenMapTiles schema, z0-14, 798,627 tiles).
//
// Counts and extents here are pinned against the FILE, read out with the
// sqlite3 CLI rather than through this reader. If the sample data is ever
// replaced the pins move, which is the fourth time this port has learned that
// lesson — so structural claims (a pyramid is dense at the top, TMS flips,
// bounds derive from tiles) are separated from the file-specific numbers.

#include "fv_mbtiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string MbtilesPath() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/OSM/mbtiles/us-south.mbtiles";
  return fs::is_regular_file(p) ? p : std::string();
}

}  // namespace

#define SKIP_WITHOUT_MBTILES()               \
  const std::string mb_path = MbtilesPath(); \
  if (mb_path.empty()) GTEST_SKIP() << "no OSM mbtiles test data"

#define OPEN_FILE(var)                         \
  fv::MbtilesFile var;                         \
  {                                            \
    const fv::Status s = var.Open(mb_path);    \
    ASSERT_TRUE(s.ok()) << s.message;          \
  }

TEST(Mbtiles, MetadataReadsAsTheFileWroteIt) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  EXPECT_TRUE(file.IsOpen());
  EXPECT_EQ(file.name(), "Tilemaker to OpenMapTiles schema");
  EXPECT_EQ(file.format(), "pbf");
  EXPECT_EQ(file.type(), "baselayer");
  EXPECT_EQ(file.version(), "3.0");
  EXPECT_EQ(file.min_zoom(), 0);
  EXPECT_EQ(file.max_zoom(), 14);
  EXPECT_EQ(file.center_zoom(), 7);
  // An unmodelled key still comes back.
  ASSERT_NE(file.Metadata("description"), nullptr);
  EXPECT_EQ(file.Metadata("nonesuch"), nullptr);
}

TEST(Mbtiles, TheLayerInventoryComesFromTheJsonMetadata) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  const std::vector<fv::MbtilesLayerInfo>& layers = file.layers();
  ASSERT_EQ(layers.size(), 16u);
  // Order is the file's, and it is what a FeatureRef::layer index means.
  EXPECT_EQ(layers[0].id, "place");
  EXPECT_EQ(layers[1].id, "boundary");
  EXPECT_EQ(layers.back().id, "mountain_peak");

  const fv::MbtilesLayerInfo* transportation = nullptr;
  for (const auto& l : layers)
    if (l.id == "transportation") transportation = &l;
  ASSERT_NE(transportation, nullptr);
  // Per-layer zoom range: the reason to read the inventory rather than scrape
  // layer names off whichever tile was loaded first.
  EXPECT_EQ(transportation->minzoom, 4);
  EXPECT_EQ(transportation->maxzoom, 14);
  EXPECT_FALSE(transportation->fields.empty());

  // A layer that only exists deep in the pyramid.
  for (const auto& l : layers) {
    if (l.id == "building") EXPECT_EQ(l.minzoom, 13);
    if (l.id == "housenumber") EXPECT_EQ(l.minzoom, 14);
  }
  EXPECT_TRUE(file.warnings().empty())
      << (file.warnings().empty() ? "" : file.warnings()[0]);
}

TEST(Mbtiles, TheDeclaredBoundsAreWrongAndThePyramidIsAuthoritative) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  // THE FINDING (2026-08-04): this file's `bounds` metadata declares an east
  // edge of exactly 0.000000 degrees — the Greenwich meridian, ~1000 km past
  // the westernmost point of Africa and nowhere near its easternmost tile.
  // The other three edges are right. A reader that trusted this would put
  // three quarters of the Atlantic inside the data's coverage.
  ASSERT_TRUE(file.has_declared_bounds());
  const fv::GeoRect declared = file.declared_bounds();
  EXPECT_DOUBLE_EQ(declared.ur.lon, 0.0);
  EXPECT_NEAR(declared.ll.lon, -106.6494, 1e-4);

  const fv::GeoRect derived = file.Bounds();
  // Derived from the z14 tile extent: columns 3338..4792, rows 9319..10219.
  EXPECT_NEAR(derived.ll.lon, -106.6552734375, 1e-9);
  EXPECT_NEAR(derived.ur.lon, -74.68505859375, 1e-9);
  EXPECT_NEAR(derived.ur.lat, 40.6473035625225, 1e-9);
  EXPECT_NEAR(derived.ll.lat, 24.0263966660173, 1e-9);

  EXPECT_TRUE(file.declared_bounds_disagree());
  // And the disagreement is the longitude, not the latitude: the declared
  // north/south edges are within a tile of the pyramid's.
  EXPECT_NEAR(declared.ll.lat, derived.ll.lat, 0.01);
  EXPECT_NEAR(declared.ur.lat, derived.ur.lat, 0.01);
}

TEST(Mbtiles, TilesComeBackByXyzWithTheTmsFlipHandled) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  // z0 has exactly one tile, and it is stored at TMS row 0.
  std::string blob;
  ASSERT_TRUE(file.ReadTile(fv::webmerc::TileId{0, 0, 0}, &blob).ok());
  EXPECT_EQ(blob.size(), 140u);
  ASSERT_GE(blob.size(), 2u);
  EXPECT_EQ(static_cast<unsigned char>(blob[0]), 0x1fu);  // gzip framing
  EXPECT_EQ(static_cast<unsigned char>(blob[1]), 0x8bu);

  // Downtown Atlanta at z14: XYZ y 6558 is TMS row 16383-6558 = 9825.
  ASSERT_TRUE(file.ReadTile(fv::webmerc::TileId{14, 4351, 6558}, &blob).ok());
  EXPECT_GT(blob.size(), 100000u);
  EXPECT_TRUE(file.HasTile(fv::webmerc::TileId{14, 4351, 6558}));
}

TEST(Mbtiles, AMissingTileIsNotFoundAndAnImpossibleOneIsInvalid) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  std::string blob;
  // Inside the grid, outside this pyramid's data: the normal answer when
  // panning off the edge of the coverage, and NOT an I/O error.
  fv::Status s = file.ReadTile(fv::webmerc::TileId{14, 100, 100}, &blob);
  EXPECT_EQ(s.code, fv::kNotFound) << s.message;
  EXPECT_FALSE(file.HasTile(fv::webmerc::TileId{14, 100, 100}));

  // Off the grid entirely: a caller bug, reported as one.
  s = file.ReadTile(fv::webmerc::TileId{2, 4, 0}, &blob);
  EXPECT_EQ(s.code, fv::kInvalidArg) << s.message;
  s = file.ReadTile(fv::webmerc::TileId{2, 0, -1}, &blob);
  EXPECT_EQ(s.code, fv::kInvalidArg) << s.message;
  s = file.ReadTile(fv::webmerc::TileId{99, 0, 0}, &blob);
  EXPECT_EQ(s.code, fv::kInvalidArg) << s.message;
}

TEST(Mbtiles, ZoomExtentsReportTheXyzBoxAndTheTileCount) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  fv::MbtilesFile::ZoomExtent e;
  ASSERT_TRUE(file.ZoomExtentOf(14, &e).ok());
  EXPECT_EQ(e.count, 594419);
  EXPECT_EQ(e.min_x, 3338);
  EXPECT_EQ(e.max_x, 4792);
  // Stored TMS rows are 9319..10219; flipped, the northernmost is the
  // SMALLEST XYZ y. Getting this backwards is the classic MBTiles bug.
  EXPECT_EQ(e.min_y, fv::webmerc::TmsRow(14, 10219));
  EXPECT_EQ(e.max_y, fv::webmerc::TmsRow(14, 9319));
  EXPECT_EQ(e.min_y, 6164);
  EXPECT_EQ(e.max_y, 7064);
  EXPECT_FALSE(e.empty());

  ASSERT_TRUE(file.ZoomExtentOf(0, &e).ok());
  EXPECT_EQ(e.count, 1);
  EXPECT_EQ(e.min_x, 0);
  EXPECT_EQ(e.max_x, 0);

  // A zoom the file does not have.
  ASSERT_TRUE(file.ZoomExtentOf(20, &e).ok());
  EXPECT_TRUE(e.empty());

  std::vector<int> zooms;
  ASSERT_TRUE(file.ZoomLevels(&zooms).ok());
  ASSERT_EQ(zooms.size(), 15u);
  EXPECT_EQ(zooms.front(), 0);
  EXPECT_EQ(zooms.back(), 14);
  EXPECT_TRUE(std::is_sorted(zooms.begin(), zooms.end()));
}

TEST(Mbtiles, ThePyramidIsSparseAndThatIsNormal) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);
  fv::MbtilesFile::ZoomExtent e;
  ASSERT_TRUE(file.ZoomExtentOf(14, &e).ok());
  const int64_t box =
      int64_t(e.max_x - e.min_x + 1) * int64_t(e.max_y - e.min_y + 1);
  // A cutter writes no tile for a cell with nothing in it, so the stored
  // count is well under the bounding box's area. Anything that assumed a
  // dense pyramid would be reading 40% air.
  EXPECT_LT(e.count, box);
  EXPECT_GT(e.count, box / 4);
}

TEST(Mbtiles, RefusingWhatIsNotAnMbtilesFile) {
  SKIP_WITHOUT_MBTILES();

  fv::MbtilesFile file;
  fv::Status s = file.Open(mb_path + ".does-not-exist");
  EXPECT_EQ(s.code, fv::kIoError) << s.message;
  EXPECT_FALSE(file.IsOpen());

  // A real file that is not SQLite. (An MBTiles reader that used
  // sqlite3_open() rather than OpenReadOnly would CREATE this path instead.)
  const fs::path scratch =
      fs::temp_directory_path() / "fv_osm_not_an_mbtiles.bin";
  {
    std::ofstream out(scratch, std::ios::binary);
    out << "this is not a database";
  }
  s = file.Open(scratch.string());
  EXPECT_FALSE(s.ok()) << "opened a non-database";
  fs::remove(scratch);

  const fs::path absent = fs::temp_directory_path() / "fv_osm_absent.mbtiles";
  fs::remove(absent);
  s = file.Open(absent.string());
  EXPECT_FALSE(s.ok());
  EXPECT_FALSE(fs::exists(absent)) << "a failed open created the file";
}

TEST(Mbtiles, CloseForgetsEverything) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);
  ASSERT_FALSE(file.layers().empty());
  file.Close();
  EXPECT_FALSE(file.IsOpen());
  EXPECT_TRUE(file.layers().empty());
  std::string blob;
  EXPECT_FALSE(file.ReadTile(fv::webmerc::TileId{0, 0, 0}, &blob).ok());
}
