// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::S57Cell tests (ENC phase E1) — real NOAA data.
//
// Data: TestData/enc, four Charleston SC harbour cells (usage band 5) tiling a
// 2x2 grid of 0.075-degree squares over Charleston Harbor and the Ashley River,
// each with its own exchange-set CATALOG.031.
//
// Three kinds of assertion here, in descending order of how much they prove:
//
//  1. Cross-checks against the PRODUCER's own numbers, which no bug of ours can
//     move: DSSI declares how many isolated nodes / connected nodes / edges and
//     feature records the cell contains, and each CATALOG.031 declares the
//     cell's bounding box. A parse that drifts fails these.
//  2. Structural invariants: every area ring closes, no vertex lands outside
//     Earth (or outside the declared cell box), soundings carry depths.
//  3. Regression pins: exact counts and coordinates for named features. These
//     are derived from the data, so they stay valid as long as these cells do —
//     new cells added alongside must not change them (see the 2026-07-23
//     TestData lesson: counts that sweep a directory get derived, not bumped).

#include "fv_s57.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

std::string CellPath(const std::string& stem) {
  const std::string root = EncRoot();
  if (root.empty()) return {};
  const std::string p = root + "/" + stem + "/" + stem + ".000";
  return fs::is_regular_file(p) ? p : std::string();
}

#define SKIP_WITHOUT_ENC()                  \
  const std::string enc_root = EncRoot();   \
  if (enc_root.empty()) GTEST_SKIP() << "no ENC test data"

#define OPEN_CELL(var, stem)                                       \
  const std::string var##_path = CellPath(stem);                   \
  if (var##_path.empty()) GTEST_SKIP() << "no " stem " test data"; \
  fv::S57Cell var;                                                 \
  ASSERT_TRUE(var.Open(var##_path).ok()) << var.Open(var##_path).message

// The four cells, and the box each one's CATALOG.031 declares.
struct CellSpec {
  const char* stem;
  double south, west, north, east;
};
constexpr CellSpec kCells[] = {
    {"US5CHSDC", 32.700, -80.025, 32.775, -79.950},
    {"US5CHSDD", 32.700, -79.950, 32.775, -79.875},
    {"US5CHSEC", 32.775, -80.025, 32.850, -79.950},
    {"US5CHSED", 32.775, -79.950, 32.850, -79.875},
};

int CountPrimitive(const fv::S57Cell& cell, fv::S57Primitive prim) {
  int n = 0;
  for (const fv::S57Feature& f : cell.features())
    if (f.primitive == prim) ++n;
  return n;
}

size_t VertexCount(const fv::S57Cell& cell) {
  size_t n = 0;
  for (const fv::S57Feature& f : cell.features())
    for (const auto& part : f.geometry.parts) n += part.size();
  return n;
}

const fv::S57Feature* ById(const fv::S57Cell& cell, int32_t rcid) {
  for (const fv::S57Feature& f : cell.features())
    if (f.record_id == rcid) return &f;
  return nullptr;
}

// --- dataset metadata -------------------------------------------------------

TEST(S57Cell, ReadsDatasetIdentification) {
  OPEN_CELL(cell, "US5CHSDC");
  const fv::S57DatasetInfo& i = cell.info();
  EXPECT_EQ("US5CHSDC.000", i.name);
  EXPECT_EQ("2", i.edition);
  EXPECT_EQ("0", i.update_number) << "a .000 file is update 0 by definition";
  EXPECT_EQ("03.1", i.standard_edition) << "S-57 edition 3.1";
  EXPECT_EQ(550, i.producer_agency) << "550 = NOAA";
  EXPECT_EQ(5, i.intended_usage) << "usage band 5 = harbour";
  EXPECT_EQ("Produced by NOAA", i.comment);
  EXPECT_EQ("20250228", i.issue_date);

  EXPECT_EQ(2, i.data_structure) << "ENC is chain-node topology";
  EXPECT_EQ(12000, i.compilation_scale);
  EXPECT_DOUBLE_EQ(1e7, i.coordinate_multiplier);
  EXPECT_DOUBLE_EQ(10.0, i.sounding_multiplier);
  EXPECT_EQ(2, i.horizontal_datum) << "2 = WGS 84, so no datum shift is needed";
  EXPECT_EQ(1, i.depth_units) << "1 = metres";
  EXPECT_EQ(1, i.coordinate_units) << "1 = latitude/longitude";
  EXPECT_EQ(1, i.national_lexical_level)
      << "level 2 would mean UCS-2 national text, which E1 does not decode";
}

TEST(S57Cell, UsageBandInTheFileNameMatchesTheOneInTheData) {
  for (const CellSpec& spec : kCells) {
    const std::string path = CellPath(spec.stem);
    if (path.empty()) continue;
    fv::S57Cell cell;
    ASSERT_TRUE(cell.Open(path).ok()) << spec.stem;
    const fv::S57CellName name = fv::ParseCellName(path);
    ASSERT_TRUE(name.valid) << spec.stem;
    EXPECT_EQ(name.usage_band, cell.info().intended_usage) << spec.stem;
  }
}

// --- the producer's own counts ----------------------------------------------

TEST(S57Cell, ParsedRecordCountsMatchTheDeclaredOnes) {
  SKIP_WITHOUT_ENC();
  int cells_checked = 0;
  for (const CellSpec& spec : kCells) {
    const std::string path = CellPath(spec.stem);
    if (path.empty()) continue;
    ++cells_checked;
    fv::S57Cell cell;
    ASSERT_TRUE(cell.Open(path).ok()) << spec.stem;
    const fv::S57DatasetInfo& i = cell.info();
    const fv::S57ParseCounts& c = cell.counts();
    EXPECT_EQ(i.declared_isolated_nodes, c.isolated_nodes) << spec.stem;
    EXPECT_EQ(i.declared_connected_nodes, c.connected_nodes) << spec.stem;
    EXPECT_EQ(i.declared_edges, c.edges) << spec.stem;
    EXPECT_EQ(i.declared_faces, c.faces) << spec.stem;
    // The declared feature-record total, across all four categories.
    EXPECT_EQ(i.declared_meta_records + i.declared_cartographic_records +
                  i.declared_geo_records + i.declared_collection_records,
              c.feature_records)
        << spec.stem;
    EXPECT_EQ(static_cast<size_t>(c.feature_records), cell.features().size())
        << spec.stem;
    // Collection objects are the primitive-less features (S-57 Appendix A's
    // C_AGGR/C_ASSO/C_STAC); NOLR pins that reading in every delivered cell.
    EXPECT_EQ(i.declared_collection_records, c.collection_records) << spec.stem;
  }
  EXPECT_EQ(4, cells_checked) << "all four Charleston cells should be present";
}

TEST(S57Cell, GeometryStaysInsideTheBoxTheCatalogueDeclares) {
  for (const CellSpec& spec : kCells) {
    const std::string path = CellPath(spec.stem);
    if (path.empty()) continue;
    fv::S57Cell cell;
    ASSERT_TRUE(cell.Open(path).ok()) << spec.stem;
    // A cell's data limit is its declared box; vertices may sit on it, so the
    // tolerance only absorbs COMF rounding (1e-7 degree).
    const double eps = 1e-6;
    size_t checked = 0;
    for (const fv::S57Feature& f : cell.features())
      for (const auto& part : f.geometry.parts)
        for (const fv::S57Vertex& v : part) {
          ASSERT_GE(v.lat, spec.south - eps) << spec.stem;
          ASSERT_LE(v.lat, spec.north + eps) << spec.stem;
          ASSERT_GE(v.lon, spec.west - eps) << spec.stem;
          ASSERT_LE(v.lon, spec.east + eps) << spec.stem;
          ++checked;
        }
    EXPECT_GT(checked, 10000u) << spec.stem;
    const fv::GeoRect b = cell.bounds();
    EXPECT_NEAR(spec.south, b.ll.lat, 0.02) << spec.stem;
    EXPECT_NEAR(spec.west, b.ll.lon, 0.02) << spec.stem;
    EXPECT_NEAR(spec.north, b.ur.lat, 0.02) << spec.stem;
    EXPECT_NEAR(spec.east, b.ur.lon, 0.02) << spec.stem;
  }
}

// --- structure --------------------------------------------------------------

TEST(S57Cell, EveryAreaRingCloses) {
  SKIP_WITHOUT_ENC();
  for (const CellSpec& spec : kCells) {
    const std::string path = CellPath(spec.stem);
    if (path.empty()) continue;
    fv::S57Cell cell;
    ASSERT_TRUE(cell.Open(path).ok()) << spec.stem;
    int rings = 0, incomplete = 0;
    for (const fv::S57Feature& f : cell.features()) {
      if (f.primitive != fv::S57Primitive::kArea) continue;
      if (!f.geometry_complete) ++incomplete;
      ASSERT_FALSE(f.geometry.parts.empty())
          << spec.stem << " area feature " << f.record_id << " has no ring";
      ASSERT_EQ(f.geometry.parts.size(), f.geometry.part_is_hole.size());
      for (const auto& ring : f.geometry.parts) {
        ASSERT_GE(ring.size(), 4u) << spec.stem << " feature " << f.record_id;
        EXPECT_EQ(ring.front().lat, ring.back().lat)
            << spec.stem << " feature " << f.record_id;
        EXPECT_EQ(ring.front().lon, ring.back().lon)
            << spec.stem << " feature " << f.record_id;
        ++rings;
      }
    }
    EXPECT_GT(rings, 200) << spec.stem;
    EXPECT_EQ(0, incomplete)
        << spec.stem << ": every area assembled from complete chain-node edges";
  }
}

TEST(S57Cell, LineAndPointFeaturesHaveUsableGeometry) {
  OPEN_CELL(cell, "US5CHSDC");
  int lines = 0, points = 0;
  for (const fv::S57Feature& f : cell.features()) {
    if (f.primitive == fv::S57Primitive::kLine) {
      ++lines;
      ASSERT_FALSE(f.geometry.parts.empty()) << "line feature " << f.record_id;
      for (const auto& run : f.geometry.parts)
        ASSERT_GE(run.size(), 2u) << "line feature " << f.record_id;
    } else if (f.primitive == fv::S57Primitive::kPoint) {
      ++points;
      ASSERT_FALSE(f.geometry.parts.empty()) << "point feature " << f.record_id;
      ASSERT_FALSE(f.geometry.parts[0].empty());
    }
  }
  EXPECT_EQ(287, lines);
  EXPECT_EQ(105, points);
}

TEST(S57Cell, SoundingsCarryDepthsScaledBySomf) {
  OPEN_CELL(cell, "US5CHSDC");
  int sounding_features = 0;
  size_t sounding_vertices = 0;
  bool found_pinned = false;
  for (const fv::S57Feature& f : cell.features()) {
    if (f.primitive != fv::S57Primitive::kPoint) continue;
    bool is_sounding = false;
    for (const auto& part : f.geometry.parts)
      for (const fv::S57Vertex& v : part) {
        if (!v.has_depth) continue;
        is_sounding = true;
        ++sounding_vertices;
        // Charleston Harbor: a plausible ENC sounding, drying heights negative.
        EXPECT_GT(v.depth, -5.0);
        EXPECT_LT(v.depth, 40.0);
        if (v.lat == 32.735895 && v.lon == -80.011249) {
          EXPECT_DOUBLE_EQ(1.5, v.depth) << "SOMF is 10, so 15 stores as 1.5 m";
          found_pinned = true;
        }
      }
    if (is_sounding) ++sounding_features;
  }
  EXPECT_EQ(11, sounding_features) << "sounding arrays are one point feature each";
  EXPECT_EQ(165u, sounding_vertices);
  EXPECT_TRUE(found_pinned) << "the cell's first sounding vertex";
}

// --- pinned features --------------------------------------------------------

TEST(S57Cell, PinnedLineFeature) {
  OPEN_CELL(cell, "US5CHSDC");
  const fv::S57Feature* f = ById(cell, 59);
  ASSERT_NE(nullptr, f);
  EXPECT_EQ(fv::S57Primitive::kLine, f->primitive);
  EXPECT_EQ(30, f->object_class);
  EXPECT_EQ(2, f->group) << "group 2 = not skin of the earth";
  ASSERT_EQ(1u, f->geometry.parts.size());
  const auto& run = f->geometry.parts[0];
  ASSERT_EQ(3u, run.size());
  EXPECT_DOUBLE_EQ(32.7671766, run[0].lat);
  EXPECT_DOUBLE_EQ(-80.025, run[0].lon) << "starts on the cell's west limit";
  EXPECT_DOUBLE_EQ(32.767156, run[2].lat);
  EXPECT_DOUBLE_EQ(-80.024802, run[2].lon);
  EXPECT_FALSE(run[0].has_depth);

  ASSERT_EQ(3u, f->attributes.size());
  EXPECT_EQ(15, f->attributes[0].code);
  EXPECT_EQ("8", f->attributes[0].value);
  EXPECT_FALSE(f->attributes[0].national);
  EXPECT_EQ(147, f->attributes[1].code);
  EXPECT_EQ(148, f->attributes[2].code);
  EXPECT_EQ("US,US,graph,Chart 11518", f->attributes[2].value);
  ASSERT_NE(nullptr, f->FindAttribute(147));
  EXPECT_EQ("200605", f->FindAttribute(147)->value);
  EXPECT_EQ(nullptr, f->FindAttribute(9999));
}

TEST(S57Cell, PinnedSingleRingArea) {
  OPEN_CELL(cell, "US5CHSDC");
  const fv::S57Feature* f = ById(cell, 444);
  ASSERT_NE(nullptr, f);
  EXPECT_EQ(fv::S57Primitive::kArea, f->primitive);
  EXPECT_EQ(308, f->object_class);
  ASSERT_EQ(1u, f->geometry.parts.size());
  EXPECT_FALSE(f->geometry.part_is_hole[0]);
  const auto& ring = f->geometry.parts[0];
  ASSERT_EQ(11u, ring.size());
  EXPECT_DOUBLE_EQ(32.774769, ring.front().lat);
  EXPECT_DOUBLE_EQ(-79.95, ring.front().lon);
  EXPECT_DOUBLE_EQ(32.7747598, ring[3].lat);
  EXPECT_DOUBLE_EQ(-79.950132, ring[3].lon);
  EXPECT_EQ(ring.front().lat, ring.back().lat);
  EXPECT_NEAR(32.7746671, f->bounds.ll.lat, 1e-9) << "ring[1] is the southernmost";
  EXPECT_NEAR(32.775, f->bounds.ur.lat, 1e-9);
  EXPECT_NEAR(-79.9504743, f->bounds.ll.lon, 1e-9);
  EXPECT_NEAR(-79.95, f->bounds.ur.lon, 1e-9);
}

TEST(S57Cell, PinnedAreaWithHoles) {
  // USAG on this feature's FSPT pointers is 12x exterior, 3x interior, 1x
  // exterior-truncated-by-data-limit: three rings, two of them holes.
  OPEN_CELL(cell, "US5CHSDC");
  const fv::S57Feature* f = ById(cell, 448);
  ASSERT_NE(nullptr, f);
  EXPECT_EQ(fv::S57Primitive::kArea, f->primitive);
  EXPECT_EQ(308, f->object_class);
  ASSERT_EQ(3u, f->geometry.parts.size());
  EXPECT_EQ(145u, f->geometry.parts[0].size());
  EXPECT_EQ(18u, f->geometry.parts[1].size());
  EXPECT_EQ(19u, f->geometry.parts[2].size());
  EXPECT_FALSE(f->geometry.part_is_hole[0]) << "part 0 is the outer ring";
  EXPECT_TRUE(f->geometry.part_is_hole[1]);
  EXPECT_TRUE(f->geometry.part_is_hole[2]);
  EXPECT_DOUBLE_EQ(32.7744576, f->geometry.parts[0].front().lat);
  EXPECT_DOUBLE_EQ(-80.0181507, f->geometry.parts[0].front().lon);
  // Holes lie inside the outer ring's box.
  for (size_t p = 1; p < f->geometry.parts.size(); ++p)
    for (const fv::S57Vertex& v : f->geometry.parts[p]) {
      EXPECT_GE(v.lat, f->bounds.ll.lat);
      EXPECT_LE(v.lat, f->bounds.ur.lat);
    }
  ASSERT_NE(nullptr, f->FindAttribute(151));
  EXPECT_EQ("1939", f->FindAttribute(151)->value);
}

TEST(S57Cell, FeatureObjectIdsAreDecodedAndUnique) {
  OPEN_CELL(cell, "US5CHSDC");
  std::set<std::tuple<int, uint32_t, int>> seen;
  for (const fv::S57Feature& f : cell.features()) {
    EXPECT_EQ(550, f.object_id.agency) << "feature " << f.record_id;
    EXPECT_NE(0u, f.object_id.feature_id) << "feature " << f.record_id;
    EXPECT_TRUE(seen
                    .insert({f.object_id.agency, f.object_id.feature_id,
                             f.object_id.subdivision})
                    .second)
        << "duplicate FOID on feature " << f.record_id;
  }
}

TEST(S57Cell, FeatureToFeaturePointersAreDecoded) {
  OPEN_CELL(cell, "US5CHSDC");
  int relations = 0;
  for (const fv::S57Feature& f : cell.features())
    for (const fv::S57FeatureRelation& r : f.relations) {
      ++relations;
      EXPECT_EQ(550, r.target.agency) << "LNAM agency of feature " << f.record_id;
      EXPECT_NE(0u, r.target.feature_id);
      EXPECT_GE(r.relation, 1);
      EXPECT_LE(r.relation, 3);
    }
  // 33 pointers across 21 FFPT fields: FFPT is a repeating field, so one field
  // can relate a feature to several others.
  EXPECT_EQ(33, relations);
}

TEST(S57Cell, CollectionFeaturesArePresentAndCarryNoGeometry) {
  // Primitive-less features (C_AGGR/C_ASSO and friends) exist to relate other
  // features through FFPT; they have nothing to draw.
  OPEN_CELL(cell, "US5CHSED");
  EXPECT_EQ(12, CountPrimitive(cell, fv::S57Primitive::kNone));
  for (const fv::S57Feature& f : cell.features())
    if (f.primitive == fv::S57Primitive::kNone)
      EXPECT_TRUE(f.geometry.parts.empty()) << "feature " << f.record_id;
}

TEST(S57Cell, PerCellTotalsArePinned) {
  const struct {
    const char* stem;
    int points, lines, areas, meta;
    size_t vertices;
  } kExpect[] = {
      {"US5CHSDC", 105, 287, 208, 0, 32737},
      {"US5CHSDD", 204, 576, 467, 5, 60011},
      {"US5CHSEC", 165, 389, 299, 0, 48670},
      {"US5CHSED", 457, 762, 551, 12, 64260},
  };
  for (const auto& e : kExpect) {
    const std::string path = CellPath(e.stem);
    if (path.empty()) continue;
    fv::S57Cell cell;
    ASSERT_TRUE(cell.Open(path).ok()) << e.stem;
    EXPECT_EQ(e.points, CountPrimitive(cell, fv::S57Primitive::kPoint)) << e.stem;
    EXPECT_EQ(e.lines, CountPrimitive(cell, fv::S57Primitive::kLine)) << e.stem;
    EXPECT_EQ(e.areas, CountPrimitive(cell, fv::S57Primitive::kArea)) << e.stem;
    EXPECT_EQ(e.meta, CountPrimitive(cell, fv::S57Primitive::kNone)) << e.stem;
    EXPECT_EQ(e.vertices, VertexCount(cell)) << e.stem;
  }
}

TEST(S57Cell, RereadingACellGivesTheSameResult) {
  OPEN_CELL(cell, "US5CHSDD");
  const size_t features = cell.features().size();
  const size_t vertices = VertexCount(cell);
  fv::S57Cell again;
  ASSERT_TRUE(again.Open(cell_path).ok());
  ASSERT_EQ(features, again.features().size());
  EXPECT_EQ(vertices, VertexCount(again));
  for (size_t i = 0; i < features; ++i) {
    ASSERT_EQ(cell.features()[i].record_id, again.features()[i].record_id) << i;
    ASSERT_EQ(cell.features()[i].geometry.parts.size(),
              again.features()[i].geometry.parts.size())
        << i;
  }
}

// --- staleness: the base edition is not the current chart -------------------

TEST(S57Cell, UnappliedUpdateFilesAreReportedLoudly) {
  OPEN_CELL(cell, "US5CHSDC");
  const std::vector<std::string>& updates = cell.unapplied_updates();
  ASSERT_EQ(3u, updates.size()) << "US5CHSDC ships .001 through .003";
  EXPECT_NE(std::string::npos, updates[0].find("US5CHSDC.001"));
  EXPECT_NE(std::string::npos, updates[2].find("US5CHSDC.003"));
  const std::string warning = cell.StalenessWarning();
  EXPECT_NE(std::string::npos, warning.find("BASE EDITION"));
  EXPECT_NE(std::string::npos, warning.find("out of date"));
  EXPECT_NE(std::string::npos, warning.find("US5CHSDC.003"));
}

TEST(S57Cell, EveryDeliveredCellHasUnappliedUpdates) {
  // True of all four NOAA cells as delivered, and the reason E1 must be loud:
  // there is no "base cell is fine" case in this data set.
  SKIP_WITHOUT_ENC();
  for (const CellSpec& spec : kCells) {
    const std::string path = CellPath(spec.stem);
    if (path.empty()) continue;
    fv::S57Cell cell;
    ASSERT_TRUE(cell.Open(path).ok()) << spec.stem;
    EXPECT_FALSE(cell.unapplied_updates().empty()) << spec.stem;
    EXPECT_FALSE(cell.StalenessWarning().empty()) << spec.stem;
  }
}

TEST(S57Cell, MissingCellIsAnError) {
  fv::S57Cell cell;
  const fv::Status s = cell.Open("/nonexistent/enc/US5NOPE.000");
  EXPECT_EQ(fv::kIoError, s.code);
  EXPECT_FALSE(cell.is_open());
}

// --- exchange set: catalogue and enumeration --------------------------------

TEST(S57Catalog, ReadsCatdBoundsAndLongNames) {
  SKIP_WITHOUT_ENC();
  // The root CATALOG.031 belongs to the US5CHSEC download (the other three
  // sets' catalogues stayed in their ENC_ROOT-N shells).
  const std::string path = enc_root + "/CATALOG.031";
  if (!fs::is_regular_file(path)) GTEST_SKIP() << "no CATALOG.031";
  std::vector<fv::S57CatalogEntry> entries;
  ASSERT_TRUE(fv::ReadS57Catalog(path, &entries).ok());
  ASSERT_EQ(5u, entries.size());

  // The catalogue lists itself first, without bounds.
  EXPECT_EQ("CATALOG.031", entries[0].file);
  EXPECT_FALSE(entries[0].has_bounds);
  EXPECT_EQ("ASC", entries[0].implementation);

  const fv::S57CatalogEntry* base = nullptr;
  for (const fv::S57CatalogEntry& e : entries)
    if (e.file.find("US5CHSEC.000") != std::string::npos) base = &e;
  ASSERT_NE(nullptr, base) << "the base cell must be catalogued";
  EXPECT_EQ(4, base->record_id);
  EXPECT_EQ("Ashley River", base->long_file_name);
  EXPECT_EQ("BIN", base->implementation);
  EXPECT_EQ("E807A340", base->crc);
  ASSERT_TRUE(base->has_bounds) << "CATD carries the cell's box";
  EXPECT_DOUBLE_EQ(32.775, base->bounds.ll.lat);
  EXPECT_DOUBLE_EQ(-80.025, base->bounds.ll.lon);
  EXPECT_DOUBLE_EQ(32.850, base->bounds.ur.lat);
  EXPECT_DOUBLE_EQ(-79.950, base->bounds.ur.lon);
  // FILE is a DOS-style relative path; resolving it is the consumer's problem.
  EXPECT_NE(std::string::npos, base->file.find('\\'));
}

TEST(S57Catalog, CatalogueBoundsAgreeWithTheCellItDescribes) {
  SKIP_WITHOUT_ENC();
  const std::string path = enc_root + "/CATALOG.031";
  const std::string cell_path = CellPath("US5CHSEC");
  if (!fs::is_regular_file(path) || cell_path.empty())
    GTEST_SKIP() << "no catalogue or cell";
  std::vector<fv::S57CatalogEntry> entries;
  ASSERT_TRUE(fv::ReadS57Catalog(path, &entries).ok());
  fv::S57Cell cell;
  ASSERT_TRUE(cell.Open(cell_path).ok());
  for (const fv::S57CatalogEntry& e : entries) {
    if (e.file.find("US5CHSEC.000") == std::string::npos) continue;
    const fv::GeoRect& b = cell.bounds();
    EXPECT_GE(b.ll.lat, e.bounds.ll.lat - 1e-6);
    EXPECT_GE(b.ll.lon, e.bounds.ll.lon - 1e-6);
    EXPECT_LE(b.ur.lat, e.bounds.ur.lat + 1e-6);
    EXPECT_LE(b.ur.lon, e.bounds.ur.lon + 1e-6);
  }
}

TEST(S57Catalog, NonCatalogFileIsRejected) {
  const std::string cell = CellPath("US5CHSDC");
  if (cell.empty()) GTEST_SKIP() << "no ENC test data";
  std::vector<fv::S57CatalogEntry> entries;
  EXPECT_EQ(fv::kIoError, fv::ReadS57Catalog(cell, &entries).code)
      << "a cell has no CATD field";
}

TEST(S57Enumerate, FindsCellsByExtensionNotByEncRootLayout) {
  SKIP_WITHOUT_ENC();
  std::vector<std::string> cells;
  ASSERT_TRUE(fv::EnumerateEncCells(enc_root, &cells).ok());

  // Derived, not hardcoded: whatever *.000 files are on disk are what the
  // enumerator must return (the delivered sets have their cell folders lifted
  // out of ENC_ROOT, and more cells may be dropped in later).
  std::vector<std::string> on_disk;
  for (fs::recursive_directory_iterator it(enc_root), end; it != end; ++it)
    if (it->is_regular_file() && it->path().extension() == ".000")
      on_disk.push_back(it->path().string());
  std::sort(on_disk.begin(), on_disk.end());
  EXPECT_EQ(on_disk, cells);
  EXPECT_GE(cells.size(), 4u);

  for (const CellSpec& spec : kCells) {
    bool found = false;
    for (const std::string& c : cells)
      if (c.find(std::string(spec.stem) + ".000") != std::string::npos) found = true;
    EXPECT_TRUE(found) << spec.stem;
  }
  for (const std::string& c : cells)
    EXPECT_EQ(std::string::npos, c.find(".001")) << "updates are not base cells";
}

TEST(S57Enumerate, MissingDirectory) {
  std::vector<std::string> cells;
  EXPECT_EQ(fv::kNotFound, fv::EnumerateEncCells("/nonexistent/enc", &cells).code);
}

TEST(S57CellNameTest, DecomposesTheStructuredName) {
  const fv::S57CellName n = fv::ParseCellName("/data/enc/US5CHSDC/US5CHSDC.000");
  ASSERT_TRUE(n.valid);
  EXPECT_EQ("US", n.producer) << "US = NOAA";
  EXPECT_EQ(5, n.usage_band) << "5 = harbour";
  EXPECT_EQ("CHS", n.region);
  EXPECT_EQ("DC", n.id);

  EXPECT_EQ(1, fv::ParseCellName("GB1ABCDE.000").usage_band) << "1 = overview";
  EXPECT_TRUE(fv::ParseCellName("us5chsdc.000").valid) << "case-insensitive";
  EXPECT_EQ("US", fv::ParseCellName("us5chsdc.000").producer);
  EXPECT_FALSE(fv::ParseCellName("CATALOG.031").valid)
      << "no usage-band digit in position 3";
  EXPECT_FALSE(fv::ParseCellName("US5.000").valid) << "too short";
}

}  // namespace
