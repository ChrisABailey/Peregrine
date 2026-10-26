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
//   * the stretches a calculated route rides on the beach (`RoutePlan::beach`)
//     have a sand casing instead of the white one;
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
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fv_route_doc.h"
#include "fv_route_planner.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
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
                     public app::EditHint,
                     public app::SearchProvider,
                     public app::Properties,
                     public app::ItemInfo {
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
  // The casing under the beach stretches: darker than the chart's pale sand
  // fill, so the stretch still stands out where it crosses the beach.
  static const FvColor kBeachCasingColor;
  // The previous line after a re-plan: translucent grey, no casing.
  static const FvColor kGhostColor;
  // The beach wind arrows, opaque: against the rider, with the rider, and
  // under the threshold.
  static const FvColor kHeadwindColor;
  static const FvColor kTailwindColor;
  static const FvColor kCalmWindColor;
  /// Length of a beach wind arrow, tail to tip, in points at wind scale 1.
  static constexpr double kWindArrowLength = 14.0;

  /// Slope colours. `SlopeColor` returns its `flat` argument on level ground; a climb is yellow up
  /// to `kSlopeEasyPercent` and blends to red at `kSlopeSteepPercent`; a
  /// descent blends from light to dark green at `kSlopeSteepDescentPercent`.
  static const FvColor kClimbEasyColor;
  static const FvColor kClimbSteepColor;
  static const FvColor kDescentGentleColor;
  static const FvColor kDescentSteepColor;
  /// Grades within this many percent of level count as flat.
  static constexpr double kSlopeFlatPercent = 1.5;
  static constexpr double kSlopeEasyPercent = 3.0;
  static constexpr double kSlopeSteepPercent = 8.0;
  static constexpr double kSlopeSteepDescentPercent = 6.0;
  /// The shortest distance a slope colour's grade is measured over, metres.
  /// Shorter arcs take the grade of the window centred on them, so a 5 m arc
  /// with a 0.3 m step in the elevation model is not drawn as a wall.
  static constexpr double kSlopeWindowM = 50.0;
  /// The slope halo each side of the line, in points: `kSlopeHaloPt` in the
  /// slope colour, then `kSlopeFringePt` of it tinted halfway to the casing.
  static constexpr double kSlopeHaloPt = 2.0;
  static constexpr double kSlopeFringePt = 1.0;

  /// The line colour for a grade in percent (positive is uphill), with
  /// `flat` for level ground.
  static FvColor SlopeColor(double grade_percent, FvColor flat);
  /// The grade, percent, of each segment between consecutive points of
  /// `profile`, measured over the segment or over `window_m` centred on it,
  /// whichever is longer, and kept within the route. One entry per segment.
  static std::vector<double> SegmentGrades(const std::vector<routing::ElevationPoint>& profile,
                                           double window_m = kSlopeWindowM);

  app::Persistence* AsPersistence() override { return this; }
  app::HitTest* AsHitTest() override { return this; }
  app::SnapTo* AsSnapTo() override { return this; }
  app::EditTarget* AsEditTarget() override { return this; }
  app::SearchProvider* AsSearch() override { return this; }
  app::Properties* AsProperties() override { return this; }
  app::EditHint* AsEditHint() override { return this; }
  app::ItemInfo* AsItemInfo() override { return this; }
  std::string EditHintAt(PixelPoint p) override;
  /// The route's name, length and legs when `p` is on a waypoint or the line;
  /// Edit Route… selects the waypoint hit, if any.
  std::optional<app::ItemInfoSheet> InfoAt(const MapProjection& proj, PixelPoint p,
                                           double tolerance_px) override;

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

  /// Whether the route may use the beach. A document field, so it dirties.
  void SetBeach(RouteBeach beach);
  RouteBeach beach() const { return doc_.beach(); }

  /// Whether the route avoids climbing. A document field, so it dirties.
  void SetAvoidHills(bool avoid);
  bool avoid_hills() const { return doc_.avoid_hills(); }

  /// The climb multiplier an "Avoid hills" plan uses (`[routing]
  /// avoid_hills_penalty`). A shell setting, not a document field.
  void SetAvoidHillsPenalty(double penalty) { avoid_hills_penalty_ = penalty; }

  /// Draws a calculated route with a halo coloured by grade (`SlopeColor`)
  /// in place of its casing when its plan has elevation; the line itself
  /// keeps its colour and style, and flat ground keeps the casing. Not a
  /// document field: the `slope_colors` property.
  void SetSlopeColors(bool on) { slope_colors_ = on; }
  bool slope_colors() const { return slope_colors_; }

  /// A previous plan's legs, drawn in `kGhostColor` under the route so a
  /// re-plan can be compared with the line it replaced. Not a document field;
  /// `ClearRoads` (and so any waypoint edit) drops it.
  void SetGhost(std::vector<std::vector<GeoPoint>> legs) { ghost_ = std::move(legs); }
  void ClearGhost() { ghost_.clear(); }
  const std::vector<std::vector<GeoPoint>>& ghost() const { return ghost_; }
  double avoid_hills_penalty() const { return avoid_hills_penalty_; }

  /// True when the planner's graph carries elevation, or is not loaded yet
  /// and so may. False without a planner.
  bool MayHaveElevation() const;

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
  // saved as a cycle route replans as one; likewise its beach setting when
  // `options.beach` is kProfileDefault. Returns false for anything short of
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

  /// The user's symbol size, applied to the beach wind arrows on top of the
  /// device scale: their length, spacing and clearance from the line.
  void SetWindArrowScale(double s) { wind_scale_ = s > 0.0 ? s : 1.0; }
  double wind_arrow_scale() const { return wind_scale_; }

  // Waypoint names. On by default, unlike PointOverlay: a route has a handful
  // of waypoints and their ORDER is the document, so an unlabelled diamond is
  // a marker whose meaning has been thrown away.
  void SetShowLabels(bool on) { show_labels_ = on; }
  bool show_labels() const { return show_labels_; }

  /// Off: `OnDraw` draws the line and status but no waypoint diamonds or
  /// names, for a shell that shows them as upright sprites over a tilted map.
  /// Waypoints are still placed for the hit test.
  void SetDrawMarkers(bool on) { draw_markers_ = on; }
  bool draw_markers() const { return draw_markers_; }

  /// Stamps waypoint `index`'s diamond and name centred on surface pixel
  /// (x, y): what `OnDraw` stamps for it, minus placement. kInvalidArg when
  /// `index` is past the last waypoint.
  Status DrawMarkerAt(const MapProjection& proj, ICanvas& canvas, size_t index,
                      double x, double y);

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

  // --- beach wind ----------------------------------------------------------

  /// The wind on one beach stretch of the plan.
  struct BeachWindMark {
    /// Index into `plan().beach`.
    size_t stretch = 0;
    /// The direction the wind blows towards, degrees true.
    double toward_deg = 0.0;
    /// Component along the direction of travel, m/s; positive is a tailwind.
    double along_mps = 0.0;
    /// The direction the beach faces out to sea, degrees true. Arrows sit
    /// on the landward side of the line; NaN centres them on it.
    double seaward_deg = std::numeric_limits<double>::quiet_NaN();
  };

  /// Arrows along the plan's beach stretches, pointing where the wind
  /// blows: headwind or tailwind colour when |along_mps| exceeds `warn_mps`,
  /// grey otherwise. Not a document field. Cleared whenever the
  /// plan is replaced, since the marks index its stretches.
  void SetBeachWind(std::vector<BeachWindMark> marks, double warn_mps);
  const std::vector<BeachWindMark>& beach_wind() const { return wind_marks_; }

  enum class WindClass { kCalm, kHead, kTail };
  /// One wind arrow as the last `OnDraw` placed it: its centre in surface
  /// pixels and the unit vector, in surface pixels, the wind blows along.
  struct DrawnChevron {
    double x = 0.0;
    double y = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    WindClass kind = WindClass::kCalm;
  };
  const std::vector<DrawnChevron>& drawn_wind() const { return drawn_wind_; }

  // --- Properties ----------------------------------------------------------

  /// The drawing options, the `[route]` settings section: a calculated route's
  /// line colour, width and style, its casing and beach casing, slope colours, and whether
  /// waypoint names and the status line are drawn. Not document fields: they
  /// neither save nor dirty.
  const std::vector<app::PropertySpec>& Describe() const override;
  Status GetProperty(const std::string& key, app::PropertyValue* out) const override;
  Status SetProperty(const std::string& key, const app::PropertyValue& value) override;

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
  double avoid_hills_penalty_ = kDefaultAvoidHillsPenalty;

  RoutePlan plan_;
  bool has_plan_ = false;

  std::string selected_;
  bool show_labels_ = true;
  bool show_status_ = true;
  FvColor road_color_ = kRoadColor;
  int road_width_ = 6;
  long long road_style_ = 0;  // index into the road_style choices
  FvColor casing_color_ = kCasingColor;
  int casing_width_ = 4;      // extra pixels beyond the line; 0 draws none
  FvColor beach_casing_color_ = kBeachCasingColor;
  bool slope_colors_ = false;
  std::vector<std::vector<GeoPoint>> ghost_;
  bool draw_markers_ = true;
  double dpi_scale_ = 1.0;
  double wind_scale_ = 1.0;
  LineKind leg_kind_ = LineKind::kGreatCircle;

  OverlayManager* manager_ = nullptr;
  std::unique_ptr<RouteEditSession> edit_;

  std::vector<BeachWindMark> wind_marks_;
  double wind_warn_mps_ = 0.0;
  std::vector<DrawnChevron> drawn_wind_;
  /// The casing pass of a slope-coloured route: halo and fringe by grade.
  Status DrawSlopeHalo(GeoDraw& draw);
  /// The wind arrows along each marked stretch of `line`, the plan's legs joined.
  Status DrawBeachWind(const MapProjection& proj, ICanvas& canvas,
                       const std::vector<GeoPoint>& line);

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
    FvColor color;  // marker fill: start, via or end
  };
  std::vector<DrawnPoint> drawn_;

  /// The fill for waypoint `index` of `count`: start, via or end.
  static FvColor MarkerColor(size_t index, size_t count);
  /// One waypoint's diamond and name, through `draw`.
  Status DrawMarker(GeoDraw& draw, const DrawnPoint& d);

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
