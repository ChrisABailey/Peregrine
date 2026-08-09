// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv::VpfVectorSource tests (vpf-geosym plan phase V5a).
//
// Real data: TestData/vpf/dnc17 harbor library h1707300 (Cape Cod / Nantucket
// Sound). Counts and coordinates below are golden values for THAT library, so
// they stay valid if more DNC data is added alongside it.
//
// The coordinate assertions here are load-bearing. Building this source
// surfaced a latent LP64 bug in the V1 reader: vpfrcset.cpp read a
// variable-length field's 4-byte count with `*(long int*)` (8 bytes on LP64).
// The count survived the truncation to int, so nothing looked wrong, but the
// cursor then skipped the payload's first float — every coordinate tuple came
// back as (lat[i], lon[i+1]) and the last one read past the end of the array.
// The symptom was subtle: plausible-looking coordinates, each one vertex out
// of step, plus a trailing (lat, 0). CoordinatesAreNotShifted below is the
// regression guard.

#include "fv_vpf_vector_source.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string HarborLibrary() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  std::string p = std::string(d) + "/vpf/dnc17/h1707300";
  if (!fs::is_directory(p)) return {};
  return p;
}

#define SKIP_WITHOUT_DNC()                       \
  const std::string lib = HarborLibrary();       \
  if (lib.empty()) GTEST_SKIP() << "no dnc17 test data"

// The library's own extent, used as a sanity envelope.
constexpr double kMinLat = 41.55, kMaxLat = 41.85;
constexpr double kMinLon = -70.05, kMaxLon = -69.75;

std::vector<fv::VectorFeature> QueryAll(fv::VpfVectorSource* s) {
  std::vector<fv::VectorFeature> out;
  fv::VectorQuery q;
  s->Query(q, &out);
  return out;
}

const fv::VectorFeature* Find(const std::vector<fv::VectorFeature>& f,
                              const std::string& layer, int feature_id) {
  for (const auto& x : f)
    if (x.layer == layer && x.ref.feature == feature_id) return &x;
  return nullptr;
}

// ---------------------------------------------------------------------------
// Open / error paths
// ---------------------------------------------------------------------------

TEST(VpfVectorSourceErrors, MissingLibrary) {
  fv::VpfVectorSource s;
  EXPECT_FALSE(s.Open("/nonexistent/dnc17/h0000000").ok());
  EXPECT_FALSE(s.IsOpen());
}

TEST(VpfVectorSourceErrors, PathWithoutLibraryComponent) {
  fv::VpfVectorSource s;
  EXPECT_EQ(s.Open("h1707300").code, fv::kInvalidArg);
}

TEST(VpfVectorSourceErrors, QueryBeforeOpen) {
  fv::VpfVectorSource s;
  std::vector<fv::VectorFeature> out;
  fv::VectorQuery q;
  EXPECT_EQ(s.Query(q, &out).code, fv::kNotFound);
  EXPECT_TRUE(out.empty());
}

TEST(VpfVectorSourceErrors, NullOutRejected) {
  fv::VpfVectorSource s;
  fv::VectorQuery q;
  EXPECT_EQ(s.Query(q, nullptr).code, fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

TEST(VpfVectorSourceReal, OpensAndDiscoversLayers) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  EXPECT_TRUE(s.IsOpen());

  const std::vector<std::string> layers = s.Layers();
  // 25 point/line classes (V5a) + 11 drawable area classes (V5c). The 12th
  // area .AFT, dqyarea (data-quality metadata, no FACC), is deliberately not
  // a feature layer.
  EXPECT_EQ(layers.size(), 36u);
  // A few the harbor library must have (line + point classes across coverages).
  for (const char* want : {"coastl", "hydline", "riverl", "pierl", "soundp",
                           "buoybcnp", "lightsp"}) {
    EXPECT_NE(std::find(layers.begin(), layers.end(), std::string(want)),
              layers.end())
        << "missing layer " << want;
  }
  // Area classes ARE served as of V5c (face/ring/edge topology).
  for (const char* want : {"hydarea", "ecrarea", "lakea", "rivera", "embanka"}) {
    EXPECT_NE(std::find(layers.begin(), layers.end(), std::string(want)),
              layers.end())
        << "missing area layer " << want;
  }
  // The data-quality area coverage is metadata, not a symbolized feature.
  EXPECT_EQ(std::find(layers.begin(), layers.end(), std::string("dqyarea")),
            layers.end())
      << "dqyarea has no FACC; it must not be a feature layer";
}

TEST(VpfVectorSourceReal, FeatureCountsAndGeometryKinds) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> f = QueryAll(&s);

  // 1699 lines + 2836 points (V5a) + 502 areas (V5c).
  ASSERT_EQ(f.size(), 5037u);
  int lines = 0, points = 0, areas = 0;
  for (const auto& x : f) {
    if (x.type == fv::VectorGeometryType::kLine) {
      ++lines;
      ASSERT_EQ(x.parts.size(), 1u) << "a DNC line is one edge run";
      EXPECT_GE(x.parts[0].size(), 2u) << x.layer << " line needs >= 2 points";
    } else if (x.type == fv::VectorGeometryType::kPoint) {
      ++points;
      ASSERT_EQ(x.parts.size(), 1u) << "a DNC point is one part";
      EXPECT_EQ(x.parts[0].size(), 1u) << x.layer << " point needs 1 point";
    } else if (x.type == fv::VectorGeometryType::kArea) {
      ++areas;
      // part[0] is the outer ring (a closed loop of >= 3 distinct points +
      // the repeated first point); holes follow.
      ASSERT_GE(x.parts.size(), 1u) << x.layer << " area needs an outer ring";
      EXPECT_GE(x.parts[0].size(), 4u) << x.layer << " outer ring too small";
      const fv::GeoPoint& a = x.parts[0].front();
      const fv::GeoPoint& b = x.parts[0].back();
      EXPECT_EQ(a.lat, b.lat) << x.layer << " outer ring not closed";
      EXPECT_EQ(a.lon, b.lon) << x.layer << " outer ring not closed";
    } else {
      ADD_FAILURE() << "unknown geometry kind in " << x.layer;
    }
    EXPECT_FALSE(x.style_key.empty()) << x.layer << " has no FACC code";
  }
  EXPECT_EQ(lines, 1699);
  EXPECT_EQ(points, 2836);
  EXPECT_EQ(areas, 502);
}

// ---------------------------------------------------------------------------
// Pinned geometry (the golden values)
// ---------------------------------------------------------------------------

TEST(VpfVectorSourceReal, PinnedFeatures) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> f = QueryAll(&s);

  // Depth contour: a closed ring, so first == last.
  const fv::VectorFeature* hyd = Find(f, "hydline", 1);
  ASSERT_NE(hyd, nullptr);
  EXPECT_EQ(hyd->style_key, "BE010");  // FACC: depth contour
  EXPECT_EQ(hyd->type, fv::VectorGeometryType::kLine);
  EXPECT_EQ(hyd->ref.tile, 1);
  ASSERT_EQ(hyd->parts[0].size(), 18u);
  EXPECT_NEAR(hyd->parts[0].front().lat, 41.619080, 1e-5);
  EXPECT_NEAR(hyd->parts[0].front().lon, -69.957443, 1e-5);
  EXPECT_NEAR(hyd->parts[0].back().lat, hyd->parts[0].front().lat, 1e-9);
  EXPECT_NEAR(hyd->parts[0].back().lon, hyd->parts[0].front().lon, 1e-9);

  // Coastline: an open run.
  const fv::VectorFeature* coast = Find(f, "coastl", 1);
  ASSERT_NE(coast, nullptr);
  EXPECT_EQ(coast->style_key, "BA010");  // FACC: coastline / shoreline
  ASSERT_EQ(coast->parts[0].size(), 9u);
  EXPECT_NEAR(coast->parts[0].front().lat, 41.669785, 1e-5);
  EXPECT_NEAR(coast->parts[0].front().lon, -69.961235, 1e-5);
  EXPECT_NEAR(coast->parts[0].back().lat, 41.670441, 1e-5);
  EXPECT_NEAR(coast->parts[0].back().lon, -69.962990, 1e-5);

  // Sounding: a single point.
  const fv::VectorFeature* snd = Find(f, "soundp", 1);
  ASSERT_NE(snd, nullptr);
  EXPECT_EQ(snd->style_key, "BE020");  // FACC: depth sounding
  EXPECT_EQ(snd->type, fv::VectorGeometryType::kPoint);
  ASSERT_EQ(snd->parts[0].size(), 1u);
  EXPECT_NEAR(snd->parts[0][0].lat, 41.749710, 1e-5);
  EXPECT_NEAR(snd->parts[0][0].lon, -69.928581, 1e-5);
}

// ---------------------------------------------------------------------------
// Area topology (V5c): faces resolved to rings via the FAC/RNG/EDG walk.
// ---------------------------------------------------------------------------

TEST(VpfVectorSourceReal, PinnedAreaFace) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> f = QueryAll(&s);

  // hydarea #1: a hydrography area, one outer boundary plus one hole, whose
  // boundary is a closed 40-point loop. Golden values for h1707300.
  const fv::VectorFeature* a = Find(f, "hydarea", 1);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->type, fv::VectorGeometryType::kArea);
  EXPECT_EQ(a->style_key, "BE010");  // FACC: depth area / water
  EXPECT_EQ(a->ref.tile, 1);
  ASSERT_EQ(a->parts.size(), 2u) << "outer ring + one hole";
  ASSERT_EQ(a->parts[0].size(), 40u);

  // Outer ring closes on itself (winged-edge walk returns to the start node).
  EXPECT_EQ(a->parts[0].front().lat, a->parts[0].back().lat);
  EXPECT_EQ(a->parts[0].front().lon, a->parts[0].back().lon);
  EXPECT_NEAR(a->parts[0].front().lat, 41.677448, 1e-5);
  EXPECT_NEAR(a->parts[0].front().lon, -69.996132, 1e-5);

  // Bounds envelope the whole face and sit inside the library extent.
  EXPECT_NEAR(a->bounds.ll.lat, 41.673965, 1e-5);
  EXPECT_NEAR(a->bounds.ll.lon, -69.999443, 1e-5);
  EXPECT_NEAR(a->bounds.ur.lat, 41.678665, 1e-5);
  EXPECT_NEAR(a->bounds.ur.lon, -69.996132, 1e-5);

  // The hole lies within the face bounds.
  for (const fv::GeoPoint& p : a->parts[1]) {
    EXPECT_GE(p.lat, a->bounds.ll.lat);
    EXPECT_LE(p.lat, a->bounds.ur.lat);
    EXPECT_GE(p.lon, a->bounds.ll.lon);
    EXPECT_LE(p.lon, a->bounds.ur.lon);
  }
}

TEST(VpfVectorSourceReal, MultiRingFaceHasHoles) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> f = QueryAll(&s);

  // ecrarea #3: an earth-cover face with one inner ring (a hole). Pins that
  // inner rings are traversed and kept as additional parts.
  const fv::VectorFeature* a = Find(f, "ecrarea", 3);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->type, fv::VectorGeometryType::kArea);
  ASSERT_EQ(a->parts.size(), 2u) << "outer ring + one hole";
  EXPECT_EQ(a->parts[0].size(), 43u);
  EXPECT_EQ(a->parts[1].size(), 13u);
  // Every ring is a closed loop.
  for (const auto& ring : a->parts) {
    ASSERT_GE(ring.size(), 4u);
    EXPECT_EQ(ring.front().lat, ring.back().lat);
    EXPECT_EQ(ring.front().lon, ring.back().lon);
  }
}

TEST(VpfVectorSourceReal, AreaLayerCounts) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> f = QueryAll(&s);

  std::map<std::string, int> by_layer;
  for (const auto& x : f)
    if (x.type == fv::VectorGeometryType::kArea) ++by_layer[x.layer];

  // Golden per-class area counts for h1707300.
  const std::map<std::string, int> want = {
      {"dangera", 2},  {"ecrarea", 84}, {"embanka", 129}, {"foreshoa", 79},
      {"hydarea", 64}, {"lakea", 89},   {"reefa", 2},     {"rivera", 15},
      {"ruinsa", 38}};
  EXPECT_EQ(by_layer, want);
}

// REGRESSION GUARD for the LP64 variable-length-count bug (see file header).
// Both symptoms are checked, because the shifted-by-one form produced
// coordinates that still looked perfectly reasonable in isolation.
TEST(VpfVectorSourceReal, CoordinatesAreNotShifted) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> f = QueryAll(&s);
  ASSERT_FALSE(f.empty());

  size_t zeros = 0, out_of_region = 0;
  for (const auto& x : f) {
    for (const fv::GeoPoint& p : x.parts[0]) {
      // Symptom 1: the run-off-the-end vertex read uninitialized memory,
      // which showed up as an exactly-zero component. No real DNC coordinate
      // in Nantucket Sound is exactly 0.
      if (p.lat == 0.0 || p.lon == 0.0) ++zeros;
      // Symptom 2: half-tuple shift dragged vertices outside the library.
      if (p.lat < kMinLat || p.lat > kMaxLat || p.lon < kMinLon ||
          p.lon > kMaxLon)
        ++out_of_region;
    }
  }
  EXPECT_EQ(zeros, 0u) << "exactly-zero coordinate: reader ran off the array";
  EXPECT_EQ(out_of_region, 0u) << "coordinate outside the library extent";

  // Symptom 3, the sharpest one: lat and lon must not be transposed. A
  // transposed pair would still be "in range" numerically here only by
  // accident, so assert the hemisphere directly.
  for (const auto& x : f) {
    const fv::GeoPoint& p = x.parts[0][0];
    ASSERT_GT(p.lat, 0.0) << x.layer << ": latitude must be north (Cape Cod)";
    ASSERT_LT(p.lon, 0.0) << x.layer << ": longitude must be west";
  }
}

// ---------------------------------------------------------------------------
// Query behavior
// ---------------------------------------------------------------------------

TEST(VpfVectorSourceReal, SpatialFilterNarrowsAndKeepsIntersectionOnly) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  const size_t all = QueryAll(&s).size();

  fv::VectorQuery q;
  q.area = fv::GeoRect{{41.66, -69.98}, {41.70, -69.94}};
  std::vector<fv::VectorFeature> some;
  ASSERT_TRUE(s.Query(q, &some).ok());

  EXPECT_GT(some.size(), 0u);
  EXPECT_LT(some.size(), all) << "a sub-rect must exclude something";
  for (const auto& x : some) {
    const bool disjoint =
        x.bounds.ur.lat < q.area.ll.lat || x.bounds.ll.lat > q.area.ur.lat ||
        x.bounds.ur.lon < q.area.ll.lon || x.bounds.ll.lon > q.area.ur.lon;
    EXPECT_FALSE(disjoint) << x.layer << " does not meet the query rect";
  }
}

TEST(VpfVectorSourceReal, QueryAppendsRatherThanClears) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  std::vector<fv::VectorFeature> out;
  out.resize(3);  // pre-existing entries the source must not touch
  fv::VectorQuery q;
  q.max_features = 10;
  ASSERT_TRUE(s.Query(q, &out).ok());
  EXPECT_GT(out.size(), 3u);
}

TEST(VpfVectorSourceReal, MaxFeaturesCaps) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  fv::VectorQuery q;
  q.max_features = 25;
  std::vector<fv::VectorFeature> out;
  ASSERT_TRUE(s.Query(q, &out).ok());
  // The cap is checked per row, so it is a ceiling, not an exact count.
  EXPECT_LE(out.size(), 25u);
  EXPECT_GT(out.size(), 0u);
}

TEST(VpfVectorSourceReal, BoundsCoverEveryFeature) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());

  const fv::GeoRect b = s.Bounds();
  EXPECT_GT(b.ur.lat, b.ll.lat);
  EXPECT_GT(b.ur.lon, b.ll.lon);
  EXPECT_GE(b.ll.lat, kMinLat);
  EXPECT_LE(b.ur.lat, kMaxLat);

  for (const auto& x : QueryAll(&s)) {
    EXPECT_GE(x.bounds.ll.lat, b.ll.lat) << x.layer;
    EXPECT_LE(x.bounds.ur.lat, b.ur.lat) << x.layer;
    EXPECT_GE(x.bounds.ll.lon, b.ll.lon) << x.layer;
    EXPECT_LE(x.bounds.ur.lon, b.ur.lon) << x.layer;
  }
}

// ---------------------------------------------------------------------------
// The parsed-feature cache (R3c).
//
// Before R3c every Query walked every row of every feature table and built
// every feature, then discarded the ones outside the box — a query returning
// 5 features cost the same 9.4 ms as one returning 5,037. The library is now
// parsed once and every later query is a box test over memory (0.3 ms).
//
// The cache is only allowed to be faster, so the tests are EQUIVALENCE tests
// against the path it replaced (R3b's rule for an optimization): the same
// source answers the same queries with the cache off, and the two answers
// must agree feature for feature, in order.
// ---------------------------------------------------------------------------

namespace {

void ExpectSameFeatures(const std::vector<fv::VectorFeature>& a,
                        const std::vector<fv::VectorFeature>& b) {
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].layer, b[i].layer) << "at " << i;
    EXPECT_EQ(a[i].style_key, b[i].style_key) << "at " << i;
    EXPECT_EQ(static_cast<int>(a[i].type), static_cast<int>(b[i].type)) << i;
    EXPECT_EQ(a[i].ref.layer, b[i].ref.layer) << "at " << i;
    EXPECT_EQ(a[i].ref.tile, b[i].ref.tile) << "at " << i;
    EXPECT_EQ(a[i].ref.feature, b[i].ref.feature) << "at " << i;
    EXPECT_DOUBLE_EQ(a[i].bounds.ll.lat, b[i].bounds.ll.lat) << "at " << i;
    EXPECT_DOUBLE_EQ(a[i].bounds.ur.lon, b[i].bounds.ur.lon) << "at " << i;
    ASSERT_EQ(a[i].parts.size(), b[i].parts.size()) << "at " << i;
    for (size_t p = 0; p < a[i].parts.size(); ++p)
      EXPECT_EQ(a[i].parts[p].size(), b[i].parts[p].size()) << i << "/" << p;
    ASSERT_EQ(a[i].attributes.size(), b[i].attributes.size()) << "at " << i;
    for (size_t k = 0; k < a[i].attributes.size(); ++k)
      EXPECT_EQ(a[i].attributes[k], b[i].attributes[k]) << "at " << i;
  }
}

}  // namespace

TEST(VpfFeatureCache, CachedAnswersMatchTheFullScanExactly) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource cached;
  ASSERT_TRUE(cached.Open(lib).ok());
  fv::VpfVectorSource scanned;
  ASSERT_TRUE(scanned.Open(lib).ok());
  scanned.SetFeatureCacheEnabled(false);
  EXPECT_FALSE(scanned.feature_cache_enabled());

  // Whole library, a sub-rect, a rect that touches nothing, and a rect
  // straddling the library's edge.
  const std::vector<fv::GeoRect> boxes = {
      fv::GeoRect::World(),
      fv::GeoRect{{41.66, -69.98}, {41.70, -69.94}},
      fv::GeoRect{{10.0, 10.0}, {11.0, 11.0}},
      fv::GeoRect{{41.80, -70.10}, {41.90, -69.90}},
  };
  for (const fv::GeoRect& box : boxes) {
    fv::VectorQuery q;
    q.area = box;
    std::vector<fv::VectorFeature> a, b;
    ASSERT_TRUE(cached.Query(q, &a).ok());
    ASSERT_TRUE(scanned.Query(q, &b).ok());
    ExpectSameFeatures(a, b);
  }
  EXPECT_GT(cached.cached_features(), 0u);
  EXPECT_EQ(scanned.cached_features(), 0u) << "the cache was off";
}

// max_features truncates in the same place either way. It counts what is
// ALREADY in `out` too, because Query appends — the seam's contract.
TEST(VpfFeatureCache, TruncationAndAppendingMatchTheFullScan) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource cached;
  ASSERT_TRUE(cached.Open(lib).ok());
  fv::VpfVectorSource scanned;
  ASSERT_TRUE(scanned.Open(lib).ok());
  scanned.SetFeatureCacheEnabled(false);

  for (size_t limit : {size_t{1}, size_t{25}, size_t{5000}}) {
    fv::VectorQuery q;
    q.max_features = limit;
    std::vector<fv::VectorFeature> a, b;
    ASSERT_TRUE(cached.Query(q, &a).ok());
    ASSERT_TRUE(scanned.Query(q, &b).ok());
    EXPECT_LE(a.size(), limit);
    ExpectSameFeatures(a, b);
  }

  // Pre-filled output: three entries the source must leave alone, and which
  // count toward the limit.
  fv::VectorQuery q;
  q.max_features = 10;
  std::vector<fv::VectorFeature> a(3), b(3);
  ASSERT_TRUE(cached.Query(q, &a).ok());
  ASSERT_TRUE(scanned.Query(q, &b).ok());
  EXPECT_EQ(a.size(), 10u);
  ASSERT_EQ(a.size(), b.size());
}

// Repeated identical queries are the interaction this exists for (a pan
// re-queries), and they must not accumulate or drift.
TEST(VpfFeatureCache, RepeatedQueriesAreStable) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  fv::VectorQuery q;
  q.area = fv::GeoRect{{41.66, -69.98}, {41.70, -69.94}};
  std::vector<fv::VectorFeature> first;
  ASSERT_TRUE(s.Query(q, &first).ok());
  ASSERT_FALSE(first.empty());
  for (int i = 0; i < 3; ++i) {
    std::vector<fv::VectorFeature> again;
    ASSERT_TRUE(s.Query(q, &again).ok());
    ExpectSameFeatures(first, again);
  }
}

// Turning the cache off releases it; turning it back on rebuilds it, and the
// answer is the same across the switch.
TEST(VpfFeatureCache, ToggleReleasesAndRebuilds) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const std::vector<fv::VectorFeature> warm = QueryAll(&s);
  const size_t held = s.cached_features();
  EXPECT_GT(held, 0u);

  s.SetFeatureCacheEnabled(false);
  EXPECT_EQ(s.cached_features(), 0u);
  ExpectSameFeatures(warm, QueryAll(&s));

  s.SetFeatureCacheEnabled(true);
  ExpectSameFeatures(warm, QueryAll(&s));
  EXPECT_EQ(s.cached_features(), held);
}

// A re-Open must drop the cache (another library has other features) while
// keeping the caller's SETTING, which belongs to the caller and not the data.
TEST(VpfFeatureCache, ReopenDropsTheCacheAndKeepsTheSetting) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  QueryAll(&s);
  EXPECT_GT(s.cached_features(), 0u);
  ASSERT_TRUE(s.Open(lib).ok());
  EXPECT_EQ(s.cached_features(), 0u) << "a re-open must not serve stale data";

  s.SetFeatureCacheEnabled(false);
  ASSERT_TRUE(s.Open(lib).ok());
  EXPECT_FALSE(s.feature_cache_enabled());
}

TEST(VpfVectorSourceReal, ReopenIsClean) {
  SKIP_WITHOUT_DNC();
  fv::VpfVectorSource s;
  ASSERT_TRUE(s.Open(lib).ok());
  const size_t first = QueryAll(&s).size();
  ASSERT_TRUE(s.Open(lib).ok());
  EXPECT_EQ(QueryAll(&s).size(), first);
}

}  // namespace
