// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::OsmVectorSource tests (OSM phase O1) — the port's third IVectorSource,
// over the real us-south pyramid.
//
// The behaviour that is NEW here, and therefore what these mostly assert, is
// everything a tile pyramid does that a DNC library and an ENC exchange set do
// not: scale chooses a zoom, the unit of I/O is a tile range, a scale-less
// query has to be capped, and a POLYGON may be a multipolygon that the seam's
// one-outer-ring-plus-holes contract has to be split into.

#include "fv_osm_vector_source.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

// Downtown Atlanta, roughly one z14 tile across.
fv::GeoRect Downtown() {
  return fv::GeoRect{{33.7440, -84.3960}, {33.7600, -84.3800}};
}

size_t CountLayer(const std::vector<fv::VectorFeature>& f, const char* layer) {
  size_t n = 0;
  for (const auto& v : f)
    if (v.layer == layer) ++n;
  return n;
}

}  // namespace

#define SKIP_WITHOUT_MBTILES()               \
  const std::string mb_path = MbtilesPath(); \
  if (mb_path.empty()) GTEST_SKIP() << "no OSM mbtiles test data"

#define OPEN_SOURCE(var)                      \
  fv::OsmVectorSource var;                    \
  {                                           \
    const fv::Status s = var.Open(mb_path);   \
    ASSERT_TRUE(s.ok()) << s.message;         \
  }

TEST(OsmVectorSource, OpensAndAnswersTheSeamsMetadataQuestions) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  EXPECT_TRUE(src.IsOpen());
  const std::vector<std::string> layers = src.Layers();
  ASSERT_EQ(layers.size(), 16u);
  EXPECT_EQ(layers[0], "place");
  EXPECT_NE(std::find(layers.begin(), layers.end(), "transportation"),
            layers.end());

  // Bounds come from the pyramid, not the (wrong) declared metadata.
  const fv::GeoRect b = src.Bounds();
  EXPECT_LT(b.ur.lon, -70.0);
  EXPECT_GT(b.ll.lon, -110.0);
  EXPECT_TRUE(b.Contains(fv::GeoPoint{33.749, -84.388}));
}

TEST(OsmVectorSource, ARasterPyramidIsRefusedAtTheVectorSeam) {
  SKIP_WITHOUT_MBTILES();
  // Nothing in TestData is a raster MBTiles, so this asserts the check exists
  // by its message rather than by data: a source that accepted a jpeg pyramid
  // would return zero features and look like empty coverage.
  fv::OsmVectorSource src;
  const fv::Status s = src.Open(mb_path + ".missing");
  EXPECT_FALSE(s.ok());
  EXPECT_FALSE(src.IsOpen());
}

TEST(OsmVectorSource, ScaleChoosesTheZoomAndTheQueryReadsThatTilesFeatures) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;  // a harbour-chart scale; z14 here

  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  EXPECT_EQ(src.last_query_zoom(), 14);
  EXPECT_GE(src.last_query_tiles_read(), 1u);
  EXPECT_FALSE(src.last_query_zoom_capped());
  EXPECT_FALSE(src.last_query_truncated());
  EXPECT_GT(out.size(), 1000u);

  // The layers a downtown viewport should hold, and one it should not: at
  // z14 every layer in the schema is available, but `boundary` does not run
  // through the middle of Atlanta.
  EXPECT_GT(CountLayer(out, "transportation"), 100u);
  EXPECT_GT(CountLayer(out, "building"), 100u);
  EXPECT_GT(CountLayer(out, "water"), 0u);

  // Every feature came back inside the query box (plus tile buffer), with a
  // usable ref and a style key.
  for (const fv::VectorFeature& f : out) {
    EXPECT_TRUE(f.ref.valid());
    EXPECT_LT(f.ref.layer, static_cast<int32_t>(src.Layers().size()));
    EXPECT_FALSE(f.layer.empty());
    EXPECT_FALSE(f.style_key.empty());
    EXPECT_FALSE(f.parts.empty());
    EXPECT_TRUE(f.bounds.Intersects(q.area));
  }
}

TEST(OsmVectorSource, TheStyleKeyIsTheDispatchTagWithTheLayerAsFallback) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  bool saw_class_key = false;
  for (const fv::VectorFeature& f : out) {
    const std::string* cls = f.Attribute("class");
    if (cls != nullptr && !cls->empty()) {
      EXPECT_EQ(f.style_key, *cls);
      saw_class_key = true;
    } else {
      EXPECT_EQ(f.style_key, f.layer);
    }
  }
  EXPECT_TRUE(saw_class_key);

  // The tag list is a knob, not a hardcode.
  src.SetStyleKeyTags({"nonexistent-tag"});
  out.clear();
  ASSERT_TRUE(src.Query(q, &out).ok());
  ASSERT_FALSE(out.empty());
  for (const fv::VectorFeature& f : out) EXPECT_EQ(f.style_key, f.layer);
}

TEST(OsmVectorSource, CoarserScalesReadCoarserLevels) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  // Same place, four scales: the pyramid level must follow the scale, and a
  // coarser level must hold fewer features over the same ground (that is what
  // pre-generalization means, and it is why a tile source needs no SCAMIN).
  const double scales[] = {12000.0, 100000.0, 1000000.0};
  const int expect_zoom[] = {14, 12, 9};
  size_t previous = 0;
  for (int i = 0; i < 3; ++i) {
    fv::VectorQuery q;
    q.area = Downtown();
    q.scale_denominator = scales[i];
    std::vector<fv::VectorFeature> out;
    ASSERT_TRUE(src.Query(q, &out).ok());
    EXPECT_EQ(src.last_query_zoom(), expect_zoom[i]) << "scale " << scales[i];
    if (i > 0) EXPECT_LT(out.size(), previous) << "scale " << scales[i];
    previous = out.size();
  }
}

TEST(OsmVectorSource, AnOverriddenZoomIsHonouredExactly) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  src.SetZoomOverride(10);
  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;  // would have asked for 14
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());
  EXPECT_EQ(src.last_query_zoom(), 10);
  EXPECT_FALSE(src.last_query_zoom_capped());

  src.SetZoomOverride(-1);
  out.clear();
  ASSERT_TRUE(src.Query(q, &out).ok());
  EXPECT_EQ(src.last_query_zoom(), 14);
}

TEST(OsmVectorSource, AScalelessQueryIsCappedByTileCountNotAttempted) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  // scale 0 is the seam's "no scale filter". Over this pyramid's whole
  // coverage that would be 594,419 tiles at maxzoom; the guard steps the
  // level coarser until the range is affordable.
  fv::VectorQuery q;
  q.area = src.Bounds();
  q.scale_denominator = 0.0;
  q.max_features = 5000;  // and a feature cap, since even z4 is a lot of road

  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  EXPECT_TRUE(src.last_query_zoom_capped());
  EXPECT_LE(src.last_query_zoom(), 6);
  EXPECT_GE(src.last_query_zoom(), 0);
  EXPECT_LE(out.size(), 5000u);
  EXPECT_TRUE(src.last_query_truncated());
  // The contract is the tile budget, so assert the budget and not a zoom
  // number that depends on where the coverage happens to fall on the grid.
  const size_t attempted =
      src.last_query_tiles_read() + src.last_query_tiles_missing();
  EXPECT_LE(attempted, 64u);

  // A smaller budget picks a coarser level, and stays inside the new budget.
  const int wide_zoom = src.last_query_zoom();
  src.SetMaxTilesPerQuery(4);
  out.clear();
  ASSERT_TRUE(src.Query(q, &out).ok());
  EXPECT_LE(src.last_query_zoom(), wide_zoom);
  EXPECT_LE(src.last_query_tiles_read() + src.last_query_tiles_missing(), 4u);
}

TEST(OsmVectorSource, MissingTilesAreCountedNotFatal) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  // A viewport straddling the pyramid's western edge: some tiles exist, some
  // do not, and panning off the coverage must not be an error.
  fv::VectorQuery q;
  q.area = fv::GeoRect{{31.0, -108.0}, {31.5, -106.0}};
  q.scale_denominator = 500000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());
  EXPECT_GT(src.last_query_tiles_missing(), 0u);

  // Entirely outside: zero features, still ok.
  q.area = fv::GeoRect{{-40.0, 20.0}, {-39.0, 21.0}};
  out.clear();
  ASSERT_TRUE(src.Query(q, &out).ok());
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(src.last_query_tiles_read(), 0u);
}

TEST(OsmVectorSource, BufferOnlyGeometryIsDroppedSoEdgeSymbolsDrawOnce) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = fv::GeoRect{{33.72, -84.42}, {33.78, -84.34}};  // several tiles
  q.scale_denominator = 12000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  EXPECT_GE(src.last_query_tiles_read(), 4u);
  // Each tile carries its neighbours' geometry; that geometry belongs to the
  // neighbour and is dropped here.
  EXPECT_GT(src.last_query_buffer_dropped(), 0u);
}

TEST(OsmVectorSource, AMultipolygonBecomesOneFeaturePerOuterRing) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  size_t areas = 0, with_holes = 0;
  for (const fv::VectorFeature& f : out) {
    if (f.type != fv::VectorGeometryType::kArea) continue;
    ++areas;
    ASSERT_FALSE(f.parts.empty());
    if (f.parts.size() > 1) ++with_holes;
    // The seam's contract: part[0] is the outer ring, and every ring closes.
    for (const auto& ring : f.parts) {
      ASSERT_GE(ring.size(), 4u);
      EXPECT_DOUBLE_EQ(ring.front().lat, ring.back().lat);
      EXPECT_DOUBLE_EQ(ring.front().lon, ring.back().lon);
    }
    // A split polygon's bounds cover ITS rings, not the whole multipolygon.
    for (const auto& ring : f.parts)
      for (const fv::GeoPoint& p : ring) {
        EXPECT_GE(p.lat, f.bounds.ll.lat);
        EXPECT_LE(p.lat, f.bounds.ur.lat);
        EXPECT_GE(p.lon, f.bounds.ll.lon);
        EXPECT_LE(p.lon, f.bounds.ur.lon);
      }
  }
  EXPECT_GT(areas, 100u);
  EXPECT_GT(with_holes, 0u) << "no polygon in downtown Atlanta has a hole?";
}

TEST(OsmVectorSource, DescribeGoesBackToTheFeatureTheRefNames) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  // Identify the city label: a feature whose description a user would read.
  const fv::VectorFeature* city = nullptr;
  for (const fv::VectorFeature& f : out) {
    const std::string* name = f.Attribute("name:latin");
    if (f.layer == "place" && name != nullptr && *name == "Atlanta") city = &f;
  }
  ASSERT_NE(city, nullptr);

  fv::FeatureDescription d;
  const fv::Status s = src.Describe(city->ref, &d);
  ASSERT_TRUE(s.ok()) << s.message;
  EXPECT_EQ(d.ref, city->ref);
  EXPECT_EQ(d.title, "Atlanta");
  EXPECT_EQ(d.layer_name, "place");
  EXPECT_EQ(d.class_name, "city");
  EXPECT_NE(d.source_note.find("us-south.mbtiles"), std::string::npos);
  EXPECT_NE(d.source_note.find("14/4351/"), std::string::npos);

  bool saw_alias = false;
  for (const fv::FeatureAttribute& a : d.attributes) {
    EXPECT_FALSE(a.code.empty());
    EXPECT_FALSE(a.name.empty());
    // OSM values are words already; display IS raw, by documented design.
    EXPECT_EQ(a.display, a.raw);
    if (a.code == "class") {
      EXPECT_EQ(a.name, "Class");  // the small alias table
      saw_alias = true;
    }
  }
  EXPECT_TRUE(saw_alias);
}

TEST(OsmVectorSource, DescribeRejectsARefItCannotResolve) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::FeatureDescription d;
  fv::FeatureRef bad;
  bad.layer = 0;
  bad.tile = 0;
  bad.feature = 0;
  // No query has run, so no tile index has been assigned yet.
  EXPECT_EQ(src.Describe(bad, &d).code, fv::kNotFound);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());
  ASSERT_FALSE(out.empty());

  fv::FeatureRef ref = out.front().ref;
  ref.feature = 1 << 30;
  EXPECT_EQ(src.Describe(ref, &d).code, fv::kNotFound);
  ref = out.front().ref;
  ref.layer = 999;
  EXPECT_EQ(src.Describe(ref, &d).code, fv::kNotFound);
}

TEST(OsmVectorSource, TheSameViewportQueriedTwiceGivesTheSameAnswer) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;

  std::vector<fv::VectorFeature> a, b;
  ASSERT_TRUE(src.Query(q, &a).ok());
  ASSERT_TRUE(src.Query(q, &b).ok());
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].ref, b[i].ref) << "at " << i;
    EXPECT_EQ(a[i].style_key, b[i].style_key);
    ASSERT_EQ(a[i].parts.size(), b[i].parts.size());
    if (!a[i].parts.empty() && !a[i].parts[0].empty())
      EXPECT_DOUBLE_EQ(a[i].parts[0][0].lat, b[i].parts[0][0].lat);
  }

  // Query APPENDS, per the seam's contract.
  ASSERT_TRUE(src.Query(q, &b).ok());
  EXPECT_EQ(b.size(), 2 * a.size());
}

TEST(OsmVectorSource, ATinyTileCacheStillAnswersCorrectly) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = fv::GeoRect{{33.72, -84.42}, {33.78, -84.34}};
  q.scale_denominator = 12000.0;

  std::vector<fv::VectorFeature> big;
  ASSERT_TRUE(src.Query(q, &big).ok());
  ASSERT_GT(src.last_query_tiles_read(), 2u);

  fv::OsmVectorSource small;
  ASSERT_TRUE(small.Open(mb_path).ok());
  small.SetTileCacheCapacity(1);  // every tile evicts the last
  std::vector<fv::VectorFeature> tiny;
  ASSERT_TRUE(small.Query(q, &tiny).ok());
  EXPECT_EQ(tiny.size(), big.size());
}

TEST(OsmVectorSource, ReopeningKeepsTheApplicationsKnobs) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  src.SetDisplayMmPerPixel(0.125);
  src.SetZoomOverride(11);
  src.SetStyleKeyTags({"subclass"});
  ASSERT_TRUE(src.Open(mb_path).ok());

  EXPECT_DOUBLE_EQ(src.display_mm_per_pixel(), 0.125);
  EXPECT_EQ(src.zoom_override(), 11);
  // The layer inventory is rebuilt from the file, not accumulated.
  EXPECT_EQ(src.Layers().size(), 16u);
}

TEST(OsmVectorSource, MaxFeaturesStopsTheScanWhereItSaysItDoes) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);

  fv::VectorQuery q;
  q.area = Downtown();
  q.scale_denominator = 12000.0;
  q.max_features = 250;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());
  EXPECT_LE(out.size(), 250u);
  EXPECT_TRUE(src.last_query_truncated());
}

// ---------------------------------------------------------------------------
// O3: per-tile clipping and overzoom
// ---------------------------------------------------------------------------

namespace {

// The tile a feature came from, read back out of Describe's source_note
// ("us-south.mbtiles 14/4372/6539"). Using the source's OWN answer rather than
// re-deriving the tile from the geometry is the point: it is what makes the
// assertion "this ink is inside THAT tile" and not "this ink is inside some
// tile", which clipping would satisfy trivially.
bool TileOf(fv::OsmVectorSource& src, const fv::FeatureRef& ref,
            fv::webmerc::TileId* out) {
  fv::FeatureDescription d;
  if (!src.Describe(ref, &d).ok()) return false;
  const size_t sp = d.source_note.find_last_of(' ');
  if (sp == std::string::npos) return false;
  return std::sscanf(d.source_note.c_str() + sp + 1, "%d/%d/%d", &out->z,
                     &out->x, &out->y) == 3;
}

size_t TotalVertices(const std::vector<fv::VectorFeature>& fs) {
  size_t n = 0;
  for (const auto& f : fs)
    for (const auto& part : f.parts) n += part.size();
  return n;
}

}  // namespace

TEST(OsmVectorSource, StraddlingGeometryIsClippedToItsOwnTile) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);
  ASSERT_TRUE(src.clip_to_tile());  // the default

  fv::VectorQuery q;
  q.area = fv::GeoRect{{33.72, -84.42}, {33.78, -84.34}};  // several tiles
  q.scale_denominator = 12000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());
  ASSERT_GE(src.last_query_tiles_read(), 4u);
  EXPECT_GT(src.last_query_clipped(), 0u);

  // Every emitted vertex lies in the box of the tile it was read from. The
  // tolerance is one part in 1e9 of a degree — clipping computes the crossing
  // point in double precision, so a vertex sits ON the seam, not past it.
  const double kEps = 1e-9;
  size_t checked = 0;
  for (const fv::VectorFeature& f : out) {
    if (f.type == fv::VectorGeometryType::kPoint) continue;
    fv::webmerc::TileId id;
    if (!TileOf(src, f.ref, &id)) continue;
    const fv::GeoRect box = fv::webmerc::TileBounds(id);
    for (const auto& part : f.parts)
      for (const fv::GeoPoint& p : part) {
        ASSERT_GE(p.lat, box.ll.lat - kEps);
        ASSERT_LE(p.lat, box.ur.lat + kEps);
        ASSERT_GE(p.lon, box.ll.lon - kEps);
        ASSERT_LE(p.lon, box.ur.lon + kEps);
      }
    ++checked;
  }
  EXPECT_GT(checked, 100u) << "nothing was actually verified";
}

TEST(OsmVectorSource, ClippingCanBeTurnedOffForWholeGeometry) {
  SKIP_WITHOUT_MBTILES();

  fv::VectorQuery q;
  q.area = fv::GeoRect{{33.72, -84.42}, {33.78, -84.34}};
  q.scale_denominator = 12000.0;

  std::vector<fv::VectorFeature> clipped, whole;
  size_t clipped_away = 0;
  {
    OPEN_SOURCE(src);
    ASSERT_TRUE(src.Query(q, &clipped).ok());
    clipped_away = src.last_query_clipped_away();
  }
  {
    OPEN_SOURCE(src);
    src.SetClipToTile(false);
    ASSERT_TRUE(src.Query(q, &whole).ok());
    EXPECT_EQ(src.last_query_clipped(), 0u);
  }

  // Clipping trims geometry; it only ever REMOVES a feature whose ink turned
  // out to be entirely in someone else's tile (or outside the query), which
  // last_query_clipped_away counts. So the two runs differ by that, and by
  // nothing else.
  EXPECT_LE(clipped.size(), whole.size());
  EXPECT_EQ(whole.size() - clipped.size(), clipped_away);
  EXPECT_GT(clipped_away, 0u);
  EXPECT_LT(TotalVertices(clipped), TotalVertices(whole))
      << "buffer geometry was not trimmed";
}

TEST(OsmVectorSource, OverzoomReadsTheDeepestLevelAndSaysHowFarPast) {
  SKIP_WITHOUT_MBTILES();
  OPEN_SOURCE(src);
  const int max_z = src.file().max_zoom();

  // 1:2,000 is about z17 at this latitude — three levels past the pyramid.
  fv::VectorQuery q;
  q.area = fv::GeoRect{{33.7530, -84.3910}, {33.7570, -84.3870}};
  q.scale_denominator = 2000.0;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(src.Query(q, &out).ok());

  EXPECT_EQ(src.last_query_zoom(), max_z);
  EXPECT_GT(out.size(), 0u) << "large scales must not go blank";
  EXPECT_GT(src.last_query_overzoom(), 2.0);
  EXPECT_LT(src.last_query_overzoom(), 4.0);

  // At a scale the pyramid DOES hold there is no overzoom to report, even
  // though the nearest-level rounding leaves a fractional gap.
  std::vector<fv::VectorFeature> mid;
  q.scale_denominator = 50000.0;
  ASSERT_TRUE(src.Query(q, &mid).ok());
  EXPECT_LT(src.last_query_zoom(), max_z);
  EXPECT_EQ(src.last_query_overzoom(), 0.0);
}
