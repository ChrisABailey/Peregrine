// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_overlay.h"

#include <algorithm>
#include <cmath>

#include "fv_route_edit.h"

namespace fv {

const char RouteOverlay::kTypeId[] = "fv.route";
const char RouteOverlay::kExtension[] = "fvrte";

// route.py's ROAD_COLOR / CASING_COLOR.
const FvColor RouteOverlay::kRoadColor{40, 90, 210, 255};
const FvColor RouteOverlay::kCasingColor{255, 255, 255, 255};

namespace {

// The diamond, in nominal symbol pixels. route.py stamps `fv.diamond` at
// scale 1.6 over a 9-pixel builtin, so this is 14.4 px across and its half is
// the 7 route.py's hit test spells as a literal.
constexpr double kMarkerScale = 1.6;

}  // namespace

RouteOverlay::RouteOverlay(std::string name) : Overlay(std::move(name)) {
  doc_.set_name(Name());
  edit_.reset(new RouteEditSession(*this));
}

RouteOverlay::~RouteOverlay() = default;

// ---------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------

void RouteOverlay::SetWaypoints(std::vector<RouteWaypoint> waypoints) {
  doc_.set_waypoints(std::move(waypoints));
  // A plan belongs to the waypoints it was computed from. Keeping it here
  // would leave a stale road under a moved marker, which reads as a bug in the
  // router rather than as a route nobody has recomputed.
  ClearRoads();
  // Selection by LABEL survives a replacement that kept the label, which is
  // what a sheet-driven edit usually is.
  bool still_there = false;
  for (const RouteWaypoint& w : doc_.waypoints()) {
    if (w.label == selected_) { still_there = true; break; }
  }
  if (!still_there) selected_.clear();
  set_dirty(true);
}

void RouteOverlay::SetColor(FvColor color) {
  doc_.set_color(color);
  set_dirty(true);
}

void RouteOverlay::SetProfile(std::string profile) {
  doc_.set_profile(std::move(profile));
  set_dirty(true);
}

void RouteOverlay::SetSelected(std::string label) {
  selected_ = std::move(label);
}

// ---------------------------------------------------------------------------
// The plan
// ---------------------------------------------------------------------------

bool RouteOverlay::FollowRoads(RoutePlanOptions options) {
  if (planner_ == nullptr) {
    plan_ = RoutePlan{};
    plan_.status = "no road graph configured";
    plan_.error = Status::Error(kInvalidArg, plan_.status);
    has_plan_ = false;
    return false;
  }
  // The document's own profile is the default, so a route saved as a cycle
  // route replans as one without the caller having to remember. A profile
  // named on the call wins, exactly as it does in route.py.
  if (options.profile.empty()) options.profile = doc_.profile();

  std::vector<GeoPoint> stops;
  stops.reserve(doc_.waypoints().size());
  for (const RouteWaypoint& w : doc_.waypoints()) stops.push_back(w.position);

  plan_ = planner_->Plan(stops, options);
  // A plan with no legs draws nothing, so it is not a plan to keep: the
  // straight legs are the better picture and the status line still says why.
  has_plan_ = !plan_.legs.empty();
  return plan_.found;
}

void RouteOverlay::ClearRoads() {
  plan_ = RoutePlan{};
  has_plan_ = false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

BuiltinSymbolLibrary* RouteOverlay::LibraryFor(const FvColor& c) {
  const uint32_t key = (uint32_t)c.r << 24 | (uint32_t)c.g << 16 |
                       (uint32_t)c.b << 8 | (uint32_t)c.a;
  auto it = libraries_.find(key);
  if (it != libraries_.end()) return it->second.get();
  std::unique_ptr<BuiltinSymbolLibrary> lib(new BuiltinSymbolLibrary());
  lib->SetColor(c);
  BuiltinSymbolLibrary* raw = lib.get();
  libraries_.emplace(key, std::move(lib));
  return raw;
}

Status RouteOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  GeoDraw draw(proj, &canvas, LibraryFor(doc_.color()));
  draw.SetSymbolDpiScale(dpi_scale_);

  // Where the markers land, recorded for the hit test: a pick has to agree
  // with the picture, and the picture is what the user aimed at. Rebuilt every
  // frame, and a waypoint that will not project is simply absent from it.
  last_proj_ = proj;
  have_proj_ = true;

  drawn_.clear();
  drawn_.reserve(doc_.waypoints().size());
  for (const RouteWaypoint& w : doc_.waypoints()) {
    double sx = 0, sy = 0;
    if (!proj.GeoToSurface(w.position, &sx, &sy).ok()) continue;
    drawn_.push_back(DrawnPoint{w.label, sx, sy});
  }

  Status s = Status::Ok();
  if (has_plan_) {
    // A CALCULATED route: blue over a white casing, dashed when it was priced
    // as a bicycle route. The road geometry IS the road, so kSimple — there is
    // nothing to interpolate between two points a few metres apart.
    //
    // THE WIDTHS ARE DEVICE PIXELS, which is why they are what they are. A
    // 3-pixel line is 3 points on a desktop and ONE point on a 3x phone, where
    // it disappears under the map it is drawn over — Chris rode with it and
    // asked for twice the width (2026-08-19). Doubled together with the
    // casing, so the white still shows the same share of itself past the blue.
    GeoLineStyle style = PresetGeoLine(
        plan_.is_bicycle ? line_preset::kDash : line_preset::kSolid,
        kRoadColor, 6);
    AddCasing(&style, kCasingColor, 4);
    for (const std::vector<GeoPoint>& leg : plan_.legs) {
      if (leg.size() < 2) continue;
      s = draw.DrawGeoPolyline(leg, LineKind::kSimple, style);
      if (!s.ok()) return s;
    }
  } else if (doc_.waypoints().size() >= 2) {
    // An UNCALCULATED route: the overlay's own colour, no casing, and GREAT
    // CIRCLE legs because that is what the route IS — a leg is flown, not
    // drawn, and the straight line between two projected pixels is an artifact
    // of the projection rather than a path.
    //
    // Drawn from the DOCUMENT and not from `drawn_`: the latter has dropped
    // any waypoint that would not project, and joining across the hole would
    // invent a leg that is not in the route.
    std::vector<GeoPoint> pts;
    pts.reserve(doc_.waypoints().size());
    for (const RouteWaypoint& w : doc_.waypoints()) pts.push_back(w.position);
    s = draw.DrawGeoPolyline(pts, leg_kind_, SolidGeoLine(doc_.color(), 4));
    if (!s.ok()) return s;
  }

  for (const DrawnPoint& d : drawn_) {
    // G4: selection is a RENDER STATE, not a second colour. The marker keeps
    // the route's colour and gains a halo of its own silhouette, so it still
    // says which route it belongs to.
    draw.SetState(!selected_.empty() && d.label == selected_
                      ? RenderState::kHighlighted
                      : RenderState::kNormal);
    s = draw.DrawSymbolAtPixel(
        d.x, d.y, builtin_symbol::kDiamond,
        PointSymbolStyle{true, builtin_symbol::kDiamond, 0.0, kMarkerScale});
    if (!s.ok()) return s;

    if (show_labels_ && !d.label.empty()) {
      // The NAME is not highlighted; the marker is. Same rule PointOverlay
      // follows, for the same reason.
      draw.SetState(RenderState::kNormal);
      LabelStyle ls;
      ls.valid = true;
      ls.style.size = 12.0;
      ls.style.color = FvColor{0, 0, 0, 255};
      ls.dx = 10;
      ls.dy = -10;
      ls.halo_width = 1.0;
      ls.halo_color = FvColor{255, 255, 255, 255};
      s = draw.DrawLabelAtPixel(d.x, d.y, d.label, ls);
      if (!s.ok()) return s;
    }
  }
  draw.SetState(RenderState::kNormal);

  if (show_status_ && !plan_.status.empty()) {
    // The one line of map a route gets to explain itself on. Drawn straight on
    // the canvas rather than through GeoDraw: it is not on the earth, and it
    // is not a label that should ever be picked.
    TextStyle ts;
    ts.size = 13.0;
    ts.color = kRoadColor;
    s = canvas.DrawTextString(plan_.status, 10, 20, ts);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

Status RouteOverlay::FileNew() {
  doc_.Reset();
  doc_.set_name(Name());
  selected_.clear();
  ClearRoads();
  // A history that reached back past a File > New would restore waypoints into
  // a document they were never in.
  edit_->ClearHistory();
  edit_->SetAdding(false);
  // Not dirty: there is nothing in it to lose.
  set_dirty(false);
  return Status::Ok();
}

Status RouteOverlay::FileOpen(const std::string& spec) {
  RouteDoc doc;
  const Status s = doc.Read(spec);
  if (!s.ok()) return s;
  doc_ = std::move(doc);
  // The DOCUMENT's own name, not the path: an overlay list should say "Ruddy
  // Turnstone to the beach", not "/tmp/x.fvrte". An unnamed document keeps the
  // overlay's own name, which the reader has already left alone.
  if (!doc_.name().empty()) SetName(doc_.name());
  selected_.clear();
  ClearRoads();
  edit_->ClearHistory();
  edit_->SetAdding(false);
  set_dirty(false);
  return Status::Ok();
}

Status RouteOverlay::FileSaveAs(const std::string& spec, int format_index) {
  if (format_index != 0) {
    return Status::Error(kInvalidArg,
                         "routes have one format (0), asked for " +
                             std::to_string(format_index));
  }
  // The overlay's name is what a user renamed in the overlay list, so it is
  // the one that goes to disk — the document field follows the overlay and not
  // the other way round.
  doc_.set_name(Name());
  const Status s = doc_.Write(spec);
  if (!s.ok()) return s;
  set_dirty(false);
  return Status::Ok();
}

Status RouteOverlay::Revert(const std::string& spec) {
  const Status s = FileOpen(spec);
  if (!s.ok()) return s;
  set_dirty(false);
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// HitTest
// ---------------------------------------------------------------------------

uint64_t RouteOverlay::FeatureIdFor(const std::string& label) {
  auto it = feature_ids_.find(label);
  if (it != feature_ids_.end()) return it->second;
  const uint64_t id = next_feature_++;
  feature_ids_.emplace(label, id);
  return id;
}

std::string RouteOverlay::LabelForFeature(uint64_t feature) const {
  for (const auto& kv : feature_ids_) {
    if (kv.second == feature) return kv.first;
  }
  return std::string();
}

void RouteOverlay::HitTestPoint(const MapProjection& proj, PixelPoint p,
                                double tolerance_px,
                                std::vector<app::HitItem>& out) {
  (void)proj;
  // Hit-tests what was DRAWN, so a pick agrees with the screen. An overlay
  // that has never been drawn answers nothing, which is correct: nothing of it
  // is on the screen to be under the cursor.
  const double half = kMarkerScale * kBuiltinSymbolNominalPx * dpi_scale_ / 2.0;
  for (const DrawnPoint& d : drawn_) {
    const double dx = d.x - p.x;
    const double dy = d.y - p.y;
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (dist > tolerance_px + half) continue;

    app::HitItem item;
    item.overlay = this;
    item.feature = FeatureIdFor(d.label);
    item.distance_px = dist;
    item.hint.tool_tip = d.label;
    item.hint.status = Name() + ": waypoint " + d.label;
    item.cursor = app::CursorId::kMove;
    out.push_back(std::move(item));
  }
}

namespace {

// The box a route's waypoints fit in, longitudes UNWRAPPED against the first
// one so a route that crosses the antimeridian gets the narrow box it really
// occupies rather than one that reaches all the way round the other way. Five
// lines, and without them "go to this route" on a Pacific crossing frames the
// entire planet.
GeoRect WaypointBounds(const std::vector<RouteWaypoint>& wps) {
  GeoRect box{wps.front().position, wps.front().position};
  const double lon0 = wps.front().position.lon;
  double lo = lon0, hi = lon0;
  for (const RouteWaypoint& w : wps) {
    box.ll.lat = std::min(box.ll.lat, w.position.lat);
    box.ur.lat = std::max(box.ur.lat, w.position.lat);
    const double lon = lon0 + NormalizeLon(w.position.lon - lon0);
    lo = std::min(lo, lon);
    hi = std::max(hi, lon);
  }
  box.ll.lon = NormalizeLon(lo);
  box.ur.lon = NormalizeLon(hi);
  return box;
}

// The centre of that box, in the unwrapped frame, which is where a route's
// "representative point" belongs: a label for the whole route hangs in the
// middle of it, not at whichever end happens to be first in the file.
GeoPoint BoundsCenter(const GeoRect& box) {
  GeoPoint c;
  c.lat = (box.ll.lat + box.ur.lat) / 2.0;
  double span = box.ur.lon - box.ll.lon;
  if (box.CrossesAntimeridian()) span += 360.0;
  c.lon = NormalizeLon(box.ll.lon + span / 2.0);
  return c;
}

}  // namespace

void RouteOverlay::Search(const app::SearchQuery& q,
                          const std::atomic<bool>& cancel,
                          std::vector<app::SearchResult>& out) {
  // A route with no waypoints is not findable, by name or otherwise, and that
  // is deliberate: every field on a SearchResult that matters -- position,
  // bounds, the distance the session orders by -- is derived from the
  // waypoints, and a row whose position was invented would sort somewhere.
  const std::vector<RouteWaypoint>& wps = doc_.waypoints();
  if (wps.empty()) return;
  const size_t before = out.size();

  // The route's own name is the document's, falling back to the overlay's --
  // which is what a route that has never been saved has, and it is still the
  // name the user sees in the overlay list.
  const std::string route_name =
      !doc_.name().empty() ? doc_.name() : Name();
  const GeoRect box = WaypointBounds(wps);
  const GeoPoint center = BoundsCenter(box);

  int quality = 0;
  // THE AREA TEST FOR THE ROUTE IS THE BOX, NOT THE CENTRE: a route whose
  // centre is outside the search area but which runs straight through it is in
  // the area by any reading a user would recognise. A waypoint, having no
  // extent, is tested as the point it is.
  const bool in_area = !q.area || q.area->Intersects(box);
  if (in_area && app::SearchTextAccepts(q, route_name, &quality)) {
    app::SearchResult r;
    r.overlay = this;
    r.feature = 0;  // the route itself; minted waypoint ids start at 1
    r.match_quality = quality;
    r.position = center;
    r.bounds = box;
    r.title = route_name;
    r.detail = "route";
    if (wps.size() > 1) {
      r.detail += " \xc2\xb7 " + std::to_string(wps.size()) + " waypoints";
    }
    out.push_back(std::move(r));
  }

  for (const RouteWaypoint& w : wps) {
    if (cancel.load()) return;
    if (q.max_results > 0 && out.size() - before >= q.max_results) return;
    if (!app::SearchAreaAccepts(q, w.position)) continue;
    quality = 0;
    if (!app::SearchTextAccepts(q, w.label, &quality)) continue;

    app::SearchResult r;
    r.overlay = this;
    // The id HitTestPoint mints, so a search result and a pick name the same
    // waypoint with the same number and LabelForFeature answers for both.
    r.feature = FeatureIdFor(w.label);
    r.match_quality = quality;
    r.position = w.position;
    r.bounds = GeoRect{w.position, w.position};
    r.title = w.label;
    r.detail = "waypoint \xc2\xb7 " + route_name;
    out.push_back(std::move(r));
  }
}

void RouteOverlay::SnapToPoint(const MapProjection& proj, PixelPoint p,
                               double tolerance_px,
                               std::vector<app::SnapToItem>& out) {
  // Over HitTestPoint for PointOverlay's reason: one reach rule, so a snap and
  // a hit can never disagree about what the finger is over.
  std::vector<app::HitItem> hits;
  HitTestPoint(proj, p, tolerance_px, hits);
  if (hits.empty()) return;

  const std::vector<RouteWaypoint>& wps = waypoints();
  for (const app::HitItem& h : hits) {
    const std::string label = LabelForFeature(h.feature);
    // The DOCUMENT's coordinate, found by the label -- `drawn_` carries only
    // where the marker landed on screen, and un-projecting that would hand
    // back a pixel's worth of precision, which is the one thing a snap exists
    // to avoid.
    const RouteWaypoint* wp = nullptr;
    for (const RouteWaypoint& w : wps) {
      if (w.label == label) {
        wp = &w;
        break;
      }
    }
    if (wp == nullptr) continue;

    app::SnapToItem item;
    item.overlay = this;
    item.point = wp->position;
    item.distance_px = h.distance_px;
    // Qualified by the route's name, unlike a point's bare name: "Start" and
    // "End" are labels within ONE document and say nothing on their own in a
    // chooser listing several overlays' answers.
    item.description = Name() + ": " + label;
    out.push_back(std::move(item));
  }
}

// ---------------------------------------------------------------------------
// EditTarget, and the input SPI
// ---------------------------------------------------------------------------
//
// One-line forwards, every one of them. The stack reaches an OVERLAY -- the
// EditorManager brackets focus on one, the OverlayManager routes events to one
// -- and the editing state lives in a session, so this is the seam between the
// two and it is deliberately empty of decisions.

void RouteOverlay::EnterEditFocus() { edit_->EnterEditFocus(); }
void RouteOverlay::ReleaseEditFocus() { edit_->ReleaseEditFocus(); }
bool RouteOverlay::CanUndo() const { return edit_->CanUndo(); }
void RouteOverlay::Undo() { edit_->Undo(); }
bool RouteOverlay::CanRedo() const { return edit_->CanRedo(); }
void RouteOverlay::Redo() { edit_->Redo(); }

bool RouteOverlay::OnMouseDown(const MouseEvent& e) {
  return edit_->OnMouseDown(e);
}
bool RouteOverlay::OnMouseMove(const MouseEvent& e) {
  return edit_->OnMouseMove(e);
}
bool RouteOverlay::OnMouseUp(const MouseEvent& e) { return edit_->OnMouseUp(e); }
bool RouteOverlay::OnKeyDown(const KeyEvent& e) { return edit_->OnKeyDown(e); }

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

Status RegisterRouteOverlayType(app::OverlayTypeRegistry& registry,
                                const RoutePlanner* planner) {
  app::OverlayTypeDesc desc;
  desc.id = RouteOverlay::kTypeId;
  desc.display_name = "Route";
  desc.icon = "route";
  // 1000: above the map, the graticule and a point set. FalconView draws the
  // route being flown over everything except the crosshair, and A6's own
  // comment on `points` already reserved this number for it.
  desc.default_display_order = 1000;

  app::FileTypeDesc file;
  file.default_extension = RouteOverlay::kExtension;
  file.open_filters = {{"Route Files (*.fvrte)", "*.fvrte"}};
  file.save_filters = file.open_filters;
  desc.file = std::move(file);

  desc.factory = [planner] {
    auto ov = std::make_shared<RouteOverlay>();
    if (planner != nullptr) ov->SetPlanner(planner);
    return ov;
  };
  // No editor: v1 editing is `SetWaypoints`, wholesale, which is what a
  // sheet-driven UI produces. route.py keeps its own and is untouched.
  return registry.Register(std::move(desc));
}

}  // namespace fv
