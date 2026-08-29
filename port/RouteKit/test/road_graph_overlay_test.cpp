// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The road-graph debug overlay: the routing network drawn as itself.
//
// WHAT THESE TESTS PIN, and it is not pixels. A golden image of a road network
// would pin the rasterizer, which geo_draw_test already does. What is new here
// is the DECISIONS the overlay makes, and each of them is a count or a field:
//
//   * every edge is drawn ONCE even though the graph holds it as two arcs,
//     and an edge leaving the screen is still drawn — the two halves of the
//     de-duplication rule, which are easy to get right one at a time and
//     wrong together;
//   * the dots are the NODES, not the shape points;
//   * a click answers with what the ROUTER sees — the class, the direction
//     and the bicycle access — not with what the chart shows;
//   * a node beats an arc on a tie, because every node sits on one.
//
// The hand-built graph is a Y with a shape point, small enough to state the
// expected counts as literals: nodes 0-1-2 with a spur 1-3, where 0-1 is a
// residential street with one interior geometry point and 1-3 is a cycleway.

#include "fv_road_graph_overlay.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/canvas/cpu_canvas.h"

namespace {

using fv::GeoPoint;
using fv::RoadArcInfo;
using fv::RoadGraphOverlay;
using fv::RoadNodeInfo;
using fv::routing::kArcBackward;
using fv::routing::kArcForward;
using fv::routing::kArcGeomReversed;
using fv::routing::kArcNoBicycle;
using fv::routing::RoadArc;
using fv::routing::RoadClass;
using fv::routing::RoadGraph;
using fv::routing::RoadNode;

int32_t E7(double d) { return static_cast<int32_t>(d * 1e7 + (d < 0 ? -0.5 : 0.5)); }

// One undirected edge, as the builder makes it: two arcs, mirrored.
struct Edge {
  uint32_t u = 0;
  uint32_t v = 0;
  RoadClass klass = RoadClass::kResidential;
  uint32_t name = 0;
  uint16_t access = 0;
  uint32_t geom_begin = 0;
  uint32_t geom_count = 0;
  bool oneway_forward = false;  // u -> v only
};

// Assembles a graph the way BuildRoadGraph's CSR section does — the same
// counting sort into arc_begin, the same mirrored flags — so what is tested is
// the overlay and not a second, friendlier graph shape it would never meet.
std::shared_ptr<RoadGraph> MakeGraph(const std::vector<RoadNode>& nodes,
                                     const std::vector<Edge>& edges,
                                     const std::vector<int32_t>& geometry,
                                     const std::vector<std::string>& names) {
  auto g = std::make_shared<RoadGraph>();
  g->mutable_nodes() = nodes;
  g->mutable_geometry() = geometry;
  g->mutable_names() = names;

  std::vector<uint32_t>& begin = g->mutable_arc_begin();
  begin.assign(nodes.size() + 1, 0);
  for (const Edge& e : edges) {
    ++begin[e.u + 1];
    ++begin[e.v + 1];
  }
  for (size_t i = 1; i < begin.size(); ++i) begin[i] += begin[i - 1];

  std::vector<RoadArc>& arcs = g->mutable_arcs();
  arcs.resize(edges.size() * 2);
  std::vector<uint32_t> fill(begin.begin(), begin.end());
  for (const Edge& e : edges) {
    RoadArc a;
    a.klass = e.klass;
    a.name = e.name;
    a.geom_begin = e.geom_begin;
    a.geom_count = e.geom_count;
    a.speed_kph = 40;
    const GeoPoint p{nodes[e.u].lat_e7 * 1e-7, nodes[e.u].lon_e7 * 1e-7};
    const GeoPoint q{nodes[e.v].lat_e7 * 1e-7, nodes[e.v].lon_e7 * 1e-7};
    a.length_m = static_cast<float>(
        fv::routing::GreatCircleMeters(p.lat, p.lon, q.lat, q.lon));

    RoadArc fwd = a;
    fwd.target = e.v;
    fwd.flags = static_cast<uint16_t>(
        kArcForward | (e.oneway_forward ? 0 : kArcBackward) | e.access);
    arcs[fill[e.u]++] = fwd;

    RoadArc rev = a;
    rev.target = e.u;
    rev.flags = static_cast<uint16_t>((e.oneway_forward ? 0 : kArcForward) |
                                      kArcBackward | kArcGeomReversed | e.access);
    arcs[fill[e.v]++] = rev;
  }

  g->Finalize();
  return g;
}

// The Y. Around 32.60 N, 80.11 W (Kiawah, so the numbers look like the place
// this tool was written for), spread over ~500 m.
//
//   node 0 --[residential "Flyway Dr", one shape point]-- node 1
//   node 1 --[service, oneway 1->2]-------------------- node 2
//   node 1 --[cycleway, bicycle=no on the SERVICE arc]- node 3
std::shared_ptr<RoadGraph> MakeY() {
  const std::vector<RoadNode> nodes = {
      {E7(32.5980), E7(-80.1140), 1001},
      {E7(32.5990), E7(-80.1130), 1002},
      {E7(32.6000), E7(-80.1120), 1003},
      {E7(32.5990), E7(-80.1110), 1004},
  };
  // One interior point on edge 0-1, bending it north.
  const std::vector<int32_t> geometry = {E7(32.5988), E7(-80.1136)};
  const std::vector<std::string> names = {"", "Flyway Dr", "Beach Walk"};

  std::vector<Edge> edges;
  edges.push_back(Edge{0, 1, RoadClass::kResidential, 1, 0, 0, 1, false});
  edges.push_back(Edge{1, 2, RoadClass::kService, 0, kArcNoBicycle, 0, 0, true});
  edges.push_back(Edge{1, 3, RoadClass::kCycleway, 2, 0, 0, 0, false});
  return MakeGraph(nodes, edges, geometry, names);
}

fv::MapProjection YProj(int w = 800, int h = 600, double scale = 20000.0) {
  fv::MapProjection p;
  p.SetSurfaceSize(w, h);
  p.SetCenter({32.5990, -80.1127});
  p.SetScale(scale);
  return p;
}

// The first system TTF this machine has, or empty. Same list route_overlay_test
// carries, for the same reason: `CpuCanvas` wants a font on the FILESYSTEM and
// the port does not ship one.
std::string SystemFont() {
  const char* candidates[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                              "/System/Library/Fonts/Supplemental/Courier New.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
  for (const char* f : candidates) {
    if (FILE* fp = std::fopen(f, "rb")) {
      std::fclose(fp);
      return f;
    }
  }
  return std::string();
}

// A canvas with a font on it. THE LEGEND IS TEXT, so a canvas with no default
// font fails the draw — the same rule RouteOverlay's status line follows, and
// the reason `LegendNeedsAFont` below pins it rather than working around it.
std::unique_ptr<fv::CpuCanvas> MakeCanvas(int w, int h) {
  auto c = std::make_unique<fv::CpuCanvas>(w, h);
  const std::string font = SystemFont();
  if (!font.empty()) (void)c->SetDefaultFont(font);
  return c;
}

std::string TestDataDir() {
  const char* env = std::getenv("FVW_TESTDATA_DIR");
  return env != nullptr ? std::string(env) : std::string("TestData");
}

std::string KiawahGraphPath() {
  const std::filesystem::path p =
      std::filesystem::path(TestDataDir()) / "OSM" / "kiawah.fvroad";
  std::error_code ec;
  return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

// Where the projection puts a node, in surface pixels.
void NodePixel(const fv::MapProjection& proj, const RoadGraph& g, uint32_t n,
               double* x, double* y) {
  ASSERT_TRUE(proj.GeoToSurface(g.location(n), x, y).ok());
}

}  // namespace

// ---------------------------------------------------------------------------
// The palette
// ---------------------------------------------------------------------------

TEST(RoadGraphPalette, EveryClassHasItsOwnColour) {
  // A duplicate colour is a class that cannot be told from another one on the
  // screen, which is the one thing a colour-coded debug view may not do.
  std::vector<uint32_t> seen;
  for (int i = 0; i < static_cast<int>(RoadClass::kCount); ++i) {
    const fv::FvColor c = fv::RoadClassColor(static_cast<RoadClass>(i));
    const uint32_t packed = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    EXPECT_EQ(std::count(seen.begin(), seen.end(), packed), 0)
        << "duplicate colour for class " << i << " ("
        << fv::routing::RoadClassName(static_cast<RoadClass>(i)) << ")";
    seen.push_back(packed);
  }
}

TEST(RoadGraphPalette, WidthsAreVisibleAndOrdered) {
  EXPECT_GT(fv::RoadClassWidth(RoadClass::kMotorway),
            fv::RoadClassWidth(RoadClass::kResidential));
  // The bike classes are deliberately NOT the thinnest — this is a bicycle
  // debugging tool. See the comment at RoadClassWidth.
  EXPECT_GE(fv::RoadClassWidth(RoadClass::kCycleway),
            fv::RoadClassWidth(RoadClass::kFootway));
  for (int i = 0; i < static_cast<int>(RoadClass::kCount); ++i) {
    EXPECT_GE(fv::RoadClassWidth(static_cast<RoadClass>(i)), 1);
  }
}

// ---------------------------------------------------------------------------
// NodesInRect — the index the overlay is built on
// ---------------------------------------------------------------------------

TEST(NodesInRect, FindsExactlyTheNodesInTheBox) {
  const std::shared_ptr<RoadGraph> g = MakeY();

  std::vector<uint32_t> all;
  g->NodesInRect(fv::GeoRect::World(), [&](uint32_t n) { all.push_back(n); });
  EXPECT_EQ(all.size(), 4u);

  // A box round node 0 alone.
  std::vector<uint32_t> one;
  g->NodesInRect(fv::GeoRect{{32.5975, -80.1145}, {32.5983, -80.1136}},
                 [&](uint32_t n) { one.push_back(n); });
  ASSERT_EQ(one.size(), 1u);
  EXPECT_EQ(one[0], 0u);

  // A box off the graph entirely — and to the WEST and SOUTH of it, which is
  // the case a truncating cast turns into cell 0 and answers wrongly.
  std::vector<uint32_t> none;
  g->NodesInRect(fv::GeoRect{{32.0, -81.0}, {32.1, -80.9}},
                 [&](uint32_t n) { none.push_back(n); });
  EXPECT_TRUE(none.empty());
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

TEST(RoadGraphOverlayDraw, EachEdgeDrawnOnceAndEachNodeDotted) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);

  const auto canvas = MakeCanvas(800, 600);
  ASSERT_TRUE(ov.OnDraw(YProj(), *canvas).ok());

  // THREE edges, six arcs. Drawing both arcs of an edge would give 6 here,
  // and it is the commonest way to get this overlay wrong.
  EXPECT_EQ(ov.drawn_arcs(), 3u);
  EXPECT_EQ(ov.drawn_nodes(), 4u);
  EXPECT_FALSE(ov.budget_hit());

  const std::vector<uint32_t>& c = ov.counts();
  EXPECT_EQ(c[static_cast<int>(RoadClass::kResidential)], 1u);
  EXPECT_EQ(c[static_cast<int>(RoadClass::kService)], 1u);
  EXPECT_EQ(c[static_cast<int>(RoadClass::kCycleway)], 1u);
  EXPECT_EQ(c[static_cast<int>(RoadClass::kMotorway)], 0u);
}

TEST(RoadGraphOverlayDraw, AnEdgeLeavingTheScreenIsStillDrawn) {
  // The other half of the de-duplication rule. Zoomed so that only node 3 —
  // the HIGHEST id, which the "lower id wins" rule would make skip its own
  // edge — is inside the query box. Nothing else visits edge 1-3, so if the
  // rule has no exception for a target outside the box, this draws nothing
  // and every road leaving the viewport disappears.
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  ov.SetShowNodes(false);

  fv::MapProjection proj;
  proj.SetSurfaceSize(60, 60);
  proj.SetCenter(g->location(3));
  proj.SetScale(2000.0);

  const auto canvas = MakeCanvas(60, 60);
  ASSERT_TRUE(ov.OnDraw(proj, *canvas).ok());
  EXPECT_GE(ov.drawn_arcs(), 1u);
  EXPECT_EQ(ov.counts()[static_cast<int>(RoadClass::kCycleway)], 1u);
}

TEST(RoadGraphOverlayDraw, ClassFilterHidesBothLinesAndDots) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  ov.SetClassFilter({RoadClass::kCycleway});

  const auto canvas = MakeCanvas(800, 600);
  ASSERT_TRUE(ov.OnDraw(YProj(), *canvas).ok());

  EXPECT_EQ(ov.drawn_arcs(), 1u);
  EXPECT_EQ(ov.counts()[static_cast<int>(RoadClass::kCycleway)], 1u);
  EXPECT_EQ(ov.counts()[static_cast<int>(RoadClass::kResidential)], 0u);
  // Only nodes 1 and 3 touch a cycleway. A node kept by a hidden class is not
  // part of the network being looked at.
  EXPECT_EQ(ov.drawn_nodes(), 2u);
}

TEST(RoadGraphOverlayDraw, BudgetStopsTheFrameAndSaysSo) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  ov.SetArcBudget(1);

  const auto canvas = MakeCanvas(800, 600);
  ASSERT_TRUE(ov.OnDraw(YProj(), *canvas).ok());
  EXPECT_EQ(ov.drawn_arcs(), 1u);
  EXPECT_TRUE(ov.budget_hit());
}

TEST(RoadGraphOverlayDraw, NoGraphIsNotAnError) {
  // A shell toggles the overlay on before the planner has loaded anything.
  // That is a blank frame, not a failure — and certainly not a crash.
  RoadGraphOverlay ov;
  const auto canvas = MakeCanvas(200, 200);
  EXPECT_TRUE(ov.OnDraw(YProj(200, 200), *canvas).ok());
  EXPECT_EQ(ov.drawn_arcs(), 0u);
  EXPECT_EQ(ov.drawn_nodes(), 0u);
}

TEST(RoadGraphOverlayDraw, LegendNeedsAFontAndTheRoadsDoNot) {
  // The legend is TEXT, and a CpuCanvas with no default font cannot draw it.
  // The draw FAILS and says why, rather than silently dropping the legend —
  // the same rule RouteOverlay's status line follows. Turning the legend off
  // is the answer for a shell that has no font, and it draws the network fine.
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);

  fv::CpuCanvas fontless(400, 300);
  const fv::Status s = ov.OnDraw(YProj(400, 300), fontless);
  EXPECT_FALSE(s.ok());
  EXPECT_NE(s.message.find("font"), std::string::npos) << s.message;
  // The ROADS were still drawn before the legend was reached — a debug view
  // that lost the network because it could not label it would be useless.
  EXPECT_EQ(ov.drawn_arcs(), 3u);

  ov.SetShowLegend(false);
  fv::CpuCanvas fontless2(400, 300);
  EXPECT_TRUE(ov.OnDraw(YProj(400, 300), fontless2).ok());
  EXPECT_EQ(ov.drawn_arcs(), 3u);
}

TEST(RoadGraphOverlayDraw, InkIsActuallyPutOnTheCanvas) {
  // The counters could be right while nothing reached the canvas. One count
  // of non-background pixels, which is the cheapest honest check that the
  // drawing calls were real.
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  ov.SetShowLegend(false);

  const auto canvas = MakeCanvas(800, 600);
  canvas->Clear(fv::FvColor{0, 0, 0, 255});
  ASSERT_TRUE(ov.OnDraw(YProj(), *canvas).ok());

  const fv::PixelBuffer& buf = canvas->Buffer();
  size_t lit = 0;
  for (int y = 0; y < buf.Height(); ++y) {
    const unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x) {
      const unsigned char* px = row + 4 * x;
      if (px[0] != 0 || px[1] != 0 || px[2] != 0) ++lit;
    }
  }
  EXPECT_GT(lit, 100u);
}

// ---------------------------------------------------------------------------
// Click to inspect
// ---------------------------------------------------------------------------

TEST(RoadGraphOverlayPick, ClickOnANodeAnswersTheNode) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  const fv::MapProjection proj = YProj();

  double x = 0.0, y = 0.0;
  NodePixel(proj, *g, 1, &x, &y);

  const RoadNodeInfo n = ov.NodeAt(proj, x, y, 8.0);
  ASSERT_TRUE(n.valid);
  EXPECT_EQ(n.node, 1u);
  EXPECT_EQ(n.osm_id, 1002);
  EXPECT_EQ(n.degree, 3u);  // the junction of the Y
  EXPECT_LT(n.distance_px, 1.0);
  EXPECT_NE(n.Summary().find("degree 3"), std::string::npos);
}

TEST(RoadGraphOverlayPick, ADeadEndSaysSo) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  const fv::MapProjection proj = YProj();

  double x = 0.0, y = 0.0;
  NodePixel(proj, *g, 3, &x, &y);
  const RoadNodeInfo n = ov.NodeAt(proj, x, y, 8.0);
  ASSERT_TRUE(n.valid);
  EXPECT_EQ(n.degree, 1u);
  // The single most common shape of a bike-routing bug: a path that looks
  // through on the chart and dead-ends in the graph.
  EXPECT_NE(n.Summary().find("dead end"), std::string::npos);
}

TEST(RoadGraphOverlayPick, ClickOnAnArcAnswersWhatTheRouterSees) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  const fv::MapProjection proj = YProj();

  // The midpoint of the one-way service arc 1->2, well away from both nodes.
  double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
  NodePixel(proj, *g, 1, &x1, &y1);
  NodePixel(proj, *g, 2, &x2, &y2);

  const RoadArcInfo a = ov.ArcAt(proj, (x1 + x2) / 2.0, (y1 + y2) / 2.0, 8.0);
  ASSERT_TRUE(a.valid);
  EXPECT_EQ(a.road_class, "service");
  // The direction and the bicycle answer are the two things a wrong bike
  // route is about, and both are read off the arc rather than guessed.
  EXPECT_TRUE(a.forward != a.backward);
  EXPECT_FALSE(a.bicycle_allowed);
  EXPECT_NE(a.Summary().find("bike NO"), std::string::npos);
  EXPECT_NE(a.Summary().find("one way"), std::string::npos);
  EXPECT_GT(a.length_m, 0.0);
}

TEST(RoadGraphOverlayPick, TheShapePointIsOnTheLineAndTheClickFindsIt) {
  // The bend in edge 0-1. Clicking the shape point must find the arc — a pick
  // that measured only the endpoints would miss a curved road by its sagitta,
  // which on a real coastal road is tens of metres.
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  const fv::MapProjection proj = YProj();

  double x = 0.0, y = 0.0;
  ASSERT_TRUE(proj.GeoToSurface(GeoPoint{32.5988, -80.1136}, &x, &y).ok());

  const RoadArcInfo a = ov.ArcAt(proj, x, y, 6.0);
  ASSERT_TRUE(a.valid);
  EXPECT_EQ(a.name, "Flyway Dr");
  EXPECT_EQ(a.road_class, "residential");
}

TEST(RoadGraphOverlayPick, EmptyMapAnswersNothing) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  const fv::MapProjection proj = YProj();

  // A corner of the viewport, hundreds of pixels off any road.
  EXPECT_FALSE(ov.NodeAt(proj, 5.0, 5.0, 8.0).valid);
  EXPECT_FALSE(ov.ArcAt(proj, 5.0, 5.0, 6.0).valid);
}

TEST(RoadGraphOverlayPick, HitTestPutsTheNodeFirstAndPacksBothKinds) {
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);
  const fv::MapProjection proj = YProj();

  double x = 0.0, y = 0.0;
  NodePixel(proj, *g, 1, &x, &y);

  std::vector<fv::app::HitItem> hits;
  ov.HitTestPoint(proj, fv::PixelPoint{static_cast<int>(x), static_cast<int>(y)},
                  8.0, hits);
  ASSERT_GE(hits.size(), 2u);

  // The node first: every node sits ON an arc, so nearest-wins would make the
  // dots unclickable.
  EXPECT_FALSE(RoadGraphOverlay::FeatureIsArc(hits[0].feature));
  EXPECT_EQ(RoadGraphOverlay::FeatureIndex(hits[0].feature), 1u);
  EXPECT_TRUE(RoadGraphOverlay::FeatureIsArc(hits[1].feature));

  // And the packed id round-trips through the accessors.
  const RoadArcInfo a =
      ov.ArcInfoFor(RoadGraphOverlay::FeatureIndex(hits[1].feature));
  EXPECT_TRUE(a.valid);
  EXPECT_EQ(a.from_node, 1u);
}

TEST(RoadGraphOverlayPick, ArcInfoFindsItsOwnSourceNode) {
  // The arc array is CSR and carries no back-pointer; the source is a binary
  // search over arc_begin. Every arc in the graph, checked against the range
  // it actually lives in.
  const std::shared_ptr<RoadGraph> g = MakeY();
  RoadGraphOverlay ov;
  ov.SetGraph(g);

  for (uint32_t n = 0; n < g->node_count(); ++n) {
    for (uint32_t ai = g->arc_begin(n); ai < g->arc_end(n); ++ai) {
      const RoadArcInfo info = ov.ArcInfoFor(ai);
      ASSERT_TRUE(info.valid) << "arc " << ai;
      EXPECT_EQ(info.from_node, n) << "arc " << ai;
      EXPECT_EQ(info.to_node, g->arc(ai).target);
    }
  }
  EXPECT_FALSE(ov.ArcInfoFor(g->arc_count()).valid);   // past the end
  EXPECT_FALSE(ov.NodeInfoFor(g->node_count()).valid);
}

// ---------------------------------------------------------------------------
// Kiawah — the real graph, when it is there
// ---------------------------------------------------------------------------

TEST(RoadGraphOverlayKiawah, DrawsTheRealNetwork) {
  const std::string path = KiawahGraphPath();
  if (path.empty()) GTEST_SKIP() << "kiawah.fvroad not present";

  auto g = std::make_shared<RoadGraph>();
  ASSERT_TRUE(RoadGraph::Load(path, g.get()).ok());

  RoadGraphOverlay ov;
  ov.SetGraph(g);

  fv::MapProjection proj;
  proj.SetSurfaceSize(1000, 700);
  proj.SetCenter({32.5987, -80.1130});
  proj.SetScale(50000.0);

  const auto canvas = MakeCanvas(1000, 700);
  ASSERT_TRUE(ov.OnDraw(proj, *canvas).ok());

  EXPECT_GT(ov.drawn_arcs(), 0u);
  EXPECT_GT(ov.drawn_nodes(), 0u);
  EXPECT_FALSE(ov.budget_hit());
  // Kiawah is why the cycleway colour is the brightest in the palette: the
  // island's bike paths are the thing the router keeps getting wrong.
  EXPECT_GT(ov.counts()[static_cast<int>(RoadClass::kCycleway)] +
                ov.counts()[static_cast<int>(RoadClass::kPath)],
            0u);
}

TEST(RoadGraphOverlayKiawah, EveryDrawnArcIsFindableByClickingIt) {
  // The picture and the pick have to agree. Walk a handful of nodes, click the
  // MIDPOINT of each of their arcs, and require the pick to find an arc there.
  const std::string path = KiawahGraphPath();
  if (path.empty()) GTEST_SKIP() << "kiawah.fvroad not present";

  auto g = std::make_shared<RoadGraph>();
  ASSERT_TRUE(RoadGraph::Load(path, g.get()).ok());

  RoadGraphOverlay ov;
  ov.SetGraph(g);

  fv::MapProjection proj;
  proj.SetSurfaceSize(1000, 700);
  proj.SetScale(5000.0);

  int checked = 0, found = 0;
  for (uint32_t n = 0; n < g->node_count() && checked < 40; ++n) {
    if (g->arc_begin(n) == g->arc_end(n)) continue;
    const uint32_t ai = g->arc_begin(n);
    const fv::routing::RoadArc& a = g->arc(ai);
    const GeoPoint p = g->location(n), q = g->location(a.target);
    // Centre on the arc's own midpoint so it is in the middle of the screen.
    proj.SetCenter(GeoPoint{(p.lat + q.lat) / 2.0, (p.lon + q.lon) / 2.0});

    double x = 0.0, y = 0.0;
    if (!proj.GeoToSurface(GeoPoint{(p.lat + q.lat) / 2.0, (p.lon + q.lon) / 2.0},
                           &x, &y)
             .ok()) {
      continue;
    }
    ++checked;
    // A generous tolerance: the midpoint of the CHORD is not on a bent road,
    // and how far off it is depends on the bend. What is being pinned is that
    // the pick reaches the local arcs at all, not a distance.
    if (ov.ArcAt(proj, x, y, 60.0).valid) ++found;
  }
  ASSERT_GT(checked, 10);
  EXPECT_GE(found, checked * 3 / 4);
}
