// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MBTiles container tests (OSM phase O1), over TestData's real us-south
// pyramid (Tilemaker, OpenMapTiles schema, z0-14, 4,872,934 tiles as re-cut
// 2026-08-17 — 798,627 at O1, 1,246,885 after the 2026-08-11 ocean merge).
//
// Counts and extents here are pinned against the FILE, read out with the
// sqlite3 CLI rather than through this reader. If the sample data is ever
// replaced the pins move, which is the fourth time this port has learned that
// lesson — so structural claims (a pyramid is dense at the top, TMS flips,
// bounds derive from tiles) are separated from the file-specific numbers.
// The 2026-08-17 re-cut moved every longitude pin and no latitude pin: its
// ocean coverage now runs east to the Greenwich meridian (z14 columns
// 3338..8191 where they were 3338..4792), while the north/south edges and the
// stored TMS row range are byte-identical to the previous cut.

#include "fv_mbtiles.h"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
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
  ASSERT_EQ(layers.size(), 17u);  // 16 before the 2026-08-17 re-cut added
                                  // man_made
  // Order is the file's, and it is what a FeatureRef::layer index means.
  EXPECT_EQ(layers[0].id, "place");
  EXPECT_EQ(layers[1].id, "boundary");
  EXPECT_EQ(layers.back().id, "man_made");

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

TEST(Mbtiles, BoundsAreDerivedFromTheTilesNotBelieved) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  // THE FINDING (2026-08-04), kept because it is why this reader derives:
  // the O1 cut of this file declared an east edge of exactly 0.000000 degrees
  // — the Greenwich meridian, ~1000 km past the westernmost point of Africa
  // and nowhere near its easternmost tile. A reader that trusted it would have
  // put three quarters of the Atlantic inside the data's coverage.
  //
  // THE SEQUEL (2026-08-17): the re-cut declares that same 0.000000 and is now
  // CORRECT — its ocean tiles genuinely reach the meridian (z14 column 8191's
  // east edge is lon 0 exactly). One number, wrong in one file and right in
  // the next, with nothing in the metadata to tell them apart. Deriving is the
  // only way to be right about both, so this test now pins the AGREEING
  // direction of the flag and MbtilesDeclaredBounds.ADeclaredBoxWiderThanThe
  // PyramidIsReportedAsDisagreeing pins the other one over a synthetic file.
  ASSERT_TRUE(file.has_declared_bounds());
  const fv::GeoRect declared = file.declared_bounds();
  EXPECT_DOUBLE_EQ(declared.ur.lon, 0.0);
  EXPECT_NEAR(declared.ll.lon, -106.6494, 1e-4);

  const fv::GeoRect derived = file.Bounds();
  // Derived from the z14 tile extent: columns 3338..8191, rows 9319..10219.
  EXPECT_NEAR(derived.ll.lon, -106.6552734375, 1e-9);
  EXPECT_NEAR(derived.ur.lon, 0.0, 1e-9);
  EXPECT_NEAR(derived.ur.lat, 40.6473035625225, 1e-9);
  EXPECT_NEAR(derived.ll.lat, 24.0263966660173, 1e-9);

  EXPECT_FALSE(file.declared_bounds_disagree());
  // All four edges are within a tile of the pyramid's, which is what makes the
  // agreement real rather than a slack tolerance on the one edge that moved.
  EXPECT_NEAR(declared.ll.lat, derived.ll.lat, 0.01);
  EXPECT_NEAR(declared.ur.lat, derived.ur.lat, 0.01);
  EXPECT_NEAR(declared.ll.lon, derived.ll.lon, 0.01);
  EXPECT_NEAR(declared.ur.lon, derived.ur.lon, 0.01);
}

// The disagreeing direction of declared_bounds_disagree(), over a file this
// test writes. It used to be covered by the real us-south pyramid, until the
// 2026-08-17 re-cut made that file's declared bounds honest — and a flag whose
// only coverage is "whichever way today's sample data happens to fall" is not
// covered at all. Four tiny tiles are enough: the flag compares a declared box
// against the pyramid's, and neither side cares what is inside a tile.
TEST(MbtilesDeclaredBounds, ADeclaredBoxWiderThanThePyramidIsReportedAsDisagreeing) {
  const fs::path path =
      fs::temp_directory_path() / "fv_osm_declared_bounds.mbtiles";
  fs::remove(path);

  // z6 columns 13..14, TMS rows 36..37 — a four-tile block over the Gulf, so
  // one tile of slack at z6 (360/64 = 5.625 deg) is small against the lie.
  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open(path.string().c_str(), &db), SQLITE_OK);
  const char* kSchema =
      "CREATE TABLE metadata(name TEXT, value TEXT);"
      "CREATE TABLE tiles(zoom_level INTEGER, tile_column INTEGER,"
      "                   tile_row INTEGER, tile_data BLOB);"
      "INSERT INTO metadata VALUES('name','synthetic'),('format','pbf'),"
      "  ('minzoom','6'),('maxzoom','6'),"
      // The lie: an east edge at +40 degrees, ~100 tiles past the last column.
      "  ('bounds','-106.0,24.0,40.0,32.0');"
      "INSERT INTO tiles VALUES(6,13,36,x'00'),(6,14,36,x'00'),"
      "                        (6,13,37,x'00'),(6,14,37,x'00');";
  char* err = nullptr;
  const int rc = sqlite3_exec(db, kSchema, nullptr, nullptr, &err);
  const std::string err_text = err != nullptr ? err : "";
  sqlite3_free(err);
  sqlite3_close(db);
  ASSERT_EQ(rc, SQLITE_OK) << err_text;

  fv::MbtilesFile file;
  const fv::Status s = file.Open(path.string());
  ASSERT_TRUE(s.ok()) << s.message;

  ASSERT_TRUE(file.has_declared_bounds());
  EXPECT_DOUBLE_EQ(file.declared_bounds().ur.lon, 40.0);

  const fv::GeoRect derived = file.Bounds();
  // Columns 13..14 of 64: west edge of 13, east edge of 15.
  EXPECT_NEAR(derived.ll.lon, -106.875, 1e-9);
  EXPECT_NEAR(derived.ur.lon, -95.625, 1e-9);
  EXPECT_TRUE(file.declared_bounds_disagree());

  // And the flag is about the BOX, not about having declared bounds at all:
  // the same pyramid with a box that only rounds out to tile edges agrees.
  fs::remove(path);
  ASSERT_EQ(sqlite3_open(path.string().c_str(), &db), SQLITE_OK);
  const std::string honest =
      std::string(kSchema).replace(
          std::string(kSchema).find("-106.0,24.0,40.0,32.0"),
          std::strlen("-106.0,24.0,40.0,32.0"), "-106.5,24.5,-95.5,31.9");
  ASSERT_EQ(sqlite3_exec(db, honest.c_str(), nullptr, nullptr, nullptr),
            SQLITE_OK);
  sqlite3_close(db);
  fv::MbtilesFile agreeing;
  ASSERT_TRUE(agreeing.Open(path.string()).ok());
  EXPECT_TRUE(agreeing.has_declared_bounds());
  EXPECT_FALSE(agreeing.declared_bounds_disagree());

  fs::remove(path);
}

TEST(Mbtiles, TilesComeBackByXyzWithTheTmsFlipHandled) {
  SKIP_WITHOUT_MBTILES();
  OPEN_FILE(file);

  // z0 has exactly one tile, and it is stored at TMS row 0.
  std::string blob;
  ASSERT_TRUE(file.ReadTile(fv::webmerc::TileId{0, 0, 0}, &blob).ok());
  EXPECT_EQ(blob.size(), 5124u);  // 140 before the 2026-08-17 re-cut: the z0
                                  // tile now carries the merged ocean polygon
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
  EXPECT_EQ(e.count, 3647283);  // 594,419 before the 2026-08-17 re-cut
  EXPECT_EQ(e.min_x, 3338);
  EXPECT_EQ(e.max_x, 8191);  // 4792 before it; 8191's east edge is lon 0
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
