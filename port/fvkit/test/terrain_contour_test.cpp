// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Terrain contour tracing (fvkit/geo/terrain_contour.h, plan step C1).
//
// Every fixture here is a surface whose contours are known on paper -- a
// plane, a cone, a saddle -- because the failure mode of a tracer is not a
// crash but a line joined to the wrong neighbour, and only a shape you can
// predict catches that.

#include "fvkit/geo/terrain_contour.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#include "fvkit/formats/dted.h"
#include "gtest/gtest.h"

namespace {

using fv::ContourLine;
using fv::ElevationGrid;
using fv::GeoPoint;
using fv::GeoRect;

// A grid over 1 degree square at 0,0 so an index maps to a coordinate by
// inspection: post (r, c) sits at lat r*step, lon c*step.
ElevationGrid MakeGrid(int w, int h) {
  ElevationGrid g;
  g.bounds = GeoRect{{0.0, 0.0}, {1.0, 1.0}};
  g.width = w;
  g.height = h;
  g.meters.assign(static_cast<size_t>(w) * h, 0.0f);
  return g;
}

void Set(ElevationGrid* g, int r, int c, float v) {
  g->meters[static_cast<size_t>(r) * g->width + c] = v;
}

int CountAtLevel(const std::vector<ContourLine>& lines, int level_index) {
  int n = 0;
  for (const ContourLine& l : lines)
    if (l.level_index == level_index) ++n;
  return n;
}

// ---------------------------------------------------------------------------
// A plane: one straight line per level, running the full height of the grid.
// ---------------------------------------------------------------------------
TEST(TerrainContour, PlaneGivesOneOpenLinePerLevel) {
  ElevationGrid g = MakeGrid(5, 5);
  // 30, 130, 230, 330, 430 metres west to east: levels 100..400 all interior,
  // and none of them lands on a post.
  for (int r = 0; r < 5; ++r)
    for (int c = 0; c < 5; ++c) Set(&g, r, c, 30.0f + 100.0f * c);

  const std::vector<ContourLine> lines = fv::TraceElevationContours(g, 100.0);
  ASSERT_EQ(lines.size(), 4u);  // 100, 200, 300, 400

  for (size_t i = 0; i < lines.size(); ++i) {
    const ContourLine& l = lines[i];
    EXPECT_EQ(l.level_index, static_cast<int>(i) + 1);
    EXPECT_DOUBLE_EQ(l.level_m, 100.0 * (i + 1));
    EXPECT_FALSE(l.closed);
    // One vertex per post row: the line crosses every row of cells.
    ASSERT_EQ(l.points.size(), 5u);
    // A level 100k metres crosses the cell whose west post is 30 + 100c at
    // fraction 0.7 -- so lon = (c + 0.7) * 0.25 degrees, constant up the line.
    const double want_lon = (i + 0.7) * 0.25;
    for (size_t k = 0; k < l.points.size(); ++k) {
      EXPECT_NEAR(l.points[k].lon, want_lon, 1e-12);
      EXPECT_NEAR(l.points[k].lat, 0.25 * k, 1e-12);
    }
  }
}

// Ascending level order, and the level index is the exact multiple -- which is
// what makes "major" an exact test in the overlay rather than a 5% fudge.
TEST(TerrainContour, LevelsAreOrderedAndIndexed) {
  ElevationGrid g = MakeGrid(9, 9);
  for (int r = 0; r < 9; ++r)
    for (int c = 0; c < 9; ++c) Set(&g, r, c, -150.0f + 50.0f * c);

  const std::vector<ContourLine> lines = fv::TraceElevationContours(g, 100.0);
  ASSERT_FALSE(lines.empty());
  int last = lines.front().level_index;
  for (const ContourLine& l : lines) {
    EXPECT_LE(last, l.level_index);
    last = l.level_index;
    EXPECT_DOUBLE_EQ(l.level_m, l.level_index * 100.0);
  }
  // Range is -150..250, so levels -100, 0, 100, 200 cross it: negative indices
  // included, which a millimetre key packed into an int would have made
  // awkward and which FalconView's `abs(level)` in the major test lost.
  EXPECT_EQ(CountAtLevel(lines, -1), 1);
  EXPECT_EQ(CountAtLevel(lines, 0), 1);
  EXPECT_EQ(CountAtLevel(lines, 2), 1);
}

// ---------------------------------------------------------------------------
// A cone: every contour is a closed ring around the summit.
// ---------------------------------------------------------------------------
TEST(TerrainContour, ConeGivesClosedRings) {
  const int n = 41;
  ElevationGrid g = MakeGrid(n, n);
  const double mid = (n - 1) / 2.0;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const double d = std::hypot(r - mid, c - mid);
      Set(&g, r, c, static_cast<float>(1000.0 - 40.0 * d));
    }
  }

  const std::vector<ContourLine> lines = fv::TraceElevationContours(g, 200.0);
  ASSERT_FALSE(lines.empty());

  int rings = 0;
  for (const ContourLine& l : lines) {
    if (!l.closed) continue;  // the low levels run out of the grid's corners
    ++rings;
    EXPECT_EQ(l.points.front().lat, l.points.back().lat);
    EXPECT_EQ(l.points.front().lon, l.points.back().lon);
    // A ring around the summit encircles the middle of the grid.
    double clat = 0, clon = 0;
    for (const GeoPoint& p : l.points) {
      clat += p.lat;
      clon += p.lon;
    }
    clat /= l.points.size();
    clon /= l.points.size();
    EXPECT_NEAR(clat, 0.5, 0.02);
    EXPECT_NEAR(clon, 0.5, 0.02);
  }
  // 200, 400, 600, 800 are rings; 1000 is the summit post itself and 0 falls
  // outside the cone's footprint in the corners, so it leaves the grid.
  EXPECT_EQ(rings, 4);
  for (int k = 1; k <= 4; ++k) EXPECT_EQ(CountAtLevel(lines, k), 1);
}

// ---------------------------------------------------------------------------
// The saddle, which is the one cell where a tracer has to make a decision.
// ---------------------------------------------------------------------------
ElevationGrid OneCell(float sw, float se, float ne, float nw) {
  ElevationGrid g = MakeGrid(2, 2);
  Set(&g, 0, 0, sw);
  Set(&g, 0, 1, se);
  Set(&g, 1, 1, ne);
  Set(&g, 1, 0, nw);
  return g;
}

TEST(TerrainContour, SaddleSplitsOnTheCellMean) {
  // sw and ne above 5, se and nw below: the classic ambiguous cell.
  // Mean 5.0 is not above the level, so the LOW ground is connected through
  // the middle and the two high corners are cut off separately.
  {
    const ElevationGrid g = OneCell(10, 0, 10, 0);
    const std::vector<ContourLine> lines = fv::TraceElevationContours(g, 5.0);
    ASSERT_EQ(lines.size(), 2u);
    // South-West pair and East-North pair: one line touches the south edge and
    // the west edge (both at lat/lon 0.5 of the cell).
    EXPECT_EQ(lines[0].points.size(), 2u);
    EXPECT_EQ(lines[1].points.size(), 2u);
    // First line: south edge (lat 0) then west edge (lon 0).
    EXPECT_DOUBLE_EQ(lines[0].points[0].lat, 0.0);
    EXPECT_DOUBLE_EQ(lines[0].points[1].lon, 0.0);
  }
  // Lift the north-west corner so the mean goes above the level: now the HIGH
  // ground is connected through the middle and the split flips.
  {
    const ElevationGrid g = OneCell(10, 0, 10, 4);
    const std::vector<ContourLine> lines = fv::TraceElevationContours(g, 5.0);
    ASSERT_EQ(lines.size(), 2u);
    // South edge joins the EAST edge now (lon 1.0), not the west.
    EXPECT_DOUBLE_EQ(lines[0].points[0].lat, 0.0);
    EXPECT_DOUBLE_EQ(lines[0].points[1].lon, 1.0);
  }
}

// ---------------------------------------------------------------------------
// Voids. FalconView filled a missing post with -32767 metres and traced it.
// ---------------------------------------------------------------------------
TEST(TerrainContour, VoidPostsAreNotTerrain) {
  ElevationGrid g = MakeGrid(7, 7);
  for (int r = 0; r < 7; ++r)
    for (int c = 0; c < 7; ++c) Set(&g, r, c, 30.0f + 100.0f * c);

  const size_t whole = fv::TraceElevationContours(g, 100.0).size();
  ASSERT_EQ(whole, 6u);

  Set(&g, 3, 3, std::numeric_limits<float>::quiet_NaN());
  const std::vector<ContourLine> holed = fv::TraceElevationContours(g, 100.0);

  // One void post kills the FOUR cells that meet at it, which straddle two
  // columns -- so the two lines running through those columns are each cut in
  // two and the count goes up by two, not one. Nothing else changes, and no
  // vertex is a NaN or a -32767-metre fantasy.
  EXPECT_EQ(holed.size(), whole + 2);
  for (const ContourLine& l : holed) {
    for (const GeoPoint& p : l.points) {
      EXPECT_FALSE(std::isnan(p.lat));
      EXPECT_FALSE(std::isnan(p.lon));
      EXPECT_GE(p.lat, 0.0);
      EXPECT_LE(p.lat, 1.0);
    }
  }
}

TEST(TerrainContour, DegenerateInputsDrawNothing) {
  ElevationGrid g = MakeGrid(5, 5);
  for (int r = 0; r < 5; ++r)
    for (int c = 0; c < 5; ++c) Set(&g, r, c, 100.0f);
  EXPECT_TRUE(fv::TraceElevationContours(g, 50.0).empty());   // flat
  EXPECT_TRUE(fv::TraceElevationContours(g, 0.0).empty());    // no interval
  EXPECT_TRUE(fv::TraceElevationContours(g, -50.0).empty());
  ElevationGrid empty;
  EXPECT_TRUE(fv::TraceElevationContours(empty, 100.0).empty());
}

// ---------------------------------------------------------------------------
// The sampler
// ---------------------------------------------------------------------------

// A source that is a plane, with a rectangular hole in it.
class FakeElevation : public fv::IElevationSource {
 public:
  GeoRect Bounds() const override { return GeoRect{{0, 0}, {1, 1}}; }
  fv::Status GetElevation(const GeoPoint& p, float* out) override {
    ++queries;
    if (p.lat > 0.4 && p.lat < 0.6 && p.lon > 0.4 && p.lon < 0.6)
      return fv::Status::Error(fv::kOutOfCoverage, "hole");
    *out = static_cast<float>(1000.0 * p.lon);
    return fv::Status::Ok();
  }
  int queries = 0;
};

TEST(TerrainContour, SamplerFillsPostsAndHoles) {
  FakeElevation src;
  ElevationGrid g;
  ASSERT_TRUE(fv::SampleElevationGrid(src, GeoRect{{0, 0}, {1, 1}}, 11, 11, &g)
                  .ok());
  EXPECT_EQ(src.queries, 121);
  EXPECT_EQ(g.width, 11);
  EXPECT_EQ(g.height, 11);
  EXPECT_FLOAT_EQ(g.At(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(g.At(0, 10), 1000.0f);
  EXPECT_FLOAT_EQ(g.At(10, 5), 500.0f);
  EXPECT_TRUE(std::isnan(g.At(5, 5)));  // the hole
}

TEST(TerrainContour, SamplerRefusesWhatItCannotGrid) {
  FakeElevation src;
  ElevationGrid g;
  EXPECT_EQ(fv::SampleElevationGrid(src, GeoRect{{0, 0}, {1, 1}}, 1, 11, &g).code,
            fv::kInvalidArg);
  EXPECT_EQ(fv::SampleElevationGrid(src, GeoRect{{0, 0}, {1, 1}}, 11, 11, nullptr)
                .code,
            fv::kInvalidArg);
  // Crossing the antimeridian would run every column backwards.
  EXPECT_EQ(
      fv::SampleElevationGrid(src, GeoRect{{0, 179.0}, {1, -179.0}}, 5, 5, &g)
          .code,
      fv::kInvalidArg);
}

// ---------------------------------------------------------------------------
// Real DTED, if it is on this machine.
// ---------------------------------------------------------------------------
TEST(TerrainContour, RealDtedTileTraces) {
  const char* dir = getenv("FVW_TESTDATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "FVW_TESTDATA_DIR not set";
  const std::string root = std::string(dir) + "/dted";
  fv::DtedElevationSource src(root);
  if (!src.Bounds().Contains(GeoPoint{33.5, -84.5}))
    GTEST_SKIP() << "no DTED over the Atlanta test cell";

  // A tenth of a degree at 3-arcsecond posts: 121 x 121 with the overlap post.
  ElevationGrid g;
  ASSERT_TRUE(
      fv::SampleElevationGrid(src, GeoRect{{33.5, -84.5}, {33.6, -84.4}}, 121,
                              121, &g)
          .ok());

  const std::vector<ContourLine> lines = fv::TraceElevationContours(g, 30.48);
  EXPECT_FALSE(lines.empty());
  size_t vertices = 0;
  for (const ContourLine& l : lines) {
    vertices += l.points.size();
    EXPECT_GE(l.points.size(), 2u);
    for (const GeoPoint& p : l.points) {
      EXPECT_GE(p.lat, 33.5 - 1e-9);
      EXPECT_LE(p.lat, 33.6 + 1e-9);
      EXPECT_GE(p.lon, -84.5 - 1e-9);
      EXPECT_LE(p.lon, -84.4 + 1e-9);
    }
  }
  EXPECT_GT(vertices, lines.size());
}

// ---------------------------------------------------------------------------
// Named levels — FalconView's TraceClearanceContours (plan TA2)
// ---------------------------------------------------------------------------

TEST(TerrainContour, NamedLevelsTraceExactlyThoseLevels) {
  const int n = 41;
  ElevationGrid g = MakeGrid(n, n);
  const double mid = (n - 1) / 2.0;
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c)
      Set(&g, r, c,
          static_cast<float>(1000.0 - 40.0 * std::hypot(r - mid, c - mid)));

  // The TA mask's three bands over that cone: one closed ring each, and
  // NOTHING at the multiples in between, which is the whole difference from
  // the interval tracer.
  const std::vector<ContourLine> lines =
      fv::TraceElevationContoursAtLevels(g, {900.0, 700.0, 500.0});
  ASSERT_EQ(lines.size(), 3u);
  for (const ContourLine& l : lines) {
    EXPECT_TRUE(l.closed);
    EXPECT_GE(l.points.size(), 4u);
  }

  // level_index is the index into the CALLER'S list, in the caller's order —
  // so an overlay that asked for warn, caution, OK gets 0, 1, 2 back and can
  // colour by band without comparing doubles.
  for (const ContourLine& l : lines) {
    const double want[3] = {900.0, 700.0, 500.0};
    ASSERT_GE(l.level_index, 0);
    ASSERT_LT(l.level_index, 3);
    EXPECT_DOUBLE_EQ(l.level_m, want[l.level_index]);
  }
  // The ring is bigger the lower the level, on a cone.
  size_t verts[3] = {0, 0, 0};
  for (const ContourLine& l : lines)
    verts[l.level_index] = l.points.size();
  EXPECT_LT(verts[0], verts[1]);
  EXPECT_LT(verts[1], verts[2]);
}

TEST(TerrainContour, NamedLevelsNeedNoSortingAndAgreeWithTheIntervalTracer) {
  const int n = 21;
  ElevationGrid g = MakeGrid(n, n);
  const double mid = (n - 1) / 2.0;
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c)
      Set(&g, r, c,
          static_cast<float>(1000.0 - 40.0 * std::hypot(r - mid, c - mid)));

  // Unsorted, and one level the grid never reaches.
  const std::vector<ContourLine> named =
      fv::TraceElevationContoursAtLevels(g, {600.0, 9000.0, 800.0});
  int at600 = 0, at800 = 0;
  for (const ContourLine& l : named) {
    if (l.level_m == 600.0) ++at600;
    if (l.level_m == 800.0) ++at800;
    EXPECT_NE(l.level_m, 9000.0);  // above the summit: nothing to trace
  }
  EXPECT_EQ(at600, 1);
  EXPECT_EQ(at800, 1);

  // The same two levels through the interval tracer produce the same
  // geometry, which is the claim that made this a refactor rather than a
  // second implementation.
  const std::vector<ContourLine> laddered = fv::TraceElevationContours(g, 200.0);
  for (const ContourLine& a : named) {
    for (const ContourLine& b : laddered) {
      if (b.level_m != a.level_m) continue;
      ASSERT_EQ(a.points.size(), b.points.size());
      for (size_t i = 0; i < a.points.size(); ++i) {
        EXPECT_DOUBLE_EQ(a.points[i].lat, b.points[i].lat);
        EXPECT_DOUBLE_EQ(a.points[i].lon, b.points[i].lon);
      }
    }
  }
}

TEST(TerrainContour, NamedLevelsRefuseAnEmptyListAndAnInvalidGrid) {
  ElevationGrid g = MakeGrid(5, 5);
  EXPECT_TRUE(fv::TraceElevationContoursAtLevels(g, {}).empty());
  ElevationGrid bad;
  EXPECT_TRUE(fv::TraceElevationContoursAtLevels(bad, {100.0}).empty());
}

}  // namespace
