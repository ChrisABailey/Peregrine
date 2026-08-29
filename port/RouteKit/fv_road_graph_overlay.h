// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RouteKit/fv_road_graph_overlay.h — the routing network, drawn as itself.
//
// A DEBUG VIEW OF THE GRAPH THE ROUTER ACTUALLY HAS, which is a different
// object from the map underneath it and is the whole reason this exists. When
// a bicycle route goes somewhere absurd the question is never "what does the
// chart say" — it is "what does the GRAPH say", and until now nothing could
// show that. The two disagree in exactly the ways that matter: a cycleway
// that is drawn on the chart and absent from the graph, two halves of a path
// that look joined and share no node, a service road tagged `bicycle=no` that
// the chart renders identically to the one beside it.
//
// So this draws the graph and nothing else:
//
//   * every ARC in view as its own geometry — the road's shape points, not a
//     chord between its endpoints — coloured BY ROAD CLASS, with a legend
//     naming the classes actually on screen and how many arcs of each;
//   * a DOT at every graph NODE. Nodes are junctions and way-ends only —
//     shape points are edge geometry and are deliberately not dotted — so the
//     dots are the topology, and a gap between two dots that ought to be one
//     dot IS the bug. This is also what snapping sees: `NearestNode` picks a
//     dot, never a point along a road, which is how a route start ends up
//     138 m away on a golf cart path (see fv_road_graph.h).
//
// IT IS A DEBUGGING AID AND SEVERAL THINGS FOLLOW FROM THAT. It is not
// persisted (no `.fv*` document — there is nothing to save that the `.fvroad`
// does not already hold), it answers no snap (snapping to the thing you are
// debugging would be circular), and it draws EVERY arc in view up to a budget
// rather than thinning by scale: a debug view that quietly dropped the arc
// you were looking for would be worse than useless.
//
// WHY IT LIVES IN RouteKit and not in fvkit or in Routing: the same invariant
// the route overlay is here for. It needs GeoDraw and `Overlay` from fvkit,
// and `RoadGraph` from Routing, and fvkit may not link Routing.
//
// WHERE THE GRAPH COMES FROM. Either the shell's `RoutePlanner` — the SAME
// graph the router routes over, which is the only graph worth debugging — or
// a graph handed over directly, for a test or for looking at a second
// `.fvroad` beside the first. The planner wins when both are set, because a
// picture of a graph nobody routes on answers the wrong question.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fv_road_graph.h"
#include "fv_route_planner.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/search.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"

namespace fv {

// The colour a class is drawn in, and the width it is drawn at.
//
// FREE FUNCTIONS rather than a table private to the overlay, because the
// LEGEND is not the only thing that wants them: a shell that lists the classes
// in its own UI has to agree with the picture, and two palettes that drift
// apart make the picture lie.
//
// The palette is grouped rather than a 22-step ramp, because the classes are
// grouped: the eye should be able to say "that is a motorway-ish thing" and
// "that is a bike-ish thing" before it reads the legend at all.
//
//   reds/oranges   the motorway..trunk family — fast, motor-only
//   yellows        primary/secondary/tertiary — the through roads
//   greys          unclassified/residential/living_street/service/track
//   green/pink     the two classes a BICYCLE route is made of — cycleway and
//                  path — deliberately the loudest things on the screen
//   violets        the rest of the walking classes: footway, pedestrian, steps
//   cyan           ferry, which is not a road at all
//
// A `_link` is the same hue as its parent, one step darker, so a slip road
// reads as belonging to the road it leaves.
FvColor RoadClassColor(routing::RoadClass klass);

// Line width in pixels. Importance, not width on the ground: a motorway is
// 5 px so it reads through a dense residential mesh at 3 px, and a footway is
// 2 px so a thousand of them do not become a solid wash.
int RoadClassWidth(routing::RoadClass klass);

// ---------------------------------------------------------------------------
// What a click landed on
// ---------------------------------------------------------------------------

// One arc, unpacked into the answers a person debugging a route asks. Every
// field is read off the graph — nothing here is computed by the overlay — so
// what this says is what the router sees.
struct RoadArcInfo {
  bool valid = false;

  uint32_t arc = routing::kNoArc;  // index into the graph's arc array
  uint32_t from_node = 0;
  uint32_t to_node = 0;
  int64_t from_osm_id = 0;
  int64_t to_osm_id = 0;

  std::string name;        // road name, or empty
  std::string road_class;  // the OSM tag value: "cycleway", "residential", ...
  double length_m = 0.0;
  int speed_kph = 0;
  uint16_t flags = 0;

  bool forward = false;
  bool backward = false;
  bool bicycle_allowed = false;
  bool foot_allowed = false;
  bool motor_vehicle_allowed = false;
  bool bicycle_private = false;
  bool foot_private = false;
  bool motor_vehicle_private = false;
  bool tolled = false;
  bool ferry = false;

  double distance_px = 0.0;  // how far the click was from the drawn line

  // One line for a status bar, e.g.
  //   "Flyway Dr - residential, 214 m, 40 km/h, both ways, bike ok"
  // Empty when `valid` is false.
  std::string Summary() const;
};

// One node, and the thing a route start actually snaps to.
struct RoadNodeInfo {
  bool valid = false;

  uint32_t node = 0;
  int64_t osm_id = 0;
  GeoPoint position;
  uint32_t degree = 0;      // arcs leaving it
  bool restricted = false;  // the via node of a turn restriction

  double distance_px = 0.0;

  // "node 4821 (osm 3390512) - degree 3" and, when it applies, ", restricted".
  std::string Summary() const;
};

// ---------------------------------------------------------------------------
// The overlay
// ---------------------------------------------------------------------------

class RoadGraphOverlay : public Overlay,
                         public app::HitTest,
                         public app::SearchProvider {
 public:
  explicit RoadGraphOverlay(std::string name = "Road graph");
  ~RoadGraphOverlay() override;

  // A STATIC type (A6): at most one, no document, created by the Overlays
  // menu and removed by it. There is no extension because there is no file
  // this overlay writes — the `.fvroad` is an input.
  static const char kTypeId[];  // "fv.roadgraph"

  app::HitTest* AsHitTest() override { return this; }
  app::SearchProvider* AsSearch() override { return this; }

  // --- where the roads come from -------------------------------------------

  // The shell's planner, BORROWED and possibly null. Its graph is preferred
  // over `SetGraph`'s whenever it has one — see the header note.
  void SetPlanner(const RoutePlanner* planner) { planner_ = planner; }
  const RoutePlanner* planner() const { return planner_; }

  // A graph directly, for a test or a second `.fvroad`. Shared and not
  // copied: a graph is tens of megabytes and the snapper indexes the same one.
  void SetGraph(std::shared_ptr<const routing::RoadGraph> graph) {
    graph_ = std::move(graph);
  }

  // The graph this overlay is actually drawing, or null when neither source
  // has one. Never loads anything — see `EnsureGraph`.
  std::shared_ptr<const routing::RoadGraph> graph() const;

  // Load the planner's graph now, if it has not been loaded. Separate from
  // `graph()` because loading a continent takes seconds and OnDraw is a frame:
  // a shell calls this when the user asks for the overlay, not when it paints.
  Status EnsureGraph();

  // --- what is drawn -------------------------------------------------------

  // The dots. On by default — they are half the point of the overlay — but a
  // dense extract at low zoom is more legible without them.
  void SetShowNodes(bool on) { show_nodes_ = on; }
  bool show_nodes() const { return show_nodes_; }

  void SetShowEdges(bool on) { show_edges_ = on; }
  bool show_edges() const { return show_edges_; }

  // The legend: which classes are on screen, and how many arcs of each.
  void SetShowLegend(bool on) { show_legend_ = on; }
  bool show_legend() const { return show_legend_; }

  // Draw only these classes. Empty (the default) means all of them. This is
  // how "show me only the cycleways" is asked, and it filters the DOTS too —
  // a node kept only by a hidden class is not part of the network being
  // looked at. It does NOT filter the SEARCH: a class filter is a rule about
  // what is drawn, and search deliberately ignores what is drawn (the
  // `visible_only` note in fvkit/app/search.h).
  void SetClassFilter(std::vector<routing::RoadClass> classes);
  const std::vector<routing::RoadClass>& class_filter() const {
    return class_filter_;
  }
  bool ClassShown(routing::RoadClass klass) const;

  // The most arcs one frame will draw. A guard against asking for a
  // continent, not a thinning rule: over budget, the frame stops early and
  // the legend says so, which is honest in a way that silently dropping
  // every second arc would not be.
  void SetArcBudget(uint32_t arcs) { arc_budget_ = arcs; }
  uint32_t arc_budget() const { return arc_budget_; }

  // Dot size in pixels (the side of the square). Device pixels, like every
  // other width in the drawing layer.
  void SetNodeSizePx(int px) { node_px_ = px < 1 ? 1 : px; }
  int node_size_px() const { return node_px_; }

  // --- what the last frame contained ---------------------------------------
  // Counters, for a test and for the legend. All reset at the top of OnDraw.

  uint32_t drawn_arcs() const { return drawn_arcs_; }
  uint32_t drawn_nodes() const { return drawn_nodes_; }
  bool budget_hit() const { return budget_hit_; }

  // Arcs drawn of each class last frame, indexed by `RoadClass`. Sized
  // kCount, so `counts()[int(RoadClass::kCycleway)]` is a legal read.
  const std::vector<uint32_t>& counts() const { return counts_; }

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  // --- click to inspect ----------------------------------------------------

  // Both answer about the graph, at the projection the last frame drew with —
  // the same rule RouteOverlay's pick follows, and for the same reason: a
  // pick has to agree with the picture. An overlay nobody has drawn answers
  // nothing, and both return `valid == false` rather than an error, because
  // "you clicked on empty map" is an answer.
  //
  // NOT a scan. The click becomes a small geographic box, the grid answers
  // which nodes are in it, and only those nodes' arcs are measured — so this
  // costs the same on Kiawah and on the whole of the south.
  RoadArcInfo ArcAt(const MapProjection& proj, double x, double y,
                    double tolerance_px = 6.0) const;
  RoadNodeInfo NodeAt(const MapProjection& proj, double x, double y,
                      double tolerance_px = 8.0) const;

  // The last projection drawn with, for a shell that wants to ask the two
  // above without threading a projection through itself.
  bool has_projection() const { return have_proj_; }
  const MapProjection& last_projection() const { return last_proj_; }

  // --- HitTest -------------------------------------------------------------

  // A NODE WINS A TIE WITH AN ARC, and it is not a distance rule: every node
  // sits ON at least one arc, so the arc is always within a pixel or two of
  // the dot, and "nearest" would make the dots unclickable. The dot is the
  // smaller and more specific target, so it goes first and the arcs follow.
  //
  // `feature` is packed rather than minted (the exception A5's rule allows for
  // an id that already IS a number): a node id and an arc index are the
  // graph's own identifiers, stable for the life of the graph, and minting
  // handles for two million arcs to hand back numbers we were given would be
  // a map the size of the graph. The top bit says which kind it is.
  void HitTestPoint(const MapProjection& proj, PixelPoint p,
                    double tolerance_px, std::vector<app::HitItem>& out) override;

  // The top bit of a `HitItem::feature` from this overlay: set for an arc,
  // clear for a node. The rest is the graph's own index.
  static constexpr uint64_t kFeatureArcBit = 1ull << 63;
  static bool FeatureIsArc(uint64_t f) { return (f & kFeatureArcBit) != 0; }
  static uint32_t FeatureIndex(uint64_t f) {
    return static_cast<uint32_t>(f & ~kFeatureArcBit);
  }

  // The arc or node a `feature` names, looked up in the CURRENT graph. Both
  // come back invalid when the graph has been swapped for a smaller one since
  // the id was minted, which is the honest answer — the thing it named is
  // gone.
  RoadArcInfo ArcInfoFor(uint32_t arc) const;
  RoadNodeInfo NodeInfoFor(uint32_t node) const;

  // --- SearchProvider (S4) -------------------------------------------------
  //
  // THE ROUTABLE ANSWER TO "WHERE IS X". The same road is very often in the
  // vector pack as well, and both rows are shown rather than deduped
  // (search-plan-COMPLETE.md, "Explicitly deferred"): the tile knows what the
  // road is
  // CALLED and this knows what a bicycle may do on it, and they are not the
  // same fact. The `detail` line says which is which.
  //
  // The label is `RoadArc::name`, which is an index into the graph's own name
  // table — and that table is why a whole-graph text search is affordable. It
  // holds each distinct name ONCE, so a text query matches the TABLE first
  // (thousands of strings) and the arc pass that follows is an integer test
  // per arc. Without text there is no such shortcut, so a spatial query goes
  // through the grid index (`NodesInRect`) instead, and a query with NEITHER
  // finds nothing — the whole graph is not an answer to a search box.
  //
  // ONE ROAD IS ONE ROW, using the same `MergeFeatureRows` the vector scan and
  // the staged name index use (S3). It matters more here than there: OSM
  // splits a way at every junction, so the graph holds "Ruddy Turnstone" as
  // fifteen arcs, and each of those is TWO arcs because an edge is stored from
  // both ends. Merging by title, class and proximity collapses all of it,
  // including the mirrored pair, which is why there is no separate
  // de-duplication step here the way there is in the draw.
  //
  // RELEVANCE IS THE ROAD CLASS, which `RoadClass`'s own ordering already
  // is — the enum is declared in descending importance and that ordering is
  // part of the file format. It is the same free ranking a tile pyramid's
  // min_zoom gives tier 2: a capped search for "main" answers with the
  // primary road before the service alley behind it.
  //
  // The feature id is PACKED exactly as HitTestPoint packs it (an arc, so
  // `kFeatureArcBit` is set), so a search result can be handed straight to
  // `FeatureIndex` + `ArcInfoFor` and gives the same summary a click does.
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override;

  // How far apart two pieces of one named road may lie and still be one row.
  // Default 100 m, matching VectorMapOverlay's — the number means the same
  // thing in both places and a user comparing the two answers should not have
  // to know which provider merged more eagerly. Negative disables merging.
  void SetSearchMergeGapMeters(double m) { search_merge_gap_m_ = m; }
  double search_merge_gap_meters() const { return search_merge_gap_m_; }

  // The most arcs one search will look at. A guard against a text query on a
  // continent, not a thinning rule — and unlike the draw's budget it is not
  // reported anywhere, because a search that stopped early still answers with
  // the most important roads it found (see the class ranking above).
  void SetSearchArcBudget(uint32_t arcs) { search_arc_budget_ = arcs; }
  uint32_t search_arc_budget() const { return search_arc_budget_; }

  // Arcs examined and rows returned by the last search. Diagnostics, the pair
  // VectorMapOverlay keeps for the same reason: they tell "there is no such
  // road" apart from "the budget ran out before it was reached".
  uint32_t last_search_arcs() const { return last_search_arcs_; }
  uint32_t last_search_results() const { return last_search_results_; }

 private:
  // The geographic box the frame draws from: the viewport grown by a margin,
  // because an arc whose two endpoints are both off screen can still cross it
  // and a query of the bare viewport would drop it.
  GeoRect QueryRect(const MapProjection& proj) const;

  // The arc's full line — its own endpoints with the shape points between,
  // in the arc's direction of travel.
  std::vector<GeoPoint> ArcLine(const routing::RoadGraph& g, uint32_t from,
                               const routing::RoadArc& a) const;

  const RoutePlanner* planner_ = nullptr;
  std::shared_ptr<const routing::RoadGraph> graph_;

  bool show_nodes_ = true;
  bool show_edges_ = true;
  bool show_legend_ = true;
  int node_px_ = 5;
  uint32_t arc_budget_ = 120000;
  std::vector<routing::RoadClass> class_filter_;

  uint32_t drawn_arcs_ = 0;
  uint32_t drawn_nodes_ = 0;
  bool budget_hit_ = false;
  std::vector<uint32_t> counts_;

  MapProjection last_proj_;
  bool have_proj_ = false;

  double search_merge_gap_m_ = 100.0;
  uint32_t search_arc_budget_ = 2000000;
  uint32_t last_search_arcs_ = 0;
  uint32_t last_search_results_ = 0;
};

// Registers `fv.roadgraph` as a STATIC type, so the Overlays menu can toggle
// it. Like `RegisterRouteOverlayType` this is not part of fvkit's
// `RegisterBuiltinOverlayTypes` and cannot be: it names the router.
//
// `planner` is BORROWED and may be null; it is handed to the overlay the
// factory makes, so the picture is of the graph the shell actually routes on.
Status RegisterRoadGraphOverlayType(app::OverlayTypeRegistry& registry,
                                    const RoutePlanner* planner = nullptr);

}  // namespace fv
