// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RouteKit/fv_route_overlay.h — the route on the map (P5).
//
// `fv::PointOverlay` was the first C++ FILE overlay; this is the second, and
// the first that DRAWS SOMETHING COMPUTED. It is route.py's overlay with the
// editor left behind: the document, the plan, the picture and the pick, in
// C++ where Pippin and PythonView and anything after them can reach it.
//
// WHAT IS DELIBERATELY NOT HERE, and it is most of route.py's line count: the
// editor. No armed add-mode, no drag, no undo stack, no per-key bindings.
// route.py keeps all of that and is left untouched; Pippin's v1 editing
// surface is `SetWaypoints` — WHOLESALE REPLACEMENT — which is exactly what a
// sheet-driven UI produces and what the plan asked for. A later session that
// wants click-and-drag on the phone adds an `OverlayEditor`, the same shape
// `route.py` already has, rather than growing this class.
//
// THE LOOK IS route.py's, to the pixel it can be:
//
//   * a CALCULATED route is blue with a WHITE CASING so it reads over a chart,
//     and DASHED when it was priced as a bicycle route — the mode is visible
//     in the LINE and not only in a line of small text;
//   * an UNCALCULATED route is the overlay's own colour (red by default),
//     straight legs, no casing: those are the waypoints joined, not a route
//     anybody can ride, and the two must not be confusable at a glance;
//   * waypoints are builtin diamonds in the route's colour and the labels are
//     haloed, so a name over dense linework is still readable;
//   * the selected waypoint is a RenderState (G4), not a second colour: it
//     keeps the route's colour and gains a halo of its own silhouette, because
//     turning it yellow would throw away the one thing saying which route it
//     belongs to.
//
// THE LEGS ARE GEOGRAPHIC LINES (G1), not lines between projected pixels: a
// great circle is what the route IS, densified to ~20-pixel chords and clipped
// in geographic space first. It is visibly the same line at harbour scale and
// visibly a different one across an ocean, which is the honest way round for a
// default. A CALCULATED leg is drawn kSimple instead — road geometry is
// already the road, and asking for a great circle between two points four
// metres apart would cost the geodesy and return the same line.
//
// THE TWO SYMBOL-SIZE REFERENCES, which P4 said P5 would have to tell apart.
// `GeoDraw::symbol_dpi_scale` closes the ledger's symbol-DPI gap for whoever
// sets it, and Pippin sets it to "one iOS point per authored pixel" for the
// ownship. A route's diamonds are the same KIND of thing — the app's own
// furniture, sized in the units the app draws its buttons in — so they take
// the app's unit too, and `SetSymbolDpiScale` is how a shell says so. What
// they are NOT is chart symbology: a GeoSym or S-52 symbol is pinned against
// paper at 0.32 mm per authored pixel, and a route line drawn to that
// reference would be sized by the chart rather than by the device the finger
// is on. Two references, one mechanism, and the argument for each belongs at
// its own site. Default 1.0 — an overlay does not know the device, and a shell
// that says nothing gets exactly what it got before.
//
// THE EDITOR IS HERE NOW, and the second paragraph of this header used to say
// it never would be. `SetWaypoints` is still the wholesale surface a
// sheet-driven UI wants and is still what Pippin drives; what has changed is
// that the GESTURES — select, drag, armed add, delete, undo — are no longer
// route.py's alone. They live in `RouteEditSession` (fv_route_edit.h), this
// overlay owns one, and `AsEditTarget()` plus the four `OnMouse*`/`OnKeyDown`
// overrides are how the stack reaches it. The reason it moved is not that C++
// needed it: it is that the desktop had an editor and the phone had none, and
// two shells must not grow two answers to "what does dragging a waypoint mean".
//
// Three things came with it and are worth knowing here rather than there:
//
//   * the overlay KEEPS A COPY of the projection each frame was drawn with
//     (`last_projection`). The SPI hands a projection to `OnDraw` and none to
//     `OnMouseDown`, so an overlay that turns a click into a position has to
//     keep one — and a copy, because the borrowed reference's lifetime is the
//     caller's business (PythonView replaces its whole engine, and its
//     projection with it, on any catalog change);
//   * it knows its `OverlayManager`, for exactly two things — mouse capture
//     during a drag, and the snap walk. Null is legal and means neither;
//   * `SetLegKind` exists because route.py's "g" key does, and because seeing
//     great circle, rhumb and straight-in-projection switch over one route is
//     the quickest way to know the geodesy is actually running.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fv_route_doc.h"
#include "fv_route_planner.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/search.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/canvas/canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/geo.h"
#include "fvkit/overlay/overlay.h"
#include "fvkit/proj.h"
#include "fvkit/symbol/builtin.h"

namespace fv {

class OverlayManager;

// The editing half, in fv_route_edit.h. A forward declaration and a
// unique_ptr, PointOverlay's arrangement for `EmbeddedSymbolLibrary`: the
// session needs the complete overlay type, so it cannot be a member by value
// without a cycle, and a shell that only DRAWS a route should not have to
// parse the editor to include this header.
class RouteEditSession;

class RouteOverlay : public Overlay,
                     public app::Persistence,
                     public app::HitTest,
                     public app::SnapTo,
                     public app::EditTarget,
                     public app::SearchProvider {
 public:
  explicit RouteOverlay(std::string name = "Route");
  ~RouteOverlay() override;

  // The registered type id and the document extension — route.py's, because
  // this opens route.py's documents.
  static const char kTypeId[];      // "fv.route"
  static const char kExtension[];   // "fvrte"

  // The line a CALCULATED route wears. Blue over white, route.py's numbers.
  static const FvColor kRoadColor;
  static const FvColor kCasingColor;

  app::Persistence* AsPersistence() override { return this; }
  app::HitTest* AsHitTest() override { return this; }
  app::SnapTo* AsSnapTo() override { return this; }
  app::EditTarget* AsEditTarget() override { return this; }
  app::SearchProvider* AsSearch() override { return this; }

  // --- the editor ----------------------------------------------------------

  // The gestures, the armed modes and the undo stack. Always present — an
  // overlay nobody edits simply never calls into it — so this never returns
  // null and a caller does not have to check.
  RouteEditSession& edit() { return *edit_; }
  const RouteEditSession& edit() const { return *edit_; }

  // The stack, for mouse CAPTURE and for the snap walk, and for nothing else.
  // An overlay does not otherwise know its manager and should not want to; a
  // drag is precisely the case capture exists for, and a snap is a question
  // about the whole stack that only the stack can answer. Null is legal: the
  // gesture still works, it is just uncaptured and unsnapped.
  void SetManager(OverlayManager* manager) { manager_ = manager; }
  OverlayManager* manager() const { return manager_; }

  // A copy of the projection the last frame was drawn with, and whether there
  // has been one. See the header note: the input SPI carries no projection, so
  // an overlay that turns a click into a position keeps the one it drew with.
  bool has_projection() const { return have_proj_; }
  const MapProjection& last_projection() const { return last_proj_; }

  // --- the document --------------------------------------------------------

  const RouteDoc& doc() const { return doc_; }

  // The editing surface, and all of it. Replaces every waypoint, marks the
  // document dirty, and DROPS THE PLAN — the drawn road belongs to the
  // waypoints it was computed from, and keeping it would leave a stale road
  // under a moved marker, which reads as a bug in the router.
  void SetWaypoints(std::vector<RouteWaypoint> waypoints);

  const std::vector<RouteWaypoint>& waypoints() const {
    return doc_.waypoints();
  }

  // The route's colour, which is what an UNCALCULATED leg and every waypoint
  // marker are drawn in. A document change, so it dirties.
  void SetColor(FvColor color);
  FvColor color() const { return doc_.color(); }

  // The rule-file profile this route is priced with. A document field (it is
  // saved), so it dirties.
  void SetProfile(std::string profile);
  const std::string& profile() const { return doc_.profile(); }

  // The selected waypoint's LABEL, or empty. Selection is NOT a document
  // change: selecting does not dirty the overlay.
  const std::string& selected() const { return selected_; }
  void SetSelected(std::string label);

  // --- the plan ------------------------------------------------------------

  // Attach a planner and route. `planner` is BORROWED — a shell owns one
  // planner (one graph, one rule file) and hands it to every route overlay it
  // opens, which is the whole reason the graph is not loaded in here.
  void SetPlanner(const RoutePlanner* planner) { planner_ = planner; }
  const RoutePlanner* planner() const { return planner_; }

  // Plan over the current waypoints and keep the answer to draw. `options`
  // takes the document's own profile when its `profile` is empty, so a route
  // saved as a cycle route replans as one. Returns false for anything short of
  // a through route — the same answer `follow_roads` gives — and the plan is
  // still kept and still drawn, because the fallback legs are worth seeing.
  bool FollowRoads(RoutePlanOptions options = RoutePlanOptions{});

  // Back to straight legs. What Esc does in route.py.
  void ClearRoads();

  bool has_plan() const { return has_plan_; }
  const RoutePlan& plan() const { return plan_; }

  // The one line of map the route explains itself on, or empty. Drawn by
  // `OnDraw` unless `SetShowStatus(false)`.
  const std::string& status() const { return plan_.status; }
  void SetShowStatus(bool on) { show_status_ = on; }

  // --- drawing -------------------------------------------------------------

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override;

  // One authored symbol pixel per device unit. See the note at the top of this
  // header: a route's markers are the APP's furniture and take the app's unit,
  // which is a different reference from chart symbology's 0.32 mm.
  void SetSymbolDpiScale(double s) { dpi_scale_ = s > 0.0 ? s : 1.0; }
  double symbol_dpi_scale() const { return dpi_scale_; }

  // Waypoint names. On by default, unlike PointOverlay: a route has a handful
  // of waypoints and their ORDER is the document, so an unlabelled diamond is
  // a marker whose meaning has been thrown away.
  void SetShowLabels(bool on) { show_labels_ = on; }
  bool show_labels() const { return show_labels_; }

  // How an UNCALCULATED leg is filled in between its two waypoints (G1).
  // kGreatCircle is the default because it is what the route IS — a leg is
  // flown, not drawn, and the straight line between two projected pixels is an
  // artifact of the projection rather than a path.
  //
  // A CALCULATED leg ignores this and is always kSimple: road geometry is
  // already the road, and asking for a great circle between two points four
  // metres apart would cost the geodesy and return the same line.
  //
  // Not a document field: it is how the route is being LOOKED at, not what the
  // route is, so it neither saves nor dirties.
  void SetLegKind(LineKind kind) { leg_kind_ = kind; }
  LineKind leg_kind() const { return leg_kind_; }

  // --- Persistence ---------------------------------------------------------

  Status FileNew() override;
  Status FileOpen(const std::string& spec) override;
  Status FileSaveAs(const std::string& spec, int format_index) override;
  bool SupportsRevert() const override { return true; }
  Status Revert(const std::string& spec) override;

  // --- HitTest -------------------------------------------------------------

  // `HitItem::feature` is a minted handle, not a packing (A5's rule), because
  // a waypoint's identity in this document is its LABEL and a label is not a
  // number. Ids are assigned on demand and never reused, so one stays valid
  // for the life of the overlay; `LabelForFeature` translates back.
  void HitTestPoint(const MapProjection& proj, PixelPoint p,
                    double tolerance_px, std::vector<app::HitItem>& out) override;

  std::string LabelForFeature(uint64_t feature) const;

  // --- SnapTo --------------------------------------------------------------

  // A ROUTE'S WAYPOINTS ARE SNAP TARGETS TOO, and this is the second
  // implementer of the capability rather than a feature anybody asked for by
  // name (Pippin P19). The reason it exists is that "snap to a point" is not a
  // property of the point overlay: it is a property of the SPI, and an
  // interface with exactly one implementer has not been shown to be one. What
  // it buys a rider is real anyway -- starting a route at the last one's end,
  // or hanging a via off a waypoint already placed, without re-aiming at it.
  //
  // ONLY WAYPOINTS, never a position along the drawn road. A snap returns a
  // place that already existed and had a name; the nearest point on a polyline
  // is a computed coordinate with nothing behind it, and offering it here
  // would make the capability mean two different things.
  //
  // Like HitTestPoint this answers about what was DRAWN, so an overlay nobody
  // has rendered snaps to nothing -- and for the same reason: a coordinate
  // cannot jump to something that is not on the screen.
  void SnapToPoint(const MapProjection& proj, PixelPoint p,
                   double tolerance_px,
                   std::vector<app::SnapToItem>& out) override;

  // --- SearchProvider ------------------------------------------------------

  // A route answers to TWO names and they are different kinds of thing, so it
  // returns both kinds of row:
  //
  //   * the ROUTE, matched on its document name, whose `bounds` is the box its
  //     waypoints fit in and whose `feature` is 0 -- minted ids start at 1, so
  //     zero is free and means "the route itself" rather than a waypoint of
  //     it. This is the row that makes "show me the Kiawah loop" work, and its
  //     bounds are the only reason `bounds` is on SearchResult at all;
  //   * each WAYPOINT, matched on its label, carrying the SAME feature id
  //     HitTestPoint mints, so `LabelForFeature` translates a search result
  //     back exactly as it translates a hit.
  //
  // Like PointOverlay's and unlike this class's own HitTestPoint, it answers
  // about the DOCUMENT: no projection, no drawn frame, no visibility. A route
  // that has never been rendered is still findable, which is the whole point
  // of the capability being separate from the pick.
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override;

  // --- EditTarget, and the input SPI --------------------------------------
  //
  // All eight are one-line forwards to `edit()`. They are here rather than
  // inline in the session because the STACK calls them: `EditorManager`
  // brackets the focus pair and `OverlayManager` routes the events, and both
  // reach an overlay, never a session.

  void EnterEditFocus() override;
  void ReleaseEditFocus() override;
  bool CanUndo() const override;
  void Undo() override;
  bool CanRedo() const override;
  void Redo() override;

  bool OnMouseDown(const MouseEvent& e) override;
  bool OnMouseMove(const MouseEvent& e) override;
  bool OnMouseUp(const MouseEvent& e) override;
  bool OnKeyDown(const KeyEvent& e) override;

 private:
  BuiltinSymbolLibrary* LibraryFor(const FvColor& c);
  uint64_t FeatureIdFor(const std::string& label);

  RouteDoc doc_;
  const RoutePlanner* planner_ = nullptr;

  RoutePlan plan_;
  bool has_plan_ = false;

  std::string selected_;
  bool show_labels_ = true;
  bool show_status_ = true;
  double dpi_scale_ = 1.0;
  LineKind leg_kind_ = LineKind::kGreatCircle;

  OverlayManager* manager_ = nullptr;
  std::unique_ptr<RouteEditSession> edit_;

  // The projection the last OnDraw ran with, and whether there has been one.
  // Set beside `drawn_` below, and for the same reason: the picture and the
  // things asked ABOUT the picture have to come from one frame.
  MapProjection last_proj_;
  bool have_proj_ = false;

  // Where the last OnDraw actually put each waypoint, in surface pixels. The
  // hit test reads THIS rather than re-projecting: a pick has to agree with
  // the picture, and the picture is what the user aimed at.
  struct DrawnPoint {
    std::string label;
    double x = 0.0;
    double y = 0.0;
  };
  std::vector<DrawnPoint> drawn_;

  // Keyed by packed RGBA, exactly PointOverlay's arrangement: a builtin
  // library is thirteen literals with no files behind it, and a route wants
  // one per colour it draws in.
  std::map<uint32_t, std::unique_ptr<BuiltinSymbolLibrary>> libraries_;

  std::map<std::string, uint64_t> feature_ids_;
  uint64_t next_feature_ = 1;
};

// Registers `fv.route` with a shell's type registry, so File > Open reaches a
// `.fvrte` and the session layer can make one. NOT part of
// `RegisterBuiltinOverlayTypes` and it cannot be: that function lives in fvkit,
// and fvkit is exactly what may not link the router. A shell calls both.
//
// `planner` is BORROWED and may be null. It is handed to every overlay the
// factory makes, which is what lets a shell own ONE graph and one rule file
// across however many routes are open — the graph is the expensive thing, not
// the overlay.
Status RegisterRouteOverlayType(app::OverlayTypeRegistry& registry,
                                const RoutePlanner* planner = nullptr);

}  // namespace fv
