// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/overlay/feature_edit.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "fvkit/app/pick.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/proj.h"

namespace fv {

namespace {

/// Movement past this many pixels turns a press into a drag.
constexpr double kDragThresholdPx = 3.0;
/// Half the side of a drawn vertex handle.
constexpr int kHandleHalfPx = 4;

struct Px {
  double x = 0, y = 0;
};

GeometryType MultiOf(GeometryType t) {
  switch (t) {
    case GeometryType::kPoint: return GeometryType::kMultiPoint;
    case GeometryType::kLineString: return GeometryType::kMultiLineString;
    case GeometryType::kPolygon: return GeometryType::kMultiPolygon;
    default: return t;
  }
}

/// Whether a layer of `layer_type` stores a geometry of `base` type.
bool Accepts(GeometryType layer_type, GeometryType base) {
  return layer_type == GeometryType::kGeometry || layer_type == base || layer_type == MultiOf(base);
}

/// `g` as the layer stores it: wrapped in its Multi- type when the layer has one.
bool Conform(GeometryType layer_type, const Geometry& g, Geometry* out) {
  if (layer_type == GeometryType::kGeometry || layer_type == g.type) {
    *out = g;
    return true;
  }
  if (layer_type == MultiOf(g.type)) {
    *out = Geometry::Collection(layer_type, {g});
    return true;
  }
  return false;
}

Geometry* PartOf(Geometry& g, int part) {
  if (part < 0) return &g;
  if (static_cast<size_t>(part) >= g.parts.size()) return nullptr;
  return &g.parts[static_cast<size_t>(part)];
}

const Geometry* PartOf(const Geometry& g, int part) { return PartOf(const_cast<Geometry&>(g), part); }

Path* PathOf(Geometry& g, const VertexRef& v) {
  Geometry* part = PartOf(g, v.part);
  if (part == nullptr || v.path < 0 || static_cast<size_t>(v.path) >= part->paths.size()) return nullptr;
  return &part->paths[static_cast<size_t>(v.path)];
}

bool IsRing(const Geometry& g, const VertexRef& v) {
  const Geometry* part = PartOf(g, v.part);
  return part != nullptr && part->type == GeometryType::kPolygon;
}

/// Calls `fn(part_geometry, part_index)` for a single geometry or each part.
template <typename Fn>
void ForEachPart(const Geometry& g, Fn&& fn) {
  if (g.parts.empty()) {
    fn(g, -1);
  } else {
    for (size_t k = 0; k < g.parts.size(); ++k) fn(g.parts[k], static_cast<int>(k));
  }
}

void Offset(Geometry& g, double dlat, double dlon) {
  for (Path& p : g.paths)
    for (Position& q : p) {
      q.lat += dlat;
      q.lon += dlon;
    }
  for (Geometry& part : g.parts) Offset(part, dlat, dlon);
}

bool SetVertex(Feature& f, const VertexRef& v, const GeoPoint& to) {
  Path* path = PathOf(f.geometry, v);
  if (path == nullptr || v.index < 0 || static_cast<size_t>(v.index) >= path->size()) return false;
  Position& q = (*path)[static_cast<size_t>(v.index)];
  q.lat = to.lat;
  q.lon = to.lon;
  if (IsRing(f.geometry, v) && v.index == 0 && path->size() > 1) path->back() = q;
  return true;
}

bool InsertAt(Feature& f, const VertexRef& v, const GeoPoint& at) {
  Path* path = PathOf(f.geometry, v);
  if (path == nullptr || path->empty()) return false;
  const bool ring = IsRing(f.geometry, v);
  const int lo = ring ? 1 : 0;
  const int hi = static_cast<int>(path->size()) - (ring ? 1 : 0);
  if (v.index < lo || v.index > hi) return false;
  Position q = (*path)[static_cast<size_t>(std::min(v.index, static_cast<int>(path->size()) - 1))];
  q.lat = at.lat;
  q.lon = at.lon;
  path->insert(path->begin() + v.index, q);
  return true;
}

bool EraseAt(Feature& f, const VertexRef& v) {
  Path* path = PathOf(f.geometry, v);
  const Geometry* part = PartOf(f.geometry, v.part);
  if (path == nullptr || part == nullptr || part->type == GeometryType::kPoint) return false;
  const bool ring = part->type == GeometryType::kPolygon;
  const size_t vertices = ring ? path->size() - 1 : path->size();
  if (v.index < 0 || static_cast<size_t>(v.index) >= vertices) return false;
  if (vertices <= (ring ? 3u : 2u)) return false;
  path->erase(path->begin() + v.index);
  if (ring && v.index == 0) path->back() = path->front();
  return true;
}

/// Even-odd point in ring, in whatever plane both are given in.
template <typename P>
bool InRing(const std::vector<P>& ring, double x, double y) {
  bool in = false;
  for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
    const double xi = ring[i].x, yi = ring[i].y, xj = ring[j].x, yj = ring[j].y;
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

/// Distance from (x, y) to segment a–b; `t` receives the parameter of the nearest point.
double SegmentDistance(Px a, Px b, double x, double y, double* t) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  double u = len2 > 0 ? ((x - a.x) * dx + (y - a.y) * dy) / len2 : 0;
  u = std::max(0.0, std::min(1.0, u));
  if (t != nullptr) *t = u;
  return std::hypot(a.x + u * dx - x, a.y + u * dy - y);
}

bool InRect(const PixelRect& r, Px p) {
  return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
}

/// Liang–Barsky: whether segment a–b meets rectangle `r`.
bool SegmentMeetsRect(Px a, Px b, const PixelRect& r) {
  double t0 = 0, t1 = 1;
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {a.x - r.x, r.x + r.width - a.x, a.y - r.y, r.y + r.height - a.y};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
      continue;
    }
    const double t = q[i] / p[i];
    if (p[i] < 0) {
      t0 = std::max(t0, t);
    } else {
      t1 = std::min(t1, t);
    }
    if (t0 > t1) return false;
  }
  return true;
}

std::vector<Px> Project(const MapProjection& proj, const Path& path) {
  std::vector<Px> out;
  out.reserve(path.size());
  for (const Position& q : path) {
    Px p;
    if (proj.GeoToSurface(q.point(), &p.x, &p.y).ok()) out.push_back(p);
  }
  return out;
}

/// The geographic box under a surface rectangle, widened by `pad` pixels.
bool GeoBox(const MapProjection& proj, double x0, double y0, double x1, double y1, GeoRect* out) {
  GeoPoint corners[4];
  const double xs[4] = {x0, x1, x0, x1};
  const double ys[4] = {y0, y0, y1, y1};
  for (int i = 0; i < 4; ++i)
    if (!proj.SurfaceToGeo(xs[i], ys[i], &corners[i]).ok()) return false;
  GeoRect box{corners[0], corners[0]};
  for (const GeoPoint& c : corners) {
    box.ll.lat = std::min(box.ll.lat, c.lat);
    box.ll.lon = std::min(box.ll.lon, c.lon);
    box.ur.lat = std::max(box.ur.lat, c.lat);
    box.ur.lon = std::max(box.ur.lon, c.lon);
  }
  *out = box;
  return true;
}

}  // namespace

FeatureEditSession::FeatureEditSession(FeatureOverlay& overlay) : overlay_(overlay) {}

FeatureEditSession::~FeatureEditSession() = default;

// MARK: Tool state

void FeatureEditSession::SetTool(FeatureTool tool) {
  if (tool != tool_) sketch_.clear();
  tool_ = tool;
}

bool FeatureEditSession::ToolAvailable(FeatureTool tool) const {
  const auto& layer = overlay_.layer();
  if (!layer) return false;
  const GeometryType t = layer->Schema().geometry_type;
  switch (tool) {
    case FeatureTool::kSelect: return true;
    case FeatureTool::kAddPoint: return Accepts(t, GeometryType::kPoint);
    case FeatureTool::kDrawLine: return Accepts(t, GeometryType::kLineString);
    case FeatureTool::kDrawArea: return Accepts(t, GeometryType::kPolygon);
    case FeatureTool::kAddHole: {
      if (overlay_.selection().size() != 1) return false;
      Feature f;
      if (!layer->Get(overlay_.selected(), &f).ok()) return false;
      return f.geometry.type == GeometryType::kPolygon || f.geometry.type == GeometryType::kMultiPolygon;
    }
  }
  return false;
}

void FeatureEditSession::ReleaseEditFocus() {
  CancelDrag();
  sketch_.clear();
  tool_ = FeatureTool::kSelect;
  active_vertex_ = VertexRef{};
  has_edit_focus_ = false;
}

// MARK: History

void FeatureEditSession::SetUndoStack(app::UndoStack* stack) {
  if (stack == stack_) return;
  undo_stack().RemoveOwner(&overlay_);
  stack_ = stack;
}

bool FeatureEditSession::CanUndo() const {
  return stack_ != nullptr ? stack_->CanUndo() : own_stack_.CanUndo();
}

bool FeatureEditSession::CanRedo() const {
  return stack_ != nullptr ? stack_->CanRedo() : own_stack_.CanRedo();
}

Status FeatureEditSession::Undo() {
  CancelDrag();
  return undo_stack().Undo();
}

Status FeatureEditSession::Redo() {
  CancelDrag();
  return undo_stack().Redo();
}

void FeatureEditSession::ClearHistory() { undo_stack().RemoveOwner(&overlay_); }

void FeatureEditSession::Commit(const std::string& label, std::vector<Feature> before,
                                std::vector<int64_t> ids) {
  std::vector<Feature> after;
  if (!Snapshot(ids, &after)) return;
  CommitChange(label, std::move(before), std::move(after));
}

void FeatureEditSession::CommitChange(const std::string& label, std::vector<Feature> before,
                                      std::vector<Feature> after) {
  Touched();
  std::weak_ptr<IEditableFeatureLayer> weak_layer = overlay_.layer();
  std::weak_ptr<int> alive = alive_;
  FeatureOverlay* overlay = &overlay_;
  // Puts the layer in state `to` from state `from`: features only in `from`
  // are deleted, those in both replaced, those only in `to` restored by id.
  auto apply = [weak_layer, alive, overlay](const std::vector<Feature>& from,
                                            const std::vector<Feature>& to) -> Status {
    const auto layer = weak_layer.lock();
    if (alive.expired() || !layer || layer != overlay->layer())
      return Status::Error(kNotFound, "the edited layer is closed");
    std::set<int64_t> kept;
    for (const Feature& f : to) kept.insert(f.id);
    for (const Feature& f : from) {
      if (kept.count(f.id) != 0) continue;
      Status s = layer->Delete(f.id);
      if (!s.ok()) return s;
    }
    std::set<int64_t> had;
    for (const Feature& f : from) had.insert(f.id);
    std::vector<int64_t> selection;
    for (const Feature& f : to) {
      Status s = had.count(f.id) != 0 ? layer->Update(f) : layer->InsertWithId(f);
      if (!s.ok()) return s;
      selection.push_back(f.id);
    }
    FeatureEditSession& edit = overlay->edit();
    edit.CancelDrag();
    edit.active_vertex_ = VertexRef{};
    overlay->SetSelection(std::move(selection));
    overlay->set_dirty(true);
    return Status::Ok();
  };
  app::UndoEntry e;
  e.label = label;
  e.owner = &overlay_;
  e.undo = [apply, before, after] { return apply(after, before); };
  e.redo = [apply, before, after] { return apply(before, after); };
  undo_stack().Push(std::move(e));
}

bool FeatureEditSession::Snapshot(const std::vector<int64_t>& ids, std::vector<Feature>* out) const {
  const auto& layer = overlay_.layer();
  if (!layer) return false;
  out->clear();
  for (int64_t id : ids) {
    Feature f;
    if (!layer->Get(id, &f).ok()) return false;
    out->push_back(std::move(f));
  }
  return true;
}

bool FeatureEditSession::Replace(const Feature& f) {
  const auto& layer = overlay_.layer();
  return layer && layer->Update(f).ok();
}

void FeatureEditSession::Touched() { overlay_.set_dirty(true); }

bool FeatureEditSession::HasProjection() const { return overlay_.has_projection(); }

const MapProjection& FeatureEditSession::Projection() const { return overlay_.last_projection(); }

// MARK: Selection

void FeatureEditSession::Select(int64_t id, bool add) {
  if (!add) {
    overlay_.SetSelected(id);
  } else {
    std::vector<int64_t> ids = overlay_.selection();
    const auto it = std::find(ids.begin(), ids.end(), id);
    if (it != ids.end()) {
      ids.erase(it);
    } else {
      ids.push_back(id);
    }
    overlay_.SetSelection(std::move(ids));
  }
  if (active_vertex_.valid() && !overlay_.IsSelected(active_vertex_.feature)) active_vertex_ = VertexRef{};
}

void FeatureEditSession::ClearSelection() {
  overlay_.SetSelected(0);
  active_vertex_ = VertexRef{};
}

std::vector<int64_t> FeatureEditSession::FeaturesInRect(const PixelRect& r) const {
  std::vector<int64_t> ids;
  const auto& layer = overlay_.layer();
  if (!layer || !HasProjection()) return ids;
  const MapProjection& proj = Projection();
  GeoRect box;
  if (!GeoBox(proj, r.x, r.y, r.x + r.width, r.y + r.height, &box)) return ids;
  std::vector<Feature> features;
  if (!layer->Query(box, &features).ok()) return ids;
  const Px centre{r.x + r.width / 2.0, r.y + r.height / 2.0};
  for (const Feature& f : features) {
    bool meets = false;
    ForEachPart(f.geometry, [&](const Geometry& g, int) {
      if (meets) return;
      for (const Path& path : g.paths) {
        const std::vector<Px> px = Project(proj, path);
        for (size_t i = 0; i < px.size() && !meets; ++i) {
          if (InRect(r, px[i])) meets = true;
          if (i > 0 && SegmentMeetsRect(px[i - 1], px[i], r)) meets = true;
        }
      }
      if (!meets && g.type == GeometryType::kPolygon) {
        bool in = false;
        for (const Path& path : g.paths) {
          const std::vector<Px> px = Project(proj, path);
          if (px.size() >= 3 && InRing(px, centre.x, centre.y)) in = !in;
        }
        meets = in;
      }
    });
    if (meets) ids.push_back(f.id);
  }
  return ids;
}

// MARK: Edits

int64_t FeatureEditSession::AddFeature(const Geometry& geometry, const std::string& label) {
  const auto& layer = overlay_.layer();
  if (!layer) return 0;
  Feature f;
  if (!Conform(layer->Schema().geometry_type, geometry, &f.geometry)) return 0;
  if (!layer->Insert(&f).ok()) return 0;
  Feature stored;
  if (!layer->Get(f.id, &stored).ok()) return 0;
  CommitChange(label, {}, {stored});
  overlay_.SetSelected(f.id);
  active_vertex_ = VertexRef{};
  return f.id;
}

bool FeatureEditSession::DeleteFeatures(const std::vector<int64_t>& ids) {
  const auto& layer = overlay_.layer();
  std::vector<Feature> before;
  if (ids.empty() || !Snapshot(ids, &before)) return false;
  for (int64_t id : ids)
    if (!layer->Delete(id).ok()) return false;
  CommitChange(ids.size() == 1 ? "Delete Feature" : "Delete Features", std::move(before), {});
  ClearSelection();
  return true;
}

bool FeatureEditSession::DeleteSelected() { return DeleteFeatures(overlay_.selection()); }

bool FeatureEditSession::MoveFeatures(const std::vector<int64_t>& ids, double dlat, double dlon) {
  std::vector<Feature> before;
  if (ids.empty() || !Snapshot(ids, &before)) return false;
  for (Feature f : before) {
    Offset(f.geometry, dlat, dlon);
    if (!Replace(f)) return false;
  }
  Commit(ids.size() == 1 ? "Move Feature" : "Move Features", std::move(before), ids);
  return true;
}

bool FeatureEditSession::MoveVertex(const VertexRef& v, const GeoPoint& to) {
  std::vector<Feature> before;
  if (!Snapshot({v.feature}, &before)) return false;
  Feature f = before.front();
  if (!SetVertex(f, v, to) || !Replace(f)) return false;
  Commit("Move Vertex", std::move(before), {v.feature});
  return true;
}

bool FeatureEditSession::InsertVertex(const VertexRef& v, const GeoPoint& at) {
  std::vector<Feature> before;
  if (!Snapshot({v.feature}, &before)) return false;
  Feature f = before.front();
  if (!InsertAt(f, v, at) || !Replace(f)) return false;
  Commit("Insert Vertex", std::move(before), {v.feature});
  return true;
}

bool FeatureEditSession::DeleteVertex(const VertexRef& v) {
  std::vector<Feature> before;
  if (!Snapshot({v.feature}, &before)) return false;
  Feature f = before.front();
  if (!EraseAt(f, v) || !Replace(f)) return false;
  Commit("Delete Vertex", std::move(before), {v.feature});
  if (active_vertex_ == v) active_vertex_ = VertexRef{};
  return true;
}

bool FeatureEditSession::AddHole(int64_t id, const std::vector<GeoPoint>& ring) {
  if (ring.size() < 3) return false;
  std::vector<Feature> before;
  if (!Snapshot({id}, &before)) return false;
  Feature f = before.front();
  struct LatLon {
    double x, y;
  };
  Geometry* target = nullptr;
  auto holds = [&ring](const Geometry& g) {
    if (g.type != GeometryType::kPolygon || g.paths.empty() || g.paths[0].size() < 4) return false;
    std::vector<LatLon> exterior;
    for (const Position& q : g.paths[0]) exterior.push_back({q.lon, q.lat});
    return std::all_of(ring.begin(), ring.end(),
                       [&exterior](const GeoPoint& p) { return InRing(exterior, p.lon, p.lat); });
  };
  if (f.geometry.parts.empty()) {
    if (holds(f.geometry)) target = &f.geometry;
  } else {
    for (Geometry& part : f.geometry.parts)
      if (target == nullptr && holds(part)) target = &part;
  }
  if (target == nullptr) return false;
  Path hole;
  for (const GeoPoint& p : ring) {
    Position q = Position::At(p);
    hole.push_back(q);
  }
  hole.push_back(hole.front());
  target->paths.push_back(std::move(hole));
  if (!Replace(f)) return false;
  Commit("Add Hole", std::move(before), {id});
  return true;
}

bool FeatureEditSession::RemoveHole(const VertexRef& v) {
  if (v.path <= 0) return false;
  std::vector<Feature> before;
  if (!Snapshot({v.feature}, &before)) return false;
  Feature f = before.front();
  Geometry* part = PartOf(f.geometry, v.part);
  if (part == nullptr || part->type != GeometryType::kPolygon ||
      static_cast<size_t>(v.path) >= part->paths.size())
    return false;
  part->paths.erase(part->paths.begin() + v.path);
  if (!Replace(f)) return false;
  Commit("Delete Hole", std::move(before), {v.feature});
  if (active_vertex_.feature == v.feature) active_vertex_ = VertexRef{};
  return true;
}

bool FeatureEditSession::UpdateFeature(const Feature& after, const std::string& label) {
  std::vector<Feature> before;
  if (!Snapshot({after.id}, &before) || !Replace(after)) return false;
  Commit(label, std::move(before), {after.id});
  return true;
}

bool FeatureEditSession::RecordEdit(const Feature& before, const std::string& label) {
  std::vector<Feature> after;
  if (!Snapshot({before.id}, &after)) return false;
  CommitChange(label, {before}, std::move(after));
  return true;
}

// MARK: Sketch

bool FeatureEditSession::RemoveSketchPoint() {
  if (sketch_.empty()) return false;
  sketch_.pop_back();
  return true;
}

bool FeatureEditSession::FinishSketch() {
  bool done = false;
  switch (tool_) {
    case FeatureTool::kDrawLine:
      if (sketch_.size() >= 2) done = AddFeature(Geometry::LineString(sketch_), "Draw Line") != 0;
      break;
    case FeatureTool::kDrawArea:
      if (sketch_.size() >= 3) done = AddFeature(Geometry::Polygon({sketch_}), "Draw Area") != 0;
      break;
    case FeatureTool::kAddHole:
      if (sketch_.size() >= 3 && overlay_.selection().size() == 1) {
        done = AddHole(overlay_.selected(), sketch_);
        if (done) tool_ = FeatureTool::kSelect;
      }
      break;
    default:
      break;
  }
  if (done) sketch_.clear();
  return done;
}

// MARK: Hit testing

VertexRef FeatureEditSession::VertexAt(PixelPoint p) const {
  VertexRef best;
  const auto& layer = overlay_.layer();
  if (!layer || !HasProjection()) return best;
  const MapProjection& proj = Projection();
  double best_d = pick_tolerance_px_ + kHandleHalfPx;
  const std::vector<int64_t>& selection = overlay_.selection();
  for (auto id = selection.rbegin(); id != selection.rend(); ++id) {
    Feature f;
    if (!layer->Get(*id, &f).ok()) continue;
    ForEachPart(f.geometry, [&](const Geometry& g, int part) {
      for (size_t r = 0; r < g.paths.size(); ++r) {
        const Path& path = g.paths[r];
        const size_t n = g.type == GeometryType::kPolygon && path.size() > 1 ? path.size() - 1 : path.size();
        for (size_t i = 0; i < n; ++i) {
          double x = 0, y = 0;
          if (!proj.GeoToSurface(path[i].point(), &x, &y).ok()) continue;
          const double d = std::hypot(x - p.x, y - p.y);
          if (d < best_d) {
            best_d = d;
            best = VertexRef{f.id, part, static_cast<int>(r), static_cast<int>(i)};
          }
        }
      }
    });
  }
  return best;
}

VertexRef FeatureEditSession::SegmentAt(PixelPoint p, GeoPoint* on) const {
  VertexRef best;
  const auto& layer = overlay_.layer();
  if (!layer || !HasProjection()) return best;
  const MapProjection& proj = Projection();
  double best_d = pick_tolerance_px_;
  Px best_px;
  const std::vector<int64_t>& selection = overlay_.selection();
  for (auto id = selection.rbegin(); id != selection.rend(); ++id) {
    Feature f;
    if (!layer->Get(*id, &f).ok()) continue;
    ForEachPart(f.geometry, [&](const Geometry& g, int part) {
      if (g.type == GeometryType::kPoint) return;
      for (size_t r = 0; r < g.paths.size(); ++r) {
        const std::vector<Px> px = Project(proj, g.paths[r]);
        if (px.size() != g.paths[r].size()) continue;
        for (size_t i = 1; i < px.size(); ++i) {
          double t = 0;
          const double d = SegmentDistance(px[i - 1], px[i], p.x, p.y, &t);
          if (d < best_d) {
            best_d = d;
            best = VertexRef{f.id, part, static_cast<int>(r), static_cast<int>(i)};
            best_px = Px{px[i - 1].x + t * (px[i].x - px[i - 1].x), px[i - 1].y + t * (px[i].y - px[i - 1].y)};
          }
        }
      }
    });
  }
  if (best.valid() && on != nullptr && !proj.SurfaceToGeo(best_px.x, best_px.y, on).ok()) return VertexRef{};
  return best;
}

int64_t FeatureEditSession::FeatureAt(PixelPoint p) const {
  const auto& layer = overlay_.layer();
  if (!layer || !HasProjection()) return 0;
  const MapProjection& proj = Projection();
  const double scale = overlay_.display_scale();
  const FeatureSymbol& symbol = overlay_.style().symbol;
  const double reach = pick_tolerance_px_ + std::max(symbol.marker_size_px / 2, symbol.line_width_px / 2) * scale;
  GeoRect box;
  if (!GeoBox(proj, p.x - reach, p.y - reach, p.x + reach, p.y + reach, &box)) return 0;
  std::vector<Feature> features;
  if (!layer->Query(box, &features).ok()) return 0;
  // Later features draw on top, so they are hit first.
  for (auto f = features.rbegin(); f != features.rend(); ++f) {
    bool hit = false;
    ForEachPart(f->geometry, [&](const Geometry& g, int) {
      if (hit) return;
      if (g.type == GeometryType::kPolygon) {
        bool in = false;
        for (const Path& path : g.paths) {
          const std::vector<Px> px = Project(proj, path);
          if (px.size() >= 3 && InRing(px, p.x, p.y)) in = !in;
          for (size_t i = 1; i < px.size(); ++i)
            if (SegmentDistance(px[i - 1], px[i], p.x, p.y, nullptr) <= pick_tolerance_px_) hit = true;
        }
        hit = hit || in;
        return;
      }
      for (const Path& path : g.paths) {
        const std::vector<Px> px = Project(proj, path);
        if (px.size() == 1 && std::hypot(px[0].x - p.x, px[0].y - p.y) <= reach) hit = true;
        for (size_t i = 1; i < px.size(); ++i)
          if (SegmentDistance(px[i - 1], px[i], p.x, p.y, nullptr) <= reach) hit = true;
      }
    });
    if (hit) return f->id;
  }
  return 0;
}

EditPosition FeatureEditSession::ResolvePixel(PixelPoint p) const {
  EditPosition out;
  if (!HasProjection()) return out;
  const MapProjection& proj = Projection();
  OverlayManager* manager = overlay_.manager();
  if (manager != nullptr && snap_tolerance_px_ > 0.0) {
    const std::vector<app::SnapToItem> candidates = app::SnapCandidates(*manager, proj, p, snap_tolerance_px_);
    if (!candidates.empty()) {
      out.position = candidates.front().point;
      out.valid = true;
      out.snapped = true;
      out.snapped_to = candidates.front().description;
      return out;
    }
  }
  GeoPoint g;
  if (!proj.SurfaceToGeo(p.x, p.y, &g).ok()) return out;
  out.position = g;
  out.valid = true;
  return out;
}

// MARK: Dragging

bool FeatureEditSession::BeginVertexDrag(const VertexRef& v, PixelPoint at) {
  std::vector<Feature> before;
  if (!Snapshot({v.feature}, &before)) return false;
  drag_ = Drag{};
  drag_.kind = DragKind::kVertex;
  drag_.press = drag_.now = at;
  drag_.vertex = v;
  drag_.base = before.front();
  drag_.before = std::move(before);
  drag_.label = "Move Vertex";
  drag_.was_dirty = overlay_.is_dirty();
  if (OverlayManager* m = overlay_.manager()) m->CaptureMouse(&overlay_);
  return true;
}

bool FeatureEditSession::BeginMoveDrag(PixelPoint at) {
  std::vector<Feature> before;
  if (!HasProjection() || !Snapshot(overlay_.selection(), &before) || before.empty()) return false;
  GeoPoint g;
  if (!Projection().SurfaceToGeo(at.x, at.y, &g).ok()) return false;
  drag_ = Drag{};
  drag_.kind = DragKind::kMove;
  drag_.press = drag_.now = at;
  drag_.press_geo = g;
  drag_.label = before.size() == 1 ? "Move Feature" : "Move Features";
  drag_.before = std::move(before);
  drag_.was_dirty = overlay_.is_dirty();
  if (OverlayManager* m = overlay_.manager()) m->CaptureMouse(&overlay_);
  return true;
}

bool FeatureEditSession::BeginBoxDrag(PixelPoint at) {
  drag_ = Drag{};
  drag_.kind = DragKind::kBox;
  drag_.press = drag_.now = at;
  if (OverlayManager* m = overlay_.manager()) m->CaptureMouse(&overlay_);
  return true;
}

bool FeatureEditSession::IsDragged(int64_t feature, int part, int path, int index) const {
  if (!drag_.moved && !drag_.edited) return false;
  if (drag_.kind == DragKind::kVertex)
    return drag_.vertex == VertexRef{feature, part, path, index};
  if (drag_.kind == DragKind::kMove)
    return std::any_of(drag_.before.begin(), drag_.before.end(),
                       [feature](const Feature& f) { return f.id == feature; });
  return false;
}

bool FeatureEditSession::DragTo(PixelPoint at) {
  if (drag_.kind == DragKind::kNone) return false;
  if (!drag_.moved) {
    if (std::hypot(at.x - drag_.press.x, at.y - drag_.press.y) < kDragThresholdPx) return true;
    drag_.moved = true;
  }
  drag_.now = at;
  switch (drag_.kind) {
    case DragKind::kVertex: {
      const EditPosition p = ResolvePixel(at);
      if (!p.valid) return true;
      Feature f = drag_.base;
      if (SetVertex(f, drag_.vertex, p.position)) Replace(f);
      break;
    }
    case DragKind::kMove: {
      GeoPoint g;
      if (!Projection().SurfaceToGeo(at.x, at.y, &g).ok()) return true;
      for (Feature f : drag_.before) {
        Offset(f.geometry, g.lat - drag_.press_geo.lat, g.lon - drag_.press_geo.lon);
        Replace(f);
      }
      break;
    }
    default:
      break;
  }
  return true;
}

bool FeatureEditSession::EndDrag(PixelPoint at) {
  if (drag_.kind == DragKind::kNone) return false;
  if (drag_.moved) DragTo(at);
  Drag d = std::move(drag_);
  drag_ = Drag{};
  if (OverlayManager* m = overlay_.manager()) m->ReleaseMouse();
  if (d.kind == DragKind::kBox) {
    if (d.moved) {
      const PixelRect r{std::min(d.press.x, d.now.x), std::min(d.press.y, d.now.y),
                        std::abs(d.now.x - d.press.x), std::abs(d.now.y - d.press.y)};
      std::vector<int64_t> ids = overlay_.selection();
      for (int64_t id : FeaturesInRect(r)) ids.push_back(id);
      overlay_.SetSelection(std::move(ids));
    }
    return true;
  }
  if (d.moved || d.edited) {
    std::vector<int64_t> ids;
    for (const Feature& f : d.before) ids.push_back(f.id);
    Commit(d.label, std::move(d.before), std::move(ids));
  }
  return true;
}

bool FeatureEditSession::CancelDrag() {
  if (drag_.kind == DragKind::kNone) return false;
  Drag d = std::move(drag_);
  drag_ = Drag{};
  if (OverlayManager* m = overlay_.manager()) m->ReleaseMouse();
  if (d.moved || d.edited) {
    for (const Feature& f : d.before) Replace(f);
    if (!d.was_dirty) overlay_.set_dirty(false);
  }
  return true;
}

// MARK: Input

bool FeatureEditSession::OnMouseDown(const MouseEvent& e) {
  if (!has_edit_focus_ || !overlay_.layer()) return false;
  const PixelPoint at{e.x, e.y};
  const bool second_click = e.clicks >= 2;
  switch (tool_) {
    case FeatureTool::kAddPoint: {
      // The first press of the pair placed the point; the second opens it.
      if (second_click) {
        const int64_t id = FeatureAt(at);
        if (id != 0) OpenFeature(id);
        return true;
      }
      const EditPosition p = ResolvePixel(at);
      if (p.valid) AddFeature(Geometry::Point(p.position), "Add Point");
      return true;
    }
    case FeatureTool::kDrawLine:
    case FeatureTool::kDrawArea:
    case FeatureTool::kAddHole: {
      // The first press of a double-click placed the last vertex; the second
      // finishes the shape. An empty sketch means the first press closed it.
      if (second_click) {
        if (!sketch_.empty()) FinishSketch();
        return true;
      }
      if (OnSketchStart(at)) {
        FinishSketch();
        return true;
      }
      const EditPosition p = ResolvePixel(at);
      if (p.valid) AddSketchPoint(p.position);
      return true;
    }
    case FeatureTool::kSelect:
      break;
  }

  if (second_click && !e.shift) {
    const int64_t id = FeatureAt(at);
    if (id != 0) {
      OpenFeature(id);
      return true;
    }
  }

  // Shift is a selection modifier: it never starts a vertex edit.
  const VertexRef v = e.shift ? VertexRef{} : VertexAt(at);
  if (v.valid()) {
    active_vertex_ = v;
    BeginVertexDrag(v, at);
    return true;
  }
  GeoPoint on;
  const VertexRef seg = e.shift ? VertexRef{} : SegmentAt(at, &on);
  if (seg.valid()) {
    // A press on a segment inserts a vertex there and drags it; one undo entry covers both.
    std::vector<Feature> before;
    if (!Snapshot({seg.feature}, &before)) return true;
    Feature f = before.front();
    if (!InsertAt(f, seg, on) || !Replace(f)) return true;
    BeginVertexDrag(seg, at);
    drag_.before = std::move(before);
    drag_.label = "Insert Vertex";
    drag_.edited = true;
    active_vertex_ = seg;
    return true;
  }
  const int64_t id = FeatureAt(at);
  if (id != 0) {
    active_vertex_ = VertexRef{};
    if (e.shift) {
      Select(id, true);
      return true;
    }
    if (!overlay_.IsSelected(id)) Select(id);
    BeginMoveDrag(at);
    return true;
  }
  if (e.shift) return BeginBoxDrag(at);
  // A plain press on empty map pans; Escape clears the selection.
  return false;
}

bool FeatureEditSession::OnSketchStart(PixelPoint p) const {
  // A click on the first vertex closes an area or hole; a line has no close.
  if (tool_ == FeatureTool::kDrawLine || sketch_.size() < 3 || !HasProjection()) return false;
  double x = 0, y = 0;
  return Projection().GeoToSurface(sketch_.front(), &x, &y).ok() &&
         std::hypot(x - p.x, y - p.y) <= pick_tolerance_px_ + kHandleHalfPx;
}

namespace {

/// "point", "line" or "area" for a feature's geometry.
const char* KindWord(GeometryType type) {
  switch (type) {
    case GeometryType::kPoint:
    case GeometryType::kMultiPoint:
      return "point";
    case GeometryType::kLineString:
    case GeometryType::kMultiLineString:
      return "line";
    case GeometryType::kPolygon:
    case GeometryType::kMultiPolygon:
      return "area";
    default:
      return "feature";
  }
}

}  // namespace

std::string FeatureEditSession::HintAt(PixelPoint p) const {
  if (!has_edit_focus_ || !overlay_.layer()) return std::string();
  const char* finish = " (double-click or Return finishes)";
  switch (tool_) {
    case FeatureTool::kAddPoint:
      return "Add point";
    case FeatureTool::kDrawLine:
      if (sketch_.empty()) return "Start line";
      return std::string("Add vertex") + finish;
    case FeatureTool::kDrawArea:
    case FeatureTool::kAddHole: {
      const bool hole = tool_ == FeatureTool::kAddHole;
      if (sketch_.empty()) return hole ? "Start hole" : "Start area";
      if (OnSketchStart(p)) return hole ? "Close hole" : "Close area";
      return sketch_.size() >= 2 ? std::string("Add vertex") + finish : "Add vertex";
    }
    case FeatureTool::kSelect:
      break;
  }
  if (VertexAt(p).valid()) return "Move vertex";
  GeoPoint on;
  if (SegmentAt(p, &on).valid()) return "Insert vertex";
  const int64_t id = FeatureAt(p);
  if (id == 0) return std::string();
  Feature f;
  if (!overlay_.layer()->Get(id, &f).ok()) return std::string();
  const std::string what = std::string(KindWord(f.geometry.type)) + " \u201C" + overlay_.FeatureTitle(f) + "\u201D";
  if (overlay_.IsSelected(id)) return "Move " + what + " (double-click to edit)";
  return "Select " + what;
}

void FeatureEditSession::OpenFeature(int64_t id) {
  CancelDrag();
  active_vertex_ = VertexRef{};
  if (overlay_.selection() != std::vector<int64_t>{id}) Select(id);
  open_request_ = true;
}

bool FeatureEditSession::TakeOpenRequest() {
  const bool r = open_request_;
  open_request_ = false;
  return r;
}

bool FeatureEditSession::OnMouseMove(const MouseEvent& e) { return DragTo(PixelPoint{e.x, e.y}); }

bool FeatureEditSession::OnMouseUp(const MouseEvent& e) {
  if (EndDrag(PixelPoint{e.x, e.y})) return true;
  return has_edit_focus_ && tool_ != FeatureTool::kSelect;
}

bool FeatureEditSession::OnKeyDown(const KeyEvent& e) {
  if (!has_edit_focus_) return false;
  if (e.key == Key::kEscape) {
    if (CancelDrag()) return true;
    if (!sketch_.empty()) {
      sketch_.clear();
      return true;
    }
    if (tool_ != FeatureTool::kSelect) {
      tool_ = FeatureTool::kSelect;
      return true;
    }
    if (active_vertex_.valid()) {
      active_vertex_ = VertexRef{};
      return true;
    }
    if (!overlay_.selection().empty()) {
      ClearSelection();
      return true;
    }
    return false;
  }
  if (e.ctrl || e.alt || e.meta) return false;
  if (e.key == Key::kReturn) {
    if (sketch_.empty()) return false;
    FinishSketch();
    return true;
  }
  if (e.key == Key::kBackspace && !sketch_.empty()) {
    RemoveSketchPoint();
    return true;
  }
  if (e.key == Key::kDelete || e.key == Key::kBackspace) {
    if (active_vertex_.valid()) {
      DeleteVertex(active_vertex_);
      return true;
    }
    if (!overlay_.selection().empty()) {
      DeleteSelected();
      return true;
    }
  }
  return false;
}

// MARK: Chrome

Status FeatureEditSession::DrawChrome(const MapProjection& proj, ICanvas& canvas) const {
  const FvColor ink{32, 32, 32, 255};
  const FvColor paper{255, 255, 255, 255};
  const FvColor active{230, 40, 40, 255};
  const FvColor sketch_ink{255, 127, 0, 255};
  const Pen outline{ink, 1};
  auto handle = [&](const GeoPoint& g, const FvColor& fill) -> Status {
    double x = 0, y = 0;
    if (!proj.GeoToSurface(g, &x, &y).ok()) return Status::Ok();
    const Brush b{fill};
    const PixelRect r{static_cast<int>(std::lround(x)) - kHandleHalfPx,
                      static_cast<int>(std::lround(y)) - kHandleHalfPx, 2 * kHandleHalfPx + 1,
                      2 * kHandleHalfPx + 1};
    return canvas.DrawRectangle(r, &b, &outline);
  };

  if (const auto& layer = overlay_.layer()) {
    for (int64_t id : overlay_.selection()) {
      Feature f;
      if (!layer->Get(id, &f).ok()) continue;
      Status s = Status::Ok();
      ForEachPart(f.geometry, [&](const Geometry& g, int part) {
        if (g.type == GeometryType::kPoint) return;
        for (size_t r = 0; r < g.paths.size(); ++r) {
          const Path& path = g.paths[r];
          const size_t n = g.type == GeometryType::kPolygon && path.size() > 1 ? path.size() - 1 : path.size();
          for (size_t i = 0; i < n && s.ok(); ++i) {
            const bool is_active = active_vertex_ == VertexRef{id, part, static_cast<int>(r), static_cast<int>(i)};
            s = handle(path[i].point(), is_active ? active : paper);
          }
        }
      });
      if (!s.ok()) return s;
    }
  }

  if (!sketch_.empty()) {
    std::vector<PixelPoint> pts;
    for (const GeoPoint& g : sketch_) {
      double x = 0, y = 0;
      if (proj.GeoToSurface(g, &x, &y).ok())
        pts.push_back(PixelPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))});
    }
    if (pts.size() >= 2) {
      Status s = canvas.DrawLines(pts, Pen{sketch_ink, 2});
      if (!s.ok()) return s;
    }
    for (const GeoPoint& g : sketch_) {
      Status s = handle(g, sketch_ink);
      if (!s.ok()) return s;
    }
  }

  if (drag_.kind == DragKind::kBox && drag_.moved) {
    const PixelRect r{std::min(drag_.press.x, drag_.now.x), std::min(drag_.press.y, drag_.now.y),
                      std::abs(drag_.now.x - drag_.press.x), std::abs(drag_.now.y - drag_.press.y)};
    const Brush fill{FvColor{60, 120, 220, 48}};
    const Pen edge{FvColor{60, 120, 220, 255}, 1};
    Status s = canvas.DrawRectangle(r, &fill, &edge);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

}  // namespace fv
