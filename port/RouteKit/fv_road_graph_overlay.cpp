// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_road_graph_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_set>

#include "fvkit/canvas/geo_draw.h"
#include "fvkit/vector/feature_rows.h"

namespace fv {
namespace {

using routing::RoadArc;
using routing::RoadClass;
using routing::RoadGraph;

constexpr int kClassCount = static_cast<int>(RoadClass::kCount);

int ClassIndex(RoadClass k) {
  const int i = static_cast<int>(k);
  return (i >= 0 && i < kClassCount) ? i : -1;
}

// The dot. WHITE WITH A DARK OUTLINE, and one colour for every node rather
// than one per class: a node belongs to as many classes as it has arcs, so
// colouring it by class would mean picking one of them arbitrarily. White
// reads over every line colour in the palette above and over any chart.
constexpr FvColor kNodeFill{255, 255, 255, 255};
constexpr FvColor kNodeOutline{20, 20, 20, 255};

// How far past the viewport `QueryRect` reaches, as a FRACTION of the
// viewport's own height, floored and capped in metres.
//
// An arc is found through its ENDPOINTS, so the box has to reach far enough
// past the screen to catch an arc that crosses it with both ends outside.
// Both of the obvious answers are wrong in the same interesting way:
//
//   * a PIXEL margin says "reach further when you zoom IN", which is exactly
//     backwards — zooming in is when there is least road off the edge worth
//     reaching for. At 2000 px it fetched 26 screens' worth of Kiawah to draw
//     one, and that is what this constant replaced;
//   * a fixed METRIC margin is scale-free and therefore wrong at both ends:
//     6 km is thirteen screens at street zoom and a rounding error at the
//     zoom where a 6 km arc is one pixel long.
//
// What is actually true is that the longest arc worth catching is a fraction
// of what you are looking at — a few streets when you look at streets, a
// motorway junction spacing when you look at a state. So the margin is a
// fraction of the view, floored at 500 m (no arc between two junctions is
// shorter than a street) and capped at 40 km (nothing joins two junctions
// further apart than that, and past it the cap saves a continental pan).
constexpr double kQueryMarginFraction = 0.25;
constexpr double kQueryMarginMinMeters = 500.0;
constexpr double kQueryMarginMaxMeters = 40000.0;

// Metres per degree of latitude, near enough for a query box.
constexpr double kMetersPerDegLat = 111320.0;


// The query box, grown by the margin above. A FREE FUNCTION because the search
// wants the same reach the draw does and has no projection to ask: an arc is
// found through its endpoints either way, so a box that did not reach past
// itself would miss every road that merely crosses it.
GeoRect GrownQueryRect(GeoRect r) {
  // The box's own north-south span is the yardstick: it is the one
  // dimension a rotated projection cannot make lie about scale, because
  // VmapBounds already grew to the box of the turned corners.
  const double view_m = (r.ur.lat - r.ll.lat) * kMetersPerDegLat;
  const double margin_m =
      std::min(kQueryMarginMaxMeters,
               std::max(kQueryMarginMinMeters, view_m * kQueryMarginFraction));
  const double dlat = margin_m / kMetersPerDegLat;
  // Longitude degrees shrink with latitude; taken at the viewport's own
  // centre latitude and floored, so a box near a pole grows wide rather than
  // dividing by a cosine on its way to zero.
  const double mid_lat = (r.ll.lat + r.ur.lat) * 0.5;
  const double cos_lat = std::max(0.01, std::cos(mid_lat * 3.14159265358979 / 180.0));
  const double dlon = dlat / cos_lat;
  r.ll.lat = std::max(-90.0, r.ll.lat - dlat);
  r.ur.lat = std::min(90.0, r.ur.lat + dlat);
  // Grown past a hemisphere the box is the whole world in longitude, which is
  // both true and the only thing that will not wrap into nonsense.
  if (dlon >= 180.0 || (r.ur.lon - r.ll.lon) + 2.0 * dlon >= 360.0) {
    r.ll.lon = -180.0;
    r.ur.lon = 180.0;
  } else {
    r.ll.lon -= dlon;
    r.ur.lon += dlon;
    if (r.ll.lon < -180.0) r.ll.lon += 360.0;
    if (r.ur.lon > 180.0) r.ur.lon -= 360.0;
  }
  return r;
}

// Where a label would sit on an arc: the point half its length along, which
// for a road is a point ON the road. The centre of the bounding box is not —
// a hairpin's box centre can be in the next valley — and the merge downstream
// keeps the biggest piece's anchor, so this is what a merged road's position
// ends up being.
GeoPoint LineAnchor(const std::vector<GeoPoint>& line) {
  if (line.empty()) return GeoPoint{};
  if (line.size() == 1) return line[0];
  double total = 0.0;
  for (size_t i = 1; i < line.size(); ++i) {
    total += routing::GreatCircleMeters(line[i - 1].lat, line[i - 1].lon,
                                        line[i].lat, line[i].lon);
  }
  double half = total * 0.5;
  for (size_t i = 1; i < line.size(); ++i) {
    const double seg = routing::GreatCircleMeters(
        line[i - 1].lat, line[i - 1].lon, line[i].lat, line[i].lon);
    if (seg <= 0.0 || half > seg) {
      half -= seg;
      continue;
    }
    const double t = half / seg;
    return GeoPoint{line[i - 1].lat + t * (line[i].lat - line[i - 1].lat),
                    line[i - 1].lon + t * (line[i].lon - line[i - 1].lon)};
  }
  return line.back();
}

GeoRect BoxOf(const std::vector<GeoPoint>& line) {
  GeoRect r{line.front(), line.front()};
  for (const GeoPoint& p : line) {
    r.ll.lat = std::min(r.ll.lat, p.lat);
    r.ll.lon = std::min(r.ll.lon, p.lon);
    r.ur.lat = std::max(r.ur.lat, p.lat);
    r.ur.lon = std::max(r.ur.lon, p.lon);
  }
  return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// The palette
// ---------------------------------------------------------------------------

FvColor RoadClassColor(RoadClass klass) {
  switch (klass) {
    // The motor-only family: reds and oranges, links a shade darker.
    case RoadClass::kMotorway:      return FvColor{226,  50,  50, 255};
    case RoadClass::kMotorwayLink:  return FvColor{170,  36,  36, 255};
    case RoadClass::kTrunk:         return FvColor{240, 120,  40, 255};
    case RoadClass::kTrunkLink:     return FvColor{184,  90,  28, 255};

    // The through roads: yellows, descending in saturation with importance.
    case RoadClass::kPrimary:       return FvColor{245, 200,  40, 255};
    case RoadClass::kPrimaryLink:   return FvColor{190, 154,  30, 255};
    case RoadClass::kSecondary:     return FvColor{226, 216,  80, 255};
    case RoadClass::kSecondaryLink: return FvColor{174, 166,  60, 255};
    case RoadClass::kTertiary:      return FvColor{206, 214, 130, 255};
    case RoadClass::kTertiaryLink:  return FvColor{158, 166,  98, 255};

    // The ordinary street mesh: greys. Deliberately quiet — on any extract
    // these are most of the arcs, and they are the background the interesting
    // classes have to read against.
    case RoadClass::kUnclassified:  return FvColor{190, 190, 195, 255};
    case RoadClass::kResidential:   return FvColor{160, 160, 168, 255};
    case RoadClass::kLivingStreet:  return FvColor{132, 132, 142, 255};
    case RoadClass::kService:       return FvColor{110, 110, 120, 255};
    case RoadClass::kTrack:         return FvColor{140, 118,  86, 255};

    // What a bike or a walker uses. THE CYCLEWAY IS THE BRIGHTEST THING ON
    // THE MAP and that is the point of the overlay: when a bike route ignores
    // one, the first question is whether it is in the graph at all.
    //
    // PATH IS PINK BECAUSE IT WAS BLUE, and blue is what a chart paints
    // WATER. On Kiawah — lagoons on three sides of every fairway — a light
    // blue path was invisible against the thing it runs beside, and `path` is
    // the second class a bicycle route is made of. A colour is only distinct
    // if it is distinct from the MAP, not just from the other twenty-one
    // entries in this table.
    case RoadClass::kCycleway:      return FvColor{ 60, 220, 120, 255};
    case RoadClass::kPath:          return FvColor{255, 105, 180, 255};
    case RoadClass::kFootway:       return FvColor{130, 140, 220, 255};
    case RoadClass::kPedestrian:    return FvColor{170, 140, 220, 255};
    case RoadClass::kSteps:         return FvColor{150,  60, 180, 255};

    case RoadClass::kFerry:         return FvColor{ 60, 210, 220, 255};

    case RoadClass::kCount:
    case RoadClass::kNone:
      break;
  }
  // Not a class this build knows — a `.fvroad` written by a later build that
  // appended one. Magenta, because it should look wrong rather than blend in.
  return FvColor{255, 0, 255, 255};
}

int RoadClassWidth(RoadClass klass) {
  switch (klass) {
    case RoadClass::kMotorway:
    case RoadClass::kTrunk:
      return 5;
    case RoadClass::kMotorwayLink:
    case RoadClass::kTrunkLink:
    case RoadClass::kPrimary:
    case RoadClass::kSecondary:
      return 4;
    case RoadClass::kPrimaryLink:
    case RoadClass::kSecondaryLink:
    case RoadClass::kTertiary:
    case RoadClass::kTertiaryLink:
    case RoadClass::kUnclassified:
    case RoadClass::kResidential:
    case RoadClass::kFerry:
      return 3;
    // The bike classes are drawn at 3 rather than the 2 their importance
    // would give them: this is a bicycle debugging tool, and the thing being
    // debugged should not be the thinnest line on the screen.
    case RoadClass::kCycleway:
    case RoadClass::kPath:
      return 3;
    default:
      return 2;
  }
}

// ---------------------------------------------------------------------------
// The summaries
// ---------------------------------------------------------------------------

namespace {

std::string MetresText(double m) {
  char buf[64];
  if (m >= 1000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f km", m / 1000.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.0f m", m);
  }
  return buf;
}

}  // namespace

std::string RoadArcInfo::Summary() const {
  if (!valid) return std::string();
  std::string s = name.empty() ? std::string("(unnamed)") : name;
  s += " - " + road_class;
  s += ", " + MetresText(length_m);
  if (speed_kph > 0) s += ", " + std::to_string(speed_kph) + " km/h";

  // DIRECTION IS THE FIRST THING A WRONG ROUTE IS ABOUT, so it is said in
  // words rather than left to the flags line below. "no way" is not a joke:
  // an arc with neither bit is one the builder kept and nothing may traverse,
  // and seeing it named is how you find it.
  if (forward && backward)        s += ", both ways";
  else if (forward)               s += ", one way ->";
  else if (backward)              s += ", one way <-";
  else                            s += ", NO WAY";

  // The bicycle answer, spelled out, because `private` is not `no` and the
  // difference decides whether a route may use the arc at a price or not at
  // all (fv_road_graph.h, O5b).
  if (!bicycle_allowed)          s += ", bike NO";
  else if (bicycle_private)      s += ", bike private";
  else                           s += ", bike ok";

  if (!motor_vehicle_allowed)    s += ", car no";
  else if (motor_vehicle_private) s += ", car private";
  if (!foot_allowed)             s += ", foot no";
  else if (foot_private)         s += ", foot private";
  if (tolled)                    s += ", toll";
  if (ferry)                     s += ", ferry";

  s += "  [arc " + std::to_string(arc) + ": " + std::to_string(from_node) +
       " -> " + std::to_string(to_node) + "]";
  return s;
}

std::string RoadNodeInfo::Summary() const {
  if (!valid) return std::string();
  std::string s = "node " + std::to_string(node);
  if (osm_id != 0) s += " (osm " + std::to_string(osm_id) + ")";
  s += " - degree " + std::to_string(degree);
  // A degree-1 node is a DEAD END, and a dead end where the map shows a
  // through road is the single most common way a bike route goes wrong: the
  // two halves of a path were never noded together.
  if (degree == 1) s += " (dead end)";
  if (restricted) s += ", turn-restricted";
  return s;
}

// ---------------------------------------------------------------------------
// RoadGraphOverlay
// ---------------------------------------------------------------------------

const char RoadGraphOverlay::kTypeId[] = "fv.roadgraph";

RoadGraphOverlay::RoadGraphOverlay(std::string name)
    : Overlay(std::move(name)), counts_(kClassCount, 0) {}

RoadGraphOverlay::~RoadGraphOverlay() = default;

std::shared_ptr<const RoadGraph> RoadGraphOverlay::graph() const {
  // The planner wins: a picture of a graph nobody routes on answers the
  // wrong question. `graph_` is the fallback, not the override.
  if (planner_ != nullptr && planner_->graph_loaded()) return planner_->graph();
  return graph_;
}

Status RoadGraphOverlay::EnsureGraph() {
  if (planner_ != nullptr) return planner_->EnsureGraph();
  if (graph_ != nullptr) return Status::Ok();
  return Status::Error(kInvalidArg, "road graph overlay: no graph and no planner");
}

void RoadGraphOverlay::SetClassFilter(std::vector<RoadClass> classes) {
  class_filter_ = std::move(classes);
}

bool RoadGraphOverlay::ClassShown(RoadClass klass) const {
  if (class_filter_.empty()) return true;
  return std::find(class_filter_.begin(), class_filter_.end(), klass) !=
         class_filter_.end();
}

GeoRect RoadGraphOverlay::QueryRect(const MapProjection& proj) const {
  return GrownQueryRect(proj.VmapBounds());
}

std::vector<GeoPoint> RoadGraphOverlay::ArcLine(const RoadGraph& g,
                                                uint32_t from,
                                                const RoadArc& a) const {
  std::vector<GeoPoint> line;
  line.reserve(a.geom_count + 2);
  line.push_back(g.location(from));
  // `arc_point` already resolves kArcGeomReversed, so the interior points come
  // back in THIS arc's direction of travel and the line concatenates without
  // anyone here knowing how the geometry was stored.
  for (uint32_t k = 0; k < a.geom_count; ++k) line.push_back(g.arc_point(a, k));
  line.push_back(g.location(a.target));
  return line;
}

Status RoadGraphOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  last_proj_ = proj;
  have_proj_ = true;

  drawn_arcs_ = 0;
  drawn_nodes_ = 0;
  budget_hit_ = false;
  std::fill(counts_.begin(), counts_.end(), 0u);

  const std::shared_ptr<const RoadGraph> g = graph();
  if (g == nullptr || g->node_count() == 0) return Status::Ok();

  const GeoRect box = QueryRect(proj);
  // The nodes on screen, gathered once and used twice — for the arcs and for
  // the dots. A second NodesInRect for the dots would visit the same cells
  // again for the same answer.
  std::vector<uint32_t> nodes;
  g->NodesInRect(box, [&nodes](uint32_t n) { nodes.push_back(n); });
  if (nodes.empty()) return Status::Ok();

  GeoDraw draw(proj, &canvas);
  // A debug overlay is not a pick surface for the vector pick index — this
  // class answers its own HitTest off the graph, so the index would be built
  // every frame and read never.
  draw.SetPickEnabled(false);

  // Which of `nodes` are in the box, for the arc de-duplication rule below.
  // A hash set rather than a per-node flag array: on a continent the array is
  // node_count bits and the set is the handful actually on screen.
  const std::unordered_set<uint32_t> in_box(nodes.begin(), nodes.end());

  Status s = Status::Ok();

  if (show_edges_) {
    for (const uint32_t u : nodes) {
      if (drawn_arcs_ >= arc_budget_) { budget_hit_ = true; break; }
      for (uint32_t ai = g->arc_begin(u); ai < g->arc_end(u); ++ai) {
        if (drawn_arcs_ >= arc_budget_) { budget_hit_ = true; break; }
        const RoadArc& a = g->arc(ai);

        // EVERY EDGE IS TWO ARCS (one in each endpoint's list) and drawing
        // both would paint every road twice. The canonical one is the one
        // whose source id is the lower — EXCEPT when the other endpoint is
        // not in the box, because then nobody else will ever visit this edge
        // and skipping it would erase every road leaving the screen.
        if (a.target < u && in_box.count(a.target) != 0) continue;

        if (!ClassShown(a.klass)) continue;

        const std::vector<GeoPoint> line = ArcLine(*g, u, a);
        // kSimple: road geometry IS the road. A great circle between two
        // shape points twenty metres apart costs the geodesy and returns the
        // same line — the same argument RouteOverlay makes for a planned leg.
        s = draw.DrawGeoPolyline(
            line, LineKind::kSimple,
            SolidGeoLine(RoadClassColor(a.klass), RoadClassWidth(a.klass)));
        if (!s.ok()) return s;

        ++drawn_arcs_;
        const int ci = ClassIndex(a.klass);
        if (ci >= 0) ++counts_[ci];
      }
    }
  }

  if (show_nodes_) {
    // ONE CALL FOR EVERY DOT. `DrawPolyPolygon` fills a list of rings even-odd
    // and disjoint squares never overlap, so a ten-thousand-node view is one
    // canvas call — TrackPointsOverlay's trick, for the same reason.
    const PixelSize size = canvas.Size();
    const int half = node_px_ / 2;
    const double margin = node_px_ + 2.0;
    std::vector<std::vector<PixelPoint>> rings;
    rings.reserve(nodes.size());
    for (const uint32_t n : nodes) {
      // A node kept only by a hidden class is not part of the network being
      // looked at, so the filter reaches the dots too. With no filter this
      // loop does not run at all.
      if (!class_filter_.empty()) {
        bool shown = false;
        for (uint32_t ai = g->arc_begin(n); ai < g->arc_end(n) && !shown; ++ai) {
          shown = ClassShown(g->arc(ai).klass);
        }
        if (!shown) continue;
      }
      double sx = 0.0, sy = 0.0;
      if (!proj.GeoToSurface(g->location(n), &sx, &sy).ok()) continue;
      if (sx < -margin || sy < -margin || sx > size.width + margin ||
          sy > size.height + margin) {
        continue;  // in the grown query box, off the actual screen
      }
      const int x = static_cast<int>(sx + 0.5), y = static_cast<int>(sy + 0.5);
      rings.push_back({{x - half, y - half},
                       {x + half, y - half},
                       {x + half, y + half},
                       {x - half, y + half}});
      ++drawn_nodes_;
    }
    if (!rings.empty()) {
      const Brush fill{kNodeFill};
      const Pen outline{kNodeOutline, 1, {}};
      s = canvas.DrawPolyPolygon(rings, &fill, &outline);
      if (!s.ok()) return s;
    }
  }

  if (show_legend_) {
    // Straight on the canvas and not through GeoDraw: it is not on the earth.
    // Only the classes actually drawn, so the legend is a statement about
    // THIS view rather than a fixed key of twenty-two rows.
    std::vector<int> present;
    for (int i = 0; i < kClassCount; ++i) {
      if (counts_[i] > 0) present.push_back(i);
    }
    const int rows = static_cast<int>(present.size()) + 1;  // + the total line
    const int x = 10, y0 = 10, row_h = 16;
    const int box_w = 232, box_h = 8 + row_h * rows;

    const Brush panel{FvColor{0, 0, 0, 150}};
    s = canvas.DrawRectangle(PixelRect{x - 4, y0 - 4, box_w, box_h}, &panel,
                             nullptr);
    if (!s.ok()) return s;

    TextStyle text;
    text.size = 11.0;
    text.color = FvColor{255, 255, 255, 255};

    int y = y0;
    for (const int ci : present) {
      const RoadClass k = static_cast<RoadClass>(ci);
      const Brush swatch{RoadClassColor(k)};
      const Pen edge{FvColor{255, 255, 255, 200}, 1, {}};
      s = canvas.DrawRectangle(PixelRect{x, y + 3, 12, 9}, &swatch, &edge);
      if (!s.ok()) return s;
      const std::string label = std::string(routing::RoadClassName(k)) + "  " +
                                std::to_string(counts_[ci]);
      s = canvas.DrawTextString(label, x + 18, y + 12, text);
      if (!s.ok()) return s;
      y += row_h;
    }

    // The total, and the budget when it bit. A frame that stopped early SAYS
    // so — silently drawing three quarters of the network would make the
    // overlay lie about the thing it exists to show.
    std::string total = std::to_string(drawn_arcs_) + " arcs, " +
                        std::to_string(drawn_nodes_) + " nodes";
    if (budget_hit_) total += "  (BUDGET HIT - zoom in)";
    text.color = budget_hit_ ? FvColor{255, 180, 80, 255}
                             : FvColor{200, 200, 200, 255};
    s = canvas.DrawTextString(total, x, y + 12, text);
    if (!s.ok()) return s;
  }

  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

RoadArcInfo RoadGraphOverlay::ArcInfoFor(uint32_t arc) const {
  RoadArcInfo info;
  const std::shared_ptr<const RoadGraph> g = graph();
  if (g == nullptr || arc >= g->arc_count()) return info;

  const RoadArc& a = g->arc(arc);
  // The arc array is CSR and carries no back-pointer to its source, so the
  // source is found by binary search over `arc_begin` — the one thing the
  // layout makes awkward, and it costs a log rather than a scan.
  uint32_t lo = 0, hi = g->node_count();
  while (lo + 1 < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (g->arc_begin(mid) <= arc) lo = mid; else hi = mid;
  }
  const uint32_t from = lo;

  info.valid = true;
  info.arc = arc;
  info.from_node = from;
  info.to_node = a.target;
  info.from_osm_id = g->node(from).osm_id;
  info.to_osm_id = g->node(a.target).osm_id;
  info.name = g->name(a.name);
  info.road_class = routing::RoadClassName(a.klass);
  info.length_m = a.length_m;
  info.speed_kph = a.speed_kph;
  info.flags = a.flags;
  info.forward = a.forward();
  info.backward = a.backward();
  info.bicycle_allowed = a.bicycle_allowed();
  info.foot_allowed = a.foot_allowed();
  info.motor_vehicle_allowed = a.motor_vehicle_allowed();
  info.bicycle_private = a.bicycle_private();
  info.foot_private = a.foot_private();
  info.motor_vehicle_private = a.motor_vehicle_private();
  info.tolled = a.tolled();
  info.ferry = a.is_ferry();
  return info;
}

RoadNodeInfo RoadGraphOverlay::NodeInfoFor(uint32_t node) const {
  RoadNodeInfo info;
  const std::shared_ptr<const RoadGraph> g = graph();
  if (g == nullptr || node >= g->node_count()) return info;
  info.valid = true;
  info.node = node;
  info.osm_id = g->node(node).osm_id;
  info.position = g->location(node);
  info.degree = g->arc_end(node) - g->arc_begin(node);
  info.restricted = g->node_restricted(node);
  return info;
}

namespace {

// Squared pixel distance from p to segment ab, and where along it that fell.
double SegDistPxSq(double px, double py, double ax, double ay, double bx,
                   double by) {
  const double vx = bx - ax, vy = by - ay;
  const double len2 = vx * vx + vy * vy;
  double t = 0.0;
  if (len2 > 0.0) {
    t = ((px - ax) * vx + (py - ay) * vy) / len2;
    t = std::max(0.0, std::min(1.0, t));
  }
  const double dx = px - (ax + t * vx), dy = py - (ay + t * vy);
  return dx * dx + dy * dy;
}

// The box `tolerance_px` around a pixel, in degrees. Grown by a slack factor
// because a click near the MIDDLE of a long arc is nowhere near either of its
// endpoints, and only the endpoints are indexed — so the box has to reach far
// enough to catch a node whose arc comes back past the click.
GeoRect PickBox(const MapProjection& proj, const GeoPoint& at,
                double tolerance_px, double slack_px) {
  const double dlat = proj.DegPerPixelLat() * (tolerance_px + slack_px);
  const double dlon = proj.DegPerPixelLon() * (tolerance_px + slack_px);
  GeoRect r;
  r.ll.lat = std::max(-90.0, at.lat - dlat);
  r.ur.lat = std::min(90.0, at.lat + dlat);
  if (dlon >= 180.0) {
    r.ll.lon = -180.0;
    r.ur.lon = 180.0;
  } else {
    r.ll.lon = at.lon - dlon;
    r.ur.lon = at.lon + dlon;
    if (r.ll.lon < -180.0) r.ll.lon += 360.0;
    if (r.ur.lon > 180.0) r.ur.lon -= 360.0;
  }
  return r;
}

// How far the pick box reaches past the tolerance, in pixels. An arc is found
// through its endpoints, so this is "the longest arc, in pixels, that will
// still be found by a click on its middle".
constexpr double kPickSlackPx = 600.0;

}  // namespace

RoadArcInfo RoadGraphOverlay::ArcAt(const MapProjection& proj, double x,
                                    double y, double tolerance_px) const {
  RoadArcInfo best;
  const std::shared_ptr<const RoadGraph> g = graph();
  if (g == nullptr || !proj.Ready()) return best;

  GeoPoint at;
  if (!proj.SurfaceToGeo(x, y, &at).ok()) return best;

  const double tol2 = tolerance_px * tolerance_px;
  double best_d2 = std::numeric_limits<double>::infinity();
  uint32_t best_arc = routing::kNoArc;

  g->NodesInRect(
      PickBox(proj, at, tolerance_px, kPickSlackPx), [&](uint32_t u) {
        for (uint32_t ai = g->arc_begin(u); ai < g->arc_end(u); ++ai) {
          const RoadArc& a = g->arc(ai);
          if (!ClassShown(a.klass)) continue;
          // Both arcs of an edge are measured — no de-duplication here. They
          // are DIFFERENT ANSWERS: one is oneway east and the other oneway
          // west, and a person clicking a street wants the one whose source
          // they can see named. Lower index wins a tie, which is stable.
          const std::vector<GeoPoint> line = ArcLine(*g, u, a);
          double ax = 0.0, ay = 0.0;
          if (!proj.GeoToSurface(line[0], &ax, &ay).ok()) continue;
          for (size_t k = 1; k < line.size(); ++k) {
            double bx = 0.0, by = 0.0;
            if (!proj.GeoToSurface(line[k], &bx, &by).ok()) break;
            const double d2 = SegDistPxSq(x, y, ax, ay, bx, by);
            if (d2 < best_d2) { best_d2 = d2; best_arc = ai; }
            ax = bx;
            ay = by;
          }
        }
      });

  if (best_arc == routing::kNoArc || best_d2 > tol2) return best;
  best = ArcInfoFor(best_arc);
  best.distance_px = std::sqrt(best_d2);
  return best;
}

RoadNodeInfo RoadGraphOverlay::NodeAt(const MapProjection& proj, double x,
                                      double y, double tolerance_px) const {
  RoadNodeInfo best;
  const std::shared_ptr<const RoadGraph> g = graph();
  if (g == nullptr || !proj.Ready()) return best;

  GeoPoint at;
  if (!proj.SurfaceToGeo(x, y, &at).ok()) return best;

  const double tol2 = tolerance_px * tolerance_px;
  double best_d2 = std::numeric_limits<double>::infinity();
  uint32_t best_node = 0;
  bool found = false;

  // No slack: a node IS its own position, so the tolerance box is the whole
  // question. This is the one query in the class that is exactly its box.
  g->NodesInRect(PickBox(proj, at, tolerance_px, 0.0), [&](uint32_t n) {
    if (!class_filter_.empty()) {
      bool shown = false;
      for (uint32_t ai = g->arc_begin(n); ai < g->arc_end(n) && !shown; ++ai) {
        shown = ClassShown(g->arc(ai).klass);
      }
      if (!shown) return;
    }
    double sx = 0.0, sy = 0.0;
    if (!proj.GeoToSurface(g->location(n), &sx, &sy).ok()) return;
    const double dx = x - sx, dy = y - sy;
    const double d2 = dx * dx + dy * dy;
    if (d2 < best_d2) { best_d2 = d2; best_node = n; found = true; }
  });

  if (!found || best_d2 > tol2) return best;
  best = NodeInfoFor(best_node);
  best.distance_px = std::sqrt(best_d2);
  return best;
}

void RoadGraphOverlay::HitTestPoint(const MapProjection& proj, PixelPoint p,
                                    double tolerance_px,
                                    std::vector<app::HitItem>& out) {
  const double x = static_cast<double>(p.x), y = static_cast<double>(p.y);

  // The node first — see the header note: every node sits on an arc, so
  // "nearest wins" would make the dots unreachable.
  const RoadNodeInfo n = NodeAt(proj, x, y, tolerance_px);
  if (n.valid) {
    app::HitItem item;
    item.overlay = this;
    item.feature = n.node;  // top bit clear: a node
    item.distance_px = n.distance_px;
    item.hint.tool_tip = n.Summary();
    item.hint.status = n.Summary();
    out.push_back(std::move(item));
  }

  const RoadArcInfo a = ArcAt(proj, x, y, tolerance_px);
  if (a.valid) {
    app::HitItem item;
    item.overlay = this;
    item.feature = kFeatureArcBit | a.arc;
    item.distance_px = a.distance_px;
    item.hint.tool_tip = a.Summary();
    item.hint.status = a.Summary();
    out.push_back(std::move(item));
  }
}

// ---------------------------------------------------------------------------
// Search (S4)
// ---------------------------------------------------------------------------

void RoadGraphOverlay::Search(const app::SearchQuery& q,
                              const std::atomic<bool>& cancel,
                              std::vector<app::SearchResult>& out) {
  last_search_arcs_ = 0;
  last_search_results_ = 0;

  const std::shared_ptr<const RoadGraph> gp = graph();
  if (gp == nullptr) return;  // nothing loaded: not an error, just no roads
  const RoadGraph& g = *gp;

  // NEITHER SURFACE MEANS NO ANSWER. VectorMapOverlay's rule, for the same
  // reason: "everything in the graph" is not something a search box can show
  // and nobody would wait for it either.
  if (q.text.empty() && !q.area) return;

  // THE NAME TABLE IS THE INDEX, and it is why this needs no index. Each
  // distinct name is stored once, so the text test runs over thousands of
  // strings rather than over millions of arcs, and the arc pass below is a
  // vector lookup. Entry 0 is the empty name: an unnamed arc is never a row,
  // exactly as an unnamed vector feature is never one.
  const uint32_t names = g.name_count();
  std::vector<int> name_quality(names, -1);
  for (uint32_t i = 1; i < names; ++i) {
    int quality = 0;
    if (app::SearchTextAccepts(q, g.name(i), &quality)) name_quality[i] = quality;
  }

  const bool have_area = q.area.has_value();
  const GeoRect grown = have_area ? GrownQueryRect(*q.area) : GeoRect{};

  // WHICH NODES ARE VISITED, and it is the only difference between the two
  // paths. With an area the grid answers it; without one every node is
  // visited, which a text query can afford because of the table above.
  std::vector<uint32_t> nodes;
  std::unordered_set<uint32_t> visited;
  if (have_area) {
    g.NodesInRect(grown, [&](uint32_t n) {
      nodes.push_back(n);
      visited.insert(n);
    });
  }
  const uint32_t node_total = have_area ? static_cast<uint32_t>(nodes.size())
                                        : g.node_count();

  std::vector<FeatureRow> rows;
  std::vector<GeoPoint> line;
  bool stopped = false;

  for (uint32_t k = 0; k < node_total && !stopped; ++k) {
    const uint32_t u = have_area ? nodes[k] : k;
    // Polled per NODE rather than per arc: a node is a handful of arcs, so
    // this is often enough to stop on a keystroke and rare enough to cost
    // nothing measurable.
    if (cancel.load()) break;

    for (uint32_t i = g.arc_begin(u); i < g.arc_end(u); ++i) {
      if (last_search_arcs_ >= search_arc_budget_) {
        stopped = true;
        break;
      }
      const RoadArc& a = g.arc(i);
      const uint32_t v = a.target;
      // EVERY EDGE IS TWO ARCS. The draw's rule, with the same exception: the
      // lower-numbered endpoint owns the edge, unless the other endpoint is
      // outside the visited set, in which case nobody else will ever reach it.
      // The merge below would fold a mirrored pair anyway — they share a name,
      // a class and a box — so this is an economy rather than a correctness
      // rule, and it is here so that merging can be switched off without the
      // answer doubling.
      if (u > v && (!have_area || visited.count(v) != 0)) continue;
      ++last_search_arcs_;

      if (a.name == 0 || a.name >= names) continue;
      const int quality = name_quality[a.name];
      if (quality < 0) continue;

      line = ArcLine(g, u, a);
      if (line.empty()) continue;
      const GeoRect box = BoxOf(line);
      // THE BOX, NOT THE ANCHOR: a road running through the query area is in
      // it, even when the point a label would sit at is a mile up the road.
      // VectorMapOverlay's tier 2 tests the same way for the same reason.
      if (have_area && !q.area->Intersects(box)) continue;

      FeatureRow row;
      row.title = g.name(a.name);
      // The layer every road is in. It exists because MergeFeatureRows keys on
      // it, and a constant is the honest value: a road graph has one layer.
      row.layer = "road";
      row.style_key = routing::RoadClassName(a.klass);
      row.bounds = box;
      row.anchor = LineAnchor(line);
      row.quality = quality;
      // RELEVANCE FOR FREE: RoadClass is declared in descending importance and
      // that ordering is part of the file format, so the enum value IS the
      // rank — the graph's own answer to the question a tile pyramid answers
      // with min_zoom.
      const int ci = ClassIndex(a.klass);
      row.rank = ci >= 0 ? ci : kClassCount;
      row.ref.layer = 0;
      row.ref.feature = static_cast<int32_t>(i);
      rows.push_back(std::move(row));
    }
  }

  // ONE ROAD, ONE ROW — the same function the vector scan and the staged name
  // index use, so "Ruddy Turnstone" cannot mean fifteen things here and one
  // thing there.
  MergeFeatureRows(&rows, search_merge_gap_m_);

  // Ranked BEFORE the cap, which is the whole point of having a rank: a
  // capped "main" should answer with the primary road and not with whichever
  // service alley the grid happened to visit first. Stable, so equal rows keep
  // the merge's own order.
  std::stable_sort(rows.begin(), rows.end(),
                   [](const FeatureRow& a, const FeatureRow& b) {
                     if (a.quality != b.quality) return a.quality < b.quality;
                     return a.rank < b.rank;
                   });

  for (FeatureRow& row : rows) {
    if (q.max_results > 0 && last_search_results_ >= q.max_results) break;
    app::SearchResult r;
    r.overlay = this;
    // PACKED, NOT MINTED — the same packing HitTestPoint uses, so a result and
    // a click on the same road name the same arc, and `ArcInfoFor` answers
    // about either without knowing which one asked.
    r.feature = kFeatureArcBit | static_cast<uint64_t>(
                                     static_cast<uint32_t>(row.ref.feature));
    r.match_quality = row.quality;
    r.position = row.anchor;
    r.bounds = row.bounds;
    r.title = std::move(row.title);
    r.detail = "road";
    if (!row.style_key.empty()) r.detail += " \xc2\xb7 " + row.style_key;
    out.push_back(std::move(r));
    ++last_search_results_;
  }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

Status RegisterRoadGraphOverlayType(app::OverlayTypeRegistry& registry,
                                    const RoutePlanner* planner) {
  app::OverlayTypeDesc desc;
  desc.id = RoadGraphOverlay::kTypeId;
  desc.display_name = "Road Graph (debug)";
  desc.icon = "roadgraph";
  // 900: under the route (1000) and over the chart. A debug view of the roads
  // a route follows has to be BEHIND the route, or it hides the answer it was
  // opened to explain.
  desc.default_display_order = 900;
  // No `file`: a static type. There is nothing to save that the `.fvroad`
  // does not already hold, and File > Open should not offer to open one here
  // — the planner owns which graph is loaded.
  desc.factory = [planner] {
    auto ov = std::make_shared<RoadGraphOverlay>();
    if (planner != nullptr) ov->SetPlanner(planner);
    return ov;
  };
  return registry.Register(std::move(desc));
}

}  // namespace fv
