// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pyfvw.route — RouteKit, bound.
//
// WHY THIS TU EXISTS AT ALL, and it is not "the module got long". `fv_routekit`
// links BOTH fvkit and port/Routing, which is the one thing fvkit itself may
// not do — so the route overlay could not live in fvkit, was therefore not in
// `RegisterBuiltinOverlayTypes`, and until now was not in the bindings either.
// PythonView could not reach it. That is the whole reason `port/apps/route.py`
// carried a SECOND route overlay: not a design choice, an unbuilt bridge.
//
// So the point of this file is subtraction. With it, `route.py` keeps only
// what a tk shell actually owns — a tool palette of `MenuNode`s and a type
// descriptor — and the document, the plan, the picture, the pick and now the
// EDITOR are one implementation that Pippin runs too.
//
// TWO THINGS ARE BOUND HERE THAT LOOK LIKE ONE. `RouteOverlay` is the map
// object; `RouteEditSession` (`overlay.edit`) is what the user is DOING to it.
// A shell that only draws a route never touches the second, which is exactly
// the split that lets Pippin's ride view and PythonView's editor share a class.
//
// PYTHON NEVER SEES A Status (the module's rule): every fallible call raises
// pyfvw.FvError instead, and the ones that return a plain bool return it
// because the C++ does — a route that will not connect is an ANSWER, not an
// error.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <vector>

#include "pyfvw_common.h"

#include "fv_road_graph.h"
#include "fv_road_graph_overlay.h"
#include "fv_route_doc.h"
#include "fv_route_edit.h"
#include "fv_route_overlay.h"
#include "fv_route_planner.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"

namespace pyfvw {

namespace py = pybind11;
using namespace pybind11::literals;

namespace {

fv::FvColor ToColor(py::sequence s) {
  fv::FvColor c;
  c.r = static_cast<unsigned char>(py::cast<int>(s[0]));
  c.g = static_cast<unsigned char>(py::cast<int>(s[1]));
  c.b = static_cast<unsigned char>(py::cast<int>(s[2]));
  c.a = s.size() > 3 ? static_cast<unsigned char>(py::cast<int>(s[3])) : 255;
  return c;
}

py::tuple FromColor(const fv::FvColor& c) {
  return py::make_tuple(c.r, c.g, c.b);
}

}  // namespace

void BindRoute(py::module_& m) {
  py::module_ route = m.def_submodule(
      "route",
      "RouteKit (Pippin P5): the .fvrte document, the planner over the O4 "
      "road graph, the overlay that draws it, and the editor that edits it. "
      "Its own module because it links the router, which fvkit may not -- so "
      "`register_route_overlay_type` is separate from "
      "`app.register_builtin_types()` and a shell calls both.");

  // --- the document --------------------------------------------------------

  py::class_<fv::RouteWaypoint>(route, "RouteWaypoint",
      "One stop. The LABEL is the identity -- selection and deletion are by "
      "label, so two waypoints sharing one would delete as a pair.")
      .def(py::init<>())
      // Two spellings, and `MapPoint`'s precedent is why: an author writing a
      // literal has a lat and a lon, not a GeoPoint, and code passing a
      // position along already has the point.
      .def(py::init([](std::string label, double lat, double lon) {
             return fv::RouteWaypoint{std::move(label), fv::GeoPoint{lat, lon}};
           }),
           "label"_a, "lat"_a, "lon"_a)
      .def(py::init([](std::string label, const fv::GeoPoint& position) {
             return fv::RouteWaypoint{std::move(label), position};
           }),
           "label"_a, "position"_a)
      .def_readwrite("label", &fv::RouteWaypoint::label)
      .def_readwrite("position", &fv::RouteWaypoint::position)
      .def("__repr__", [](const fv::RouteWaypoint& w) {
        return "<RouteWaypoint '" + w.label + "' " +
               std::to_string(w.position.lat) + ", " +
               std::to_string(w.position.lon) + ">";
      });

  // --- the plan ------------------------------------------------------------

  py::class_<fv::RoutePlan>(route, "RoutePlan",
      "What a plan came back with. `found` is False for the per-pair fallback "
      "AND for a total failure; `legs` says which.")
      .def_readonly("found", &fv::RoutePlan::found)
      .def_readonly("legs", &fv::RoutePlan::legs,
                    "Per-leg polylines, one per consecutive pair, in order.")
      .def_readonly("length_m", &fv::RoutePlan::length_m)
      .def_readonly("seconds", &fv::RoutePlan::seconds)
      .def_readonly("straight_legs", &fv::RoutePlan::straight_legs)
      .def_readonly("u_turn_stops", &fv::RoutePlan::u_turn_stops)
      .def_readonly("unreachable_leg", &fv::RoutePlan::unreachable_leg)
      .def_readonly("is_bicycle", &fv::RoutePlan::is_bicycle,
                    "Priced as a bicycle route, which is why the line is "
                    "drawn dashed.")
      .def_readonly("status", &fv::RoutePlan::status,
                    "The one line of map a route gets to explain itself on.");

  py::class_<fv::RoutePlanOptions>(route, "RoutePlanOptions")
      .def(py::init<>())
      .def_readwrite("snap_meters", &fv::RoutePlanOptions::snap_meters,
                     "How far a stop may be from the nearest road. 2000 by "
                     "default -- much wider than the router's 500, because a "
                     "waypoint dropped by hand on a chart is not on a road.")
      .def_readwrite("profile", &fv::RoutePlanOptions::profile)
      .def_readwrite("cycle_only", &fv::RoutePlanOptions::cycle_only)
      .def_readwrite("avoid_tolls", &fv::RoutePlanOptions::avoid_tolls)
      .def_readwrite("avoid_ferries", &fv::RoutePlanOptions::avoid_ferries);

  route.def("is_bicycle_request", &fv::IsBicycleRequest, "profile"_a,
            "cycle_only"_a = false,
            "Whether this request is a bicycle one -- matched on the profile "
            "NAME, because the rule file owns the profiles and a user may well "
            "call theirs 'bicycle-winter'. The OVERLAY needs the same answer "
            "to style a line it did not plan.");

  py::class_<fv::RoutePlanner>(route, "RoutePlanner",
      "One graph and one rule file, shared by every route a shell opens. The "
      "graph loads LAZILY and ONCE; the rule file is polled per plan, so an "
      "edited weight lands on the next route with nothing restarted.")
      .def(py::init<std::string, std::string>(), "graph_path"_a,
           "rules_path"_a = "")
      .def_property_readonly("graph_path", &fv::RoutePlanner::graph_path)
      .def_property_readonly("rules_path", &fv::RoutePlanner::rules_path)
      .def("set_graph_path", &fv::RoutePlanner::SetGraphPath, "path"_a,
           "Replacing it drops whatever was loaded; setting the same path "
           "again is a no-op, so a shell may call this per frame.")
      .def("set_rules_path", &fv::RoutePlanner::SetRulesPath, "path"_a)
      .def("ensure_graph",
           [](const fv::RoutePlanner& p) { ThrowIfError(p.EnsureGraph()); },
           "Loads the graph now rather than on the first press of a button.")
      .def_property_readonly("graph_loaded", &fv::RoutePlanner::graph_loaded)
      .def("graph",
           [](const fv::RoutePlanner& p) {
             // SHARED, not copied (P7): the moving map's snapper indexes the
             // same roads the router routes over, so a shell loading Kiawah
             // twice would pay twice the memory for identical tarmac -- and
             // the two could disagree about what a road IS.
             //
             // The const goes because `pyfvw.routing.RoadGraph`'s holder is
             // non-const and every method bound on it is a query. Nothing on
             // the Python side can mutate a graph.
             return std::const_pointer_cast<fv::routing::RoadGraph>(p.graph());
           },
           "The loaded graph, or None when nothing has loaded one. Shared "
           "with whatever else indexes it -- pass it straight to "
           "nav.RoadGraphNetwork.")
      .def_property_readonly("rules_error", &fv::RoutePlanner::rules_error,
                             "Empty while the rules in force came from the "
                             "file; otherwise why they did not, with the "
                             "builtin weights still answering routes.")
      .def_property_readonly("profile_names", &fv::RoutePlanner::profile_names,
                             "The profiles the rule file defines, for a UI "
                             "that offers them.")
      .def("plan", &fv::RoutePlanner::Plan, "stops"_a,
           "options"_a = fv::RoutePlanOptions{},
           "Fewer than two stops is not an error to shout about -- it is what "
           "a half-built route looks like -- so it comes back as a failed "
           "plan whose status says so.");

  // --- the editor ----------------------------------------------------------

  // EditPosition is `fv::EditPosition`, shared with the point editor and bound
  // in the overlay module (which initialises first). Aliased here so
  // `pyfvw.route.EditPosition` still names it.
  route.attr("EditPosition") = m.attr("overlay").attr("EditPosition");

  py::class_<fv::RouteEditSession>(route, "RouteEditSession",
      "The gestures, the armed modes and the undo stack -- what the user is "
      "DOING to a route, as opposed to what the route is. Reached as "
      "`overlay.edit`; never constructed directly.\n\n"
      "TWO WAYS IN, and both are the same edit: a desktop routes raw events "
      "through the overlay's on_mouse_* / on_key_down, a shell with its own "
      "recognizers calls begin_drag/drag_to/end_drag and the named commands.")
      .def_property("adding", &fv::RouteEditSession::adding,
                    &fv::RouteEditSession::SetAdding,
                    "Armed by 'a' and spent by the next click. Add is two "
                    "gestures because a click has to keep meaning 'select'.")
      .def_property("pick_tolerance_px",
                    &fv::RouteEditSession::pick_tolerance_px,
                    &fv::RouteEditSession::SetPickTolerancePx,
                    "What a press hit-tests with, ADDED to the marker's own "
                    "drawn half-width.")
      .def_property("snap_tolerance_px",
                    &fv::RouteEditSession::snap_tolerance_px,
                    &fv::RouteEditSession::SetSnapTolerancePx,
                    "How far a placed point looks for something exact to land "
                    "on. 0 switches snapping off. The core reads no settings "
                    "file -- a shell that knows its device says the number.")
      .def_property_readonly("has_edit_focus",
                             &fv::RouteEditSession::has_edit_focus)
      .def("enter_edit_focus", &fv::RouteEditSession::EnterEditFocus)
      .def("release_edit_focus", &fv::RouteEditSession::ReleaseEditFocus,
           "Leaves any half-finished gesture with the mode, or the next entry "
           "starts armed for a click the user made a minute ago.")
      // The EditTarget half. Bound HERE and not on the overlay because this is
      // where it lives; the overlay's `AsEditTarget()` is for the app layer,
      // which reaches an overlay and never a session.
      .def("can_undo", &fv::RouteEditSession::CanUndo)
      .def("undo", &fv::RouteEditSession::Undo)
      .def("can_redo", &fv::RouteEditSession::CanRedo)
      .def("redo", &fv::RouteEditSession::Redo)
      .def("clear_history", &fv::RouteEditSession::ClearHistory,
           "What a new or newly opened document gets: an undo that reached "
           "back past a File > Open would restore waypoints into a document "
           "they were never in.")
      .def("select", &fv::RouteEditSession::Select, "label"_a)
      .def("delete", &fv::RouteEditSession::Delete, "label"_a,
           "False -- and nothing changed, not even an undo entry -- when no "
           "such waypoint exists.")
      .def("add_at", &fv::RouteEditSession::AddAt, "position"_a,
           "Inserts AFTER the selected waypoint (at the end when nothing is "
           "selected), selects it, and returns its new label.")
      .def("move_to", &fv::RouteEditSession::MoveTo, "label"_a, "position"_a)
      .def("follow_roads", &fv::RouteEditSession::FollowRoads)
      .def("follow_roads_by_bicycle",
           &fv::RouteEditSession::FollowRoadsByBicycle,
           "The rule file's own bicycle profile when it defines one, so tuned "
           "weights apply; the pre-O5c boolean otherwise.")
      .def("bicycle_options", &fv::RouteEditSession::BicycleOptions)
      .def("cycle_leg_kind", &fv::RouteEditSession::CycleLegKind,
           "Great circle -> rhumb -> straight-in-projection -> round again. "
           "Returns the kind now in force.")
      .def("resolve_pixel",
           [](const fv::RouteEditSession& s, int x, int y) {
             return s.ResolvePixel(fv::PixelPoint{x, y});
           },
           "x"_a, "y"_a,
           "The position a pixel means, snapped to an exact coordinate when "
           "one is within snap_tolerance_px.")
      .def_property_readonly("dragging", &fv::RouteEditSession::dragging)
      .def_property_readonly("drag_label", &fv::RouteEditSession::drag_label)
      .def("begin_drag",
           [](fv::RouteEditSession& s, const std::string& label, int x, int y) {
             return s.BeginDrag(label, fv::PixelPoint{x, y});
           },
           "label"_a, "x"_a, "y"_a)
      .def("drag_to",
           [](fv::RouteEditSession& s, int x, int y) {
             return s.DragTo(fv::PixelPoint{x, y});
           },
           "x"_a, "y"_a)
      .def("end_drag",
           [](fv::RouteEditSession& s, int x, int y) {
             return s.EndDrag(fv::PixelPoint{x, y});
           },
           "x"_a, "y"_a)
      .def("cancel_drag", &fv::RouteEditSession::CancelDrag,
           "Escape mid-drag: the waypoint goes back and the undo entry the "
           "first move pushed is SPENT putting it there, so a cancelled drag "
           "leaves no trace in the history.")
      .def("new_label", &fv::RouteEditSession::NewLabel)
      .def("insert_index", &fv::RouteEditSession::InsertIndex);

  // --- the overlay ---------------------------------------------------------

  py::class_<fv::RouteOverlay, fv::Overlay, std::shared_ptr<fv::RouteOverlay>>(
      route, "RouteOverlay",
      "The route on the map: a .fvrte document, a plan over the road graph, "
      "the picture, the pick, the snap and the editor. The second C++ FILE "
      "overlay and the first that draws something COMPUTED.")
      .def(py::init<std::string>(), "name"_a = "Route")

      // --- the document ---
      .def_property("waypoints", &fv::RouteOverlay::waypoints,
                    &fv::RouteOverlay::SetWaypoints,
                    "Replacing them DROPS THE PLAN: a stale road under a moved "
                    "marker reads as a bug in the router.")
      .def_property("color",
                    [](const fv::RouteOverlay& o) { return FromColor(o.color()); },
                    [](fv::RouteOverlay& o, py::sequence c) {
                      o.SetColor(ToColor(c));
                    },
                    "What an UNCALCULATED leg and every marker are drawn in.")
      .def_property("profile", &fv::RouteOverlay::profile,
                    &fv::RouteOverlay::SetProfile,
                    "The rule-file profile this route is priced with. A "
                    "document field, so it saves and it dirties.")
      .def_property("selected", &fv::RouteOverlay::selected,
                    &fv::RouteOverlay::SetSelected,
                    "The selected waypoint's LABEL, or ''. Not a document "
                    "change: selecting does not dirty the overlay.")

      // --- the plan ---
      .def("set_planner", &fv::RouteOverlay::SetPlanner, "planner"_a,
           // BORROWED in C++; from Python the overlay keeps it alive, because
           // a shell that let its planner fall out of scope would leave every
           // open route holding a dangling pointer with no warning.
           py::keep_alive<1, 2>(),
           "One planner (one graph, one rule file) is handed to every route a "
           "shell opens -- the graph is the expensive thing, not the overlay.")
      .def("follow_roads", &fv::RouteOverlay::FollowRoads,
           "options"_a = fv::RoutePlanOptions{},
           "Plans over the current waypoints and keeps the answer to draw. "
           "The document's own profile is the default. False for anything "
           "short of a through route -- the plan is still kept and still "
           "drawn, because the fallback legs are worth seeing.")
      .def("clear_roads", &fv::RouteOverlay::ClearRoads,
           "Back to straight legs. What Esc does.")
      .def_property_readonly("has_plan", &fv::RouteOverlay::has_plan)
      .def_property_readonly("plan", &fv::RouteOverlay::plan,
                             py::return_value_policy::reference_internal)
      .def_property_readonly("status", &fv::RouteOverlay::status)
      .def("set_show_status", &fv::RouteOverlay::SetShowStatus, "on"_a)

      // --- drawing ---
      .def_property("show_labels", &fv::RouteOverlay::show_labels,
                    &fv::RouteOverlay::SetShowLabels,
                    "ON by default, unlike PointOverlay: a route has a handful "
                    "of waypoints and their ORDER is the document.")
      .def_property("leg_kind", &fv::RouteOverlay::leg_kind,
                    &fv::RouteOverlay::SetLegKind,
                    "How an UNCALCULATED leg is filled in. A calculated one is "
                    "always kSimple -- road geometry IS the road.")
      .def_property("symbol_dpi_scale", &fv::RouteOverlay::symbol_dpi_scale,
                    &fv::RouteOverlay::SetSymbolDpiScale,
                    "One authored symbol pixel per device unit. A route's "
                    "markers are the APP's furniture and take the app's unit, "
                    "which is NOT chart symbology's 0.32 mm reference.")

      // --- the stack, the frame, the editor ---
      .def("set_manager", &fv::RouteOverlay::SetManager, "manager"_a,
           // Deliberately NO keep_alive: the manager owns the overlay, so
           // anchoring it here would be a reference cycle. The manager
           // outliving its own overlays is the shell's own invariant.
           "The stack, for mouse CAPTURE during a drag and for the snap walk, "
           "and nothing else. None is legal: the gesture still works, it is "
           "just uncaptured and unsnapped.")
      .def_property_readonly("has_projection", &fv::RouteOverlay::has_projection)
      .def_property_readonly(
          "edit", [](fv::RouteOverlay& o) -> fv::RouteEditSession& {
            return o.edit();
          },
          py::return_value_policy::reference_internal,
          "The editing session. Always present -- an overlay nobody edits "
          "simply never calls into it.")

      // --- Persistence ---
      .def("file_new", [](fv::RouteOverlay& o) { ThrowIfError(o.FileNew()); })
      .def("file_open",
           [](fv::RouteOverlay& o, const std::string& spec) {
             ThrowIfError(o.FileOpen(spec));
           },
           "spec"_a)
      .def("file_save_as",
           [](fv::RouteOverlay& o, const std::string& spec, int format_index) {
             ThrowIfError(o.FileSaveAs(spec, format_index));
           },
           "spec"_a, "format_index"_a = 0)
      .def("revert",
           [](fv::RouteOverlay& o, const std::string& spec) {
             ThrowIfError(o.Revert(spec));
           },
           "spec"_a)

      // --- HitTest ---
      .def("hit_test_point",
           [](fv::RouteOverlay& o, const fv::MapProjection& proj, int x, int y,
              double tolerance_px) {
             std::vector<fv::app::HitItem> out;
             o.HitTestPoint(proj, fv::PixelPoint{x, y}, tolerance_px, out);
             return out;
           },
           "proj"_a, "x"_a, "y"_a, "tolerance_px"_a = 8.0,
           "Hit-tests what was DRAWN, so a pick agrees with the screen. An "
           "overlay that has never drawn answers nothing.")
      .def("label_for_feature", &fv::RouteOverlay::LabelForFeature, "feature"_a,
           "HitItem.feature is a minted handle, not a packing: a waypoint's "
           "identity here is its LABEL, and a label is not a number.")
      .def("snap_to_point",
           [](fv::RouteOverlay& o, const fv::MapProjection& proj, int x, int y,
              double tolerance_px) {
             std::vector<fv::app::SnapToItem> out;
             o.SnapToPoint(proj, fv::PixelPoint{x, y}, tolerance_px, out);
             return out;
           },
           "proj"_a, "x"_a, "y"_a, "tolerance_px"_a = 8.0)

      .def_property_readonly_static(
          "TYPE_ID", [](py::object) { return fv::RouteOverlay::kTypeId; })
      .def_property_readonly_static(
          "EXTENSION", [](py::object) { return fv::RouteOverlay::kExtension; });

  route.def(
      "register_route_overlay_type",
      [](fv::app::OverlayTypeRegistry& registry, const fv::RoutePlanner* planner) {
        ThrowIfError(fv::RegisterRouteOverlayType(registry, planner));
      },
      "registry"_a, "planner"_a = nullptr, py::keep_alive<1, 2>(),
      "Registers `fv.route` so File > Open reaches a .fvrte. NOT part of "
      "app.register_builtin_types() and it cannot be -- that function lives "
      "in fvkit, and fvkit is exactly what may not link the router. A shell "
      "calls both.\n\n"
      "The planner is carried to every overlay the factory makes, which is "
      "what lets a shell own ONE graph across however many routes are open. "
      "A shell that wants an EDITOR for the type registers its own descriptor "
      "instead -- an editor is a palette, and a palette is shell furniture.");

  // --- the road-graph debug overlay ---------------------------------------
  //
  // BOUND HERE and not in `pyfvw.routing` for the same reason RouteOverlay is:
  // it is an `Overlay` AND it names the router, and that pair is exactly what
  // RouteKit exists for. `pyfvw.routing` is the graph and the search; this is
  // the picture of them.

  py::class_<fv::RoadArcInfo>(route, "RoadArcInfo",
      "One arc, unpacked into the answers a person debugging a route asks. "
      "Every field is read off the graph -- what this says is what the ROUTER "
      "sees, not what the chart shows.")
      .def_readonly("valid", &fv::RoadArcInfo::valid,
                    "False when the click hit no arc. Not an error: 'you "
                    "clicked empty map' is an answer.")
      .def_readonly("arc", &fv::RoadArcInfo::arc)
      .def_readonly("from_node", &fv::RoadArcInfo::from_node)
      .def_readonly("to_node", &fv::RoadArcInfo::to_node)
      .def_readonly("from_osm_id", &fv::RoadArcInfo::from_osm_id)
      .def_readonly("to_osm_id", &fv::RoadArcInfo::to_osm_id)
      .def_readonly("name", &fv::RoadArcInfo::name)
      .def_readonly("road_class", &fv::RoadArcInfo::road_class,
                    "The OSM tag value: 'cycleway', 'residential', 'ferry'.")
      .def_readonly("length_m", &fv::RoadArcInfo::length_m)
      .def_readonly("speed_kph", &fv::RoadArcInfo::speed_kph)
      .def_readonly("flags", &fv::RoadArcInfo::flags)
      .def_readonly("forward", &fv::RoadArcInfo::forward)
      .def_readonly("backward", &fv::RoadArcInfo::backward)
      .def_readonly("bicycle_allowed", &fv::RoadArcInfo::bicycle_allowed)
      .def_readonly("foot_allowed", &fv::RoadArcInfo::foot_allowed)
      .def_readonly("motor_vehicle_allowed",
                    &fv::RoadArcInfo::motor_vehicle_allowed)
      .def_readonly("bicycle_private", &fv::RoadArcInfo::bicycle_private,
                    "`access=private`, which is NOT `no`: the arc is passable "
                    "and priced up, never deleted (O5b).")
      .def_readonly("foot_private", &fv::RoadArcInfo::foot_private)
      .def_readonly("motor_vehicle_private",
                    &fv::RoadArcInfo::motor_vehicle_private)
      .def_readonly("tolled", &fv::RoadArcInfo::tolled)
      .def_readonly("ferry", &fv::RoadArcInfo::ferry)
      .def_readonly("distance_px", &fv::RoadArcInfo::distance_px)
      .def("summary", &fv::RoadArcInfo::Summary,
           "One line for a status bar. Empty when `valid` is False.")
      .def("__repr__", [](const fv::RoadArcInfo& a) {
        return a.valid ? "<RoadArcInfo " + a.Summary() + ">"
                       : std::string("<RoadArcInfo invalid>");
      });

  py::class_<fv::RoadNodeInfo>(route, "RoadNodeInfo",
      "One graph node -- a junction or a way end, and the thing a route start "
      "actually SNAPS to. Shape points are edge geometry and are not nodes.")
      .def_readonly("valid", &fv::RoadNodeInfo::valid)
      .def_readonly("node", &fv::RoadNodeInfo::node)
      .def_readonly("osm_id", &fv::RoadNodeInfo::osm_id)
      .def_readonly("position", &fv::RoadNodeInfo::position)
      .def_readonly("degree", &fv::RoadNodeInfo::degree,
                    "Arcs leaving it. ONE means a dead end, which is the "
                    "commonest shape of a bike-routing bug: two halves of a "
                    "path that were never noded together.")
      .def_readonly("restricted", &fv::RoadNodeInfo::restricted,
                    "The via node of at least one turn restriction.")
      .def_readonly("distance_px", &fv::RoadNodeInfo::distance_px)
      .def("summary", &fv::RoadNodeInfo::Summary)
      .def("__repr__", [](const fv::RoadNodeInfo& n) {
        return n.valid ? "<RoadNodeInfo " + n.Summary() + ">"
                       : std::string("<RoadNodeInfo invalid>");
      });

  route.def(
      "road_class_color",
      [](const std::string& name) {
        return FromColor(fv::RoadClassColor(fv::routing::RoadClassFromName(name)));
      },
      "road_class"_a,
      "The (r, g, b) the debug overlay draws a class in, by the class's own "
      "name. Exposed so a shell's own list of classes AGREES with the "
      "picture -- two palettes that drift apart make the picture lie.");

  route.def(
      "road_class_names",
      [] {
        py::list out;
        for (int i = 0; i < static_cast<int>(fv::routing::RoadClass::kCount); ++i) {
          out.append(std::string(
              fv::routing::RoadClassName(static_cast<fv::routing::RoadClass>(i))));
        }
        return out;
      },
      "Every class the graph knows, in file order, for a menu of them.");

  py::class_<fv::RoadGraphOverlay, fv::Overlay,
             std::shared_ptr<fv::RoadGraphOverlay>>(
      route, "RoadGraphOverlay",
      "The routing network drawn as ITSELF: every arc in view coloured by "
      "road class, a dot at every graph node, a legend of what is on screen, "
      "and a click that says what the router sees there.\n\n"
      "A DEBUG VIEW OF THE GRAPH, which is a different object from the chart "
      "under it -- and the two disagree in exactly the ways a wrong bicycle "
      "route is made of: a cycleway that is drawn and not in the graph, two "
      "halves of a path that look joined and share no node, a service road "
      "tagged bicycle=no rendered identically to the one beside it.")
      .def(py::init<std::string>(), "name"_a = "Road graph")

      .def("set_planner", &fv::RoadGraphOverlay::SetPlanner, "planner"_a,
           // BORROWED in C++; from Python the overlay keeps it alive, exactly
           // as RouteOverlay's does -- a shell that dropped its last reference
           // to the planner would otherwise leave a dangling one in here.
           py::keep_alive<1, 2>(),
           "The shell's planner. Its graph is what gets drawn, because a "
           "picture of a graph nobody routes on answers the wrong question.")
      .def("set_graph", &fv::RoadGraphOverlay::SetGraph, "graph"_a,
           "A routing.RoadGraph directly -- for a second .fvroad beside the "
           "planner's. The planner WINS when it has one.")
      .def_property_readonly("graph", &fv::RoadGraphOverlay::graph,
                             "The graph actually being drawn, or None.")
      .def("ensure_graph",
           [](fv::RoadGraphOverlay& o) { ThrowIfError(o.EnsureGraph()); },
           "Load the planner's graph NOW. Separate from `graph` because "
           "loading a continent takes seconds and a draw is a frame.")

      .def_property("show_nodes", &fv::RoadGraphOverlay::show_nodes,
                    &fv::RoadGraphOverlay::SetShowNodes,
                    "The dots. On by default -- they are half the point -- but "
                    "a dense extract at low zoom reads better without them.")
      .def_property("show_edges", &fv::RoadGraphOverlay::show_edges,
                    &fv::RoadGraphOverlay::SetShowEdges)
      .def_property("show_legend", &fv::RoadGraphOverlay::show_legend,
                    &fv::RoadGraphOverlay::SetShowLegend,
                    "The legend is TEXT, so a canvas with no default font "
                    "fails the draw. Turning it off is the answer for a shell "
                    "that has none; the network still draws.")
      .def_property("node_size_px", &fv::RoadGraphOverlay::node_size_px,
                    &fv::RoadGraphOverlay::SetNodeSizePx)
      .def_property("arc_budget", &fv::RoadGraphOverlay::arc_budget,
                    &fv::RoadGraphOverlay::SetArcBudget,
                    "The most arcs one frame draws. A guard against asking for "
                    "a continent, not a thinning rule: over budget the frame "
                    "stops early and the legend SAYS so.")
      .def_property(
          "class_filter",
          [](const fv::RoadGraphOverlay& o) {
            py::list out;
            for (const fv::routing::RoadClass k : o.class_filter()) {
              out.append(std::string(fv::routing::RoadClassName(k)));
            }
            return out;
          },
          [](fv::RoadGraphOverlay& o, const std::vector<std::string>& names) {
            std::vector<fv::routing::RoadClass> classes;
            classes.reserve(names.size());
            for (const std::string& n : names) {
              const fv::routing::RoadClass k = fv::routing::RoadClassFromName(n);
              // A name this build does not know is DROPPED rather than
              // silently turned into some class: filtering to a typo should
              // show nothing recognisable, not the wrong roads.
              if (k != fv::routing::RoadClass::kNone) classes.push_back(k);
            }
            o.SetClassFilter(std::move(classes));
          },
          "Class NAMES to draw; [] (the default) means all of them. This is "
          "how 'show me only the cycleways' is asked, and it filters the DOTS "
          "too -- a node kept only by a hidden class is not part of the "
          "network being looked at.")

      .def_property_readonly("drawn_arcs", &fv::RoadGraphOverlay::drawn_arcs)
      .def_property_readonly("drawn_nodes", &fv::RoadGraphOverlay::drawn_nodes)
      .def_property_readonly("budget_hit", &fv::RoadGraphOverlay::budget_hit)
      .def_property_readonly(
          "counts",
          [](const fv::RoadGraphOverlay& o) {
            py::dict out;
            const std::vector<uint32_t>& c = o.counts();
            for (size_t i = 0; i < c.size(); ++i) {
              if (c[i] == 0) continue;
              out[py::str(std::string(fv::routing::RoadClassName(
                  static_cast<fv::routing::RoadClass>(i))))] = c[i];
            }
            return out;
          },
          "Arcs drawn of each class in the LAST frame, by class name. Only "
          "the classes actually on screen -- the legend's own contents.")

      .def("arc_at", &fv::RoadGraphOverlay::ArcAt, "proj"_a, "x"_a, "y"_a,
           "tolerance_px"_a = 6.0,
           "What arc is under this pixel. Not a scan: the click becomes a "
           "small geographic box and only the nodes in it are measured, so it "
           "costs the same on Kiawah and on the whole of the south.")
      .def("node_at", &fv::RoadGraphOverlay::NodeAt, "proj"_a, "x"_a, "y"_a,
           "tolerance_px"_a = 8.0)
      .def("arc_info", &fv::RoadGraphOverlay::ArcInfoFor, "arc"_a)
      .def("node_info", &fv::RoadGraphOverlay::NodeInfoFor, "node"_a)
      .def("hit_test_point",
           [](fv::RoadGraphOverlay& o, const fv::MapProjection& proj, int x,
              int y, double tolerance_px) {
             std::vector<fv::app::HitItem> out;
             o.HitTestPoint(proj, fv::PixelPoint{x, y}, tolerance_px, out);
             return out;
           },
           "proj"_a, "x"_a, "y"_a, "tolerance_px"_a = 8.0,
           "A NODE comes first and an arc after it -- every node sits ON an "
           "arc, so a nearest-wins rule would make the dots unclickable.")
      .def_static("feature_is_arc", &fv::RoadGraphOverlay::FeatureIsArc,
                  "feature"_a,
                  "HitItem.feature is PACKED, not minted: a node id and an arc "
                  "index are the graph's own identifiers. The top bit says "
                  "which kind it is.")
      .def_static("feature_index", &fv::RoadGraphOverlay::FeatureIndex,
                  "feature"_a)

      .def_property_readonly("has_projection",
                             &fv::RoadGraphOverlay::has_projection)
      .def_property_readonly_static(
          "TYPE_ID", [](py::object) { return fv::RoadGraphOverlay::kTypeId; });

  route.def(
      "register_road_graph_overlay_type",
      [](fv::app::OverlayTypeRegistry& registry, const fv::RoutePlanner* planner) {
        ThrowIfError(fv::RegisterRoadGraphOverlayType(registry, planner));
      },
      "registry"_a, "planner"_a = nullptr, py::keep_alive<1, 2>(),
      "Registers `fv.roadgraph` as a STATIC type, so the Overlays menu can "
      "toggle it. Like register_route_overlay_type it is not part of "
      "app.register_builtin_types() and cannot be: it names the router.\n\n"
      "Order 900 -- UNDER the route (1000), because a debug view of the roads "
      "a route follows must not hide the answer it was opened to explain.");

  route.attr("ROUTE_TYPE_ID") = std::string(fv::kRouteTypeId);
  route.attr("ROUTE_EXTENSION") = std::string(fv::kRouteExtension);
  route.attr("ROUTE_FORMAT") = std::string(fv::kRouteFormat);
  route.attr("ROUTE_VERSION") = fv::kRouteVersion;
}

}  // namespace pyfvw
