// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/overlay/feature_overlay.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>

#include <nlohmann/json.hpp>

#include "fvkit/app/source_spec.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/feature/value_text.h"
#include "fvkit/feature/vector_source.h"
#include "fvkit/log.h"
#include "fvkit/symbol/builtin_icons.h"
#include "fvkit/vector/renderer.h"

namespace fv {

const char FeatureOverlay::kTypeId[] = "fv.features";
const char FeatureOverlay::kExtension[] = "gpkg";
const char FeatureOverlay::kGeoJsonExtension[] = "geojson";

namespace {

std::string LowerExtension(const std::string& spec) {
  std::string ext = std::filesystem::path(spec).extension().string();
  if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

/// The GeoPackages open in this process by canonical path, so the overlays of
/// one file's layers share one connection and do not lock each other out.
struct OpenPackages {
  std::mutex mu;
  std::map<std::string, std::weak_ptr<GeoPackage>> by_path;
};

OpenPackages& Packages() {
  static OpenPackages packages;
  return packages;
}

std::string PackageKey(const std::string& path) {
  std::error_code ec;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  return ec ? path : canonical.string();
}

/// The open package at `path`, writable when the file allows; opened if no
/// overlay holds it.
Status SharedPackage(const std::string& path, std::shared_ptr<GeoPackage>* out) {
  OpenPackages& open = Packages();
  const std::string key = PackageKey(path);
  std::lock_guard<std::mutex> lock(open.mu);
  auto it = open.by_path.find(key);
  if (it != open.by_path.end()) {
    if (std::shared_ptr<GeoPackage> held = it->second.lock()) {
      *out = std::move(held);
      return Status::Ok();
    }
  }
  Status s = GeoPackage::Open(path, true, out);
  if (!s.ok()) s = GeoPackage::Open(path, false, out);
  if (!s.ok()) return s;
  open.by_path[key] = *out;
  return Status::Ok();
}

/// Drops `path` from the open packages, after the file there was replaced.
void ForgetPackage(const std::string& path) {
  OpenPackages& open = Packages();
  std::lock_guard<std::mutex> lock(open.mu);
  open.by_path.erase(PackageKey(path));
}

std::string LowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

/// A table name and a `gpkg_contents` identifier for a new layer `name`, unused
/// in `infos`: the name reduced to letters, digits and underscores, numbered
/// when taken; the identifier numbered too, since that column is UNIQUE.
void NewTableNames(const std::vector<GeoPackageLayerInfo>& infos, const std::string& name, std::string* table,
                   std::string* identifier) {
  std::string base;
  for (unsigned char c : name) base += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_';
  if (base.empty() || std::isdigit(static_cast<unsigned char>(base[0]))) base = "layer_" + base;
  auto taken = [&infos](const std::string& t) {
    return std::any_of(infos.begin(), infos.end(),
                       [&t](const GeoPackageLayerInfo& i) { return LowerAscii(i.table) == t; });
  };
  *table = base;
  for (int n = 2; taken(*table); ++n) *table = base + "_" + std::to_string(n);
  *identifier = name;
  for (int n = 2; std::any_of(infos.begin(), infos.end(),
                              [identifier](const GeoPackageLayerInfo& i) { return i.identifier == *identifier; });
       ++n)
    *identifier = name + " " + std::to_string(n);
}

/// The features `ids` of `source` (every feature when null) in a memory layer
/// named `name`, keeping their ids.
Status CopyFeatures(const IFeatureLayer& source, const std::vector<int64_t>* ids, const std::string& name,
                    std::shared_ptr<MemoryFeatureLayer>* out) {
  auto copy = std::make_shared<MemoryFeatureLayer>(name, source.Schema());
  std::vector<Feature> features;
  if (ids == nullptr) {
    Status s = source.All(&features);
    if (!s.ok()) return s;
  } else {
    for (int64_t id : *ids) {
      Feature f;
      Status s = source.Get(id, &f);
      if (!s.ok()) return s;
      features.push_back(std::move(f));
    }
  }
  for (const Feature& f : features) {
    Status s = copy->InsertWithId(f);
    if (!s.ok()) return s;
  }
  *out = std::move(copy);
  return Status::Ok();
}

/// Writes `layer` and `style_json` as a new GeoPackage at `path`, its table
/// named after the file. Written beside the target and moved over it, so a
/// failed write leaves the old file.
Status WritePackageFile(const IFeatureLayer& layer, const std::string& path, const std::string& style_json) {
  const std::string temp = path + ".saving";
  std::error_code ec;
  std::filesystem::remove(temp, ec);
  {
    std::shared_ptr<GeoPackage> out;
    Status s = GeoPackage::Create(temp, &out);
    if (!s.ok()) return s;
    std::string table = std::filesystem::path(path).stem().string();
    if (table.empty()) table = "features";
    s = out->WriteLayer(layer, table, nullptr);
    if (s.ok()) s = out->WriteLayerStyle(table, style_json);
    if (s.ok()) s = out->Save();
    if (!s.ok()) {
      out.reset();
      std::filesystem::remove(temp, ec);
      return s;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) return Status::Error(kIoError, "cannot replace " + path + ": " + ec.message());
  ForgetPackage(path);
  return Status::Ok();
}

/// Adds the features `ids` of `source` (every one when null) and `style_json`
/// as a new table for a layer named `name` in the GeoPackage at `path`, and
/// commits the table alone. `spec` receives `path|layername=table`.
Status AddTableToPackage(const std::string& path, const IFeatureLayer& source, const std::vector<int64_t>* ids,
                         const std::string& name, const std::string& style_json, std::string* spec) {
  if (LowerExtension(path) != FeatureOverlay::kExtension) return Status::Error(kInvalidArg, path + " is not a GeoPackage");
  std::shared_ptr<GeoPackage> package;
  Status s = SharedPackage(path, &package);
  if (!s.ok()) return s;
  if (!package->Writable()) return Status::Error(kInvalidArg, path + " cannot be written");
  std::vector<GeoPackageLayerInfo> infos;
  s = package->Layers(&infos);
  if (!s.ok()) return s;
  std::string table, identifier;
  NewTableNames(infos, name.empty() ? "features" : name, &table, &identifier);
  // WriteLayer takes the identifier from the layer's name.
  std::shared_ptr<MemoryFeatureLayer> copy;
  s = CopyFeatures(source, ids, identifier, &copy);
  if (s.ok()) s = package->WriteLayer(*copy, table, nullptr);
  if (s.ok()) s = package->WriteLayerStyle(table, style_json);
  // The new table commits alone; the file's open layers keep their unsaved edits.
  if (s.ok()) s = package->SaveTableChanges();
  if (!s.ok()) {
    package->RevertTableChanges();
    return s;
  }
  *spec = app::JoinSourceSpec(path, table);
  return Status::Ok();
}

/// A MapLibre engine loaded from style JSON, labels on, the builtin icons
/// standing in when the style names no sprite.
Status LoadEngine(const std::string& json, const std::string& font, MapLibreStyleEngine* engine) {
  engine->SetFallbackSprites(BuiltinIconSheet());
  Status s = engine->LoadText(json);
  if (!s.ok()) return s;
  engine->SetLabelFont(font);
  engine->SetDrawLabels(true);
  return Status::Ok();
}

/// The fraction of the view added on each side of a scene rebuilt after a pan.
constexpr double kPanSceneMargin = 0.25;

/// By schema index, the fields `style_json` or `filter_json` names, as a
/// quoted string or a `{field}` text token. A name JSON would escape is always kept.
std::vector<bool> UsedFields(const FeatureSchema& schema, const std::string& style_json,
                             const std::string& filter_json) {
  std::vector<bool> keep(schema.fields.size(), false);
  for (size_t i = 0; i < schema.fields.size(); ++i) {
    const std::string& name = schema.fields[i].name;
    const bool plain = std::all_of(name.begin(), name.end(), [](char c) {
      const auto u = static_cast<unsigned char>(c);
      return u >= 0x20 && u < 0x80 && c != '"' && c != '\\';
    });
    const std::string quoted = '"' + name + '"';
    keep[i] = !plain || style_json.find(quoted) != std::string::npos ||
              style_json.find('{' + name + '}') != std::string::npos ||
              filter_json.find(quoted) != std::string::npos;
  }
  return keep;
}

/// Where a search result points: the point, a line's middle vertex, an
/// area's envelope centre.
bool SearchAnchor(const Geometry& g, GeoPoint* out) {
  if (g.type == GeometryType::kPoint || g.type == GeometryType::kLineString) {
    if (g.paths.empty() || g.paths[0].empty()) return false;
    *out = g.paths[0][g.paths[0].size() / 2].point();
    return true;
  }
  if (g.type == GeometryType::kPolygon) {
    GeoRect box;
    if (!g.Envelope(&box)) return false;
    *out = GeoPoint{(box.ll.lat + box.ur.lat) / 2, (box.ll.lon + box.ur.lon) / 2};
    return true;
  }
  return !g.parts.empty() && SearchAnchor(g.parts.front(), out);
}

}  // namespace

bool SchemasMatchForAppend(const FeatureSchema& from, const FeatureSchema& to) {
  if (from.geometry_type != to.geometry_type || from.fields.size() != to.fields.size()) return false;
  for (const FieldDef& f : from.fields) {
    const auto same = std::find_if(to.fields.begin(), to.fields.end(), [&f](const FieldDef& t) {
      return LowerAscii(t.name) == LowerAscii(f.name);
    });
    if (same == to.fields.end() || same->type != f.type) return false;
  }
  return true;
}

bool PackageIsOpen(const std::string& path) {
  OpenPackages& open = Packages();
  std::lock_guard<std::mutex> lock(open.mu);
  const auto it = open.by_path.find(PackageKey(path));
  return it != open.by_path.end() && !it->second.expired();
}

FeatureOverlay::FeatureOverlay(std::string name)
    : Overlay(std::move(name)), edit_(std::make_unique<FeatureEditSession>(*this)) {}

FeatureOverlay::~FeatureOverlay() = default;

void FeatureOverlay::SetSelected(int64_t id) {
  selection_.clear();
  if (id != 0) selection_.push_back(id);
}

void FeatureOverlay::SetSelection(std::vector<int64_t> ids) {
  selection_.clear();
  for (int64_t id : ids)
    if (id != 0 && !IsSelected(id)) selection_.push_back(id);
}

bool FeatureOverlay::IsSelected(int64_t id) const {
  return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void FeatureOverlay::SetSelectionStyle(const FeatureSelectionStyle& style) {
  has_selection_style_ = true;
  selection_style_ = style;
}

void FeatureOverlay::ClearSelectionStyle() {
  has_selection_style_ = false;
  selection_style_ = FeatureSelectionStyle();
}

Status FeatureOverlay::SetDefinitionQuery(const std::string& json) {
  FeatureFilter filter;
  Status s = FeatureFilter::Parse(json, &filter);
  if (!s.ok()) return s;
  definition_query_ = std::move(filter);
  ++definition_generation_;
  if (layer_ && !definition_query_.empty()) {
    std::vector<int64_t> kept;
    for (int64_t id : selection_) {
      Feature f;
      if (layer_->Get(id, &f).ok() && Shows(f)) kept.push_back(id);
    }
    selection_ = std::move(kept);
  }
  return Status::Ok();
}

bool FeatureOverlay::Shows(const Feature& f) const {
  return definition_query_.empty() || !layer_ || definition_query_.Accepts(f, layer_->Schema());
}

Status FeatureOverlay::QueryShown(const GeoRect& box, std::vector<Feature>* out) const {
  out->clear();
  if (!layer_) return Status::Ok();
  Status s = layer_->Query(box, out);
  if (!s.ok() || definition_query_.empty()) return s;
  out->erase(std::remove_if(out->begin(), out->end(), [this](const Feature& f) { return !Shows(f); }), out->end());
  return Status::Ok();
}

Status FeatureOverlay::AllShown(std::vector<Feature>* out) const {
  out->clear();
  if (!layer_) return Status::Ok();
  Status s = layer_->All(out);
  if (!s.ok() || definition_query_.empty()) return s;
  out->erase(std::remove_if(out->begin(), out->end(), [this](const Feature& f) { return !Shows(f); }), out->end());
  return Status::Ok();
}

bool FeatureOverlay::Draws(const Feature& f, double scale_denominator) const {
  if (!Shows(f)) return false;
  if (style_.renderer != FeatureRenderer::kRules || !layer_) return true;
  if (!draws_engine_ || draws_style_ != style_) {
    std::string json;
    auto engine = std::make_unique<MapLibreStyleEngine>();
    if (!WriteFeatureStyleJson(style_, &json).ok() || !LoadEngine(json, std::string(), engine.get()).ok()) {
      draws_engine_.reset();
      return false;
    }
    draws_engine_ = std::move(engine);
    draws_style_ = style_;
  }
  StyleContext ctx;
  ctx.scale_denominator = scale_denominator;
  ctx.device_dpi = 96.0 * display_scale_;
  std::vector<VectorFeature> pieces;
  FeatureToVector(f, layer_->Schema(), &pieces);
  std::vector<StyleResult> results;
  for (const VectorFeature& piece : pieces) {
    results.clear();
    if (!draws_engine_->Style(piece, ctx, &results).ok()) continue;
    const bool area = piece.type == VectorGeometryType::kArea;
    const bool point = piece.type == VectorGeometryType::kPoint;
    for (const StyleResult& r : results) {
      if (!r.visible) continue;
      if (r.symbol.valid || (r.label.valid && !r.label.text.empty())) return true;
      if (!point && (r.stroke.valid || r.line_pattern.valid)) return true;
      if (area && (r.fill.valid || r.area_pattern.valid)) return true;
    }
  }
  return false;
}

Status FeatureOverlay::QueryDrawn(const GeoRect& box, double scale_denominator, std::vector<Feature>* out) const {
  Status s = QueryShown(box, out);
  if (!s.ok() || style_.renderer != FeatureRenderer::kRules) return s;
  out->erase(std::remove_if(out->begin(), out->end(),
                            [this, scale_denominator](const Feature& f) { return !Draws(f, scale_denominator); }),
             out->end());
  return Status::Ok();
}

void FeatureOverlay::SnapToPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                                 std::vector<app::SnapToItem>& out) {
  if (!layer_ || !IsVisible() || !proj.Ready()) return;
  GeoPoint lo, hi;
  if (!proj.SurfaceToGeo(p.x - tolerance_px, p.y + tolerance_px, &lo).ok() ||
      !proj.SurfaceToGeo(p.x + tolerance_px, p.y - tolerance_px, &hi).ok())
    return;
  GeoRect box{GeoPoint{std::min(lo.lat, hi.lat), std::min(lo.lon, hi.lon)},
              GeoPoint{std::max(lo.lat, hi.lat), std::max(lo.lon, hi.lon)}};
  std::vector<Feature> features;
  if (!QueryDrawn(box, proj.Scale(), &features).ok()) return;
  const size_t first = out.size();
  for (const Feature& f : features) {
    auto visit = [&](const Geometry& g, int part) {
      for (size_t r = 0; r < g.paths.size(); ++r) {
        const Path& path = g.paths[r];
        // A ring's closing vertex is its first one again.
        const size_t n = g.type == GeometryType::kPolygon && path.size() > 1 ? path.size() - 1 : path.size();
        for (size_t i = 0; i < n; ++i) {
          if (edit_->IsDragged(f.id, part, static_cast<int>(r), static_cast<int>(i))) continue;
          double x = 0, y = 0;
          if (!proj.GeoToSurface(path[i].point(), &x, &y).ok()) continue;
          const double d = std::hypot(x - p.x, y - p.y);
          if (d > tolerance_px) continue;
          app::SnapToItem item;
          item.point = path[i].point();
          item.description = Name() + ": " + FeatureTitle(f);
          item.overlay = this;
          item.distance_px = d;
          out.push_back(std::move(item));
        }
      }
    };
    if (f.geometry.parts.empty()) {
      visit(f.geometry, -1);
    } else {
      for (size_t k = 0; k < f.geometry.parts.size(); ++k) visit(f.geometry.parts[k], static_cast<int>(k));
    }
  }
  std::stable_sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
                   [](const app::SnapToItem& a, const app::SnapToItem& b) { return a.distance_px < b.distance_px; });
}

Status FeatureOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  features_drawn_ = 0;
  last_proj_ = proj;
  have_proj_ = proj.Ready();
  Status s = DrawFeatures(proj, canvas);
  if (!s.ok()) return s;
  if (edit_->has_edit_focus() && have_proj_) return edit_->DrawChrome(proj, canvas);
  return Status::Ok();
}

Status FeatureOverlay::DrawFeatures(const MapProjection& proj, ICanvas& canvas) {
  if (!layer_ || !proj.Ready() || opacity() <= 0) return Status::Ok();
  if (opacity() >= 100) return DrawLayer(proj, canvas);

  // Drawn opaque off screen and blended once, so overlapping strokes and
  // fills fade as one layer rather than each darkening the others.
  const PixelSize size = canvas.Size();
  CpuCanvas layer(size.width, size.height);
  layer.Clear({0, 0, 0, 0});
  if (!label_font_.empty()) layer.SetDefaultFont(label_font_);
  Status s = DrawLayer(proj, layer);
  if (!s.ok()) return s;
  PixelBuffer& buf = layer.Buffer();
  const int alpha = opacity();
  for (int y = 0; y < buf.Height(); ++y) {
    unsigned char* row = buf.Row(y);
    for (int x = 0; x < buf.Width(); ++x) row[4 * x + 3] = static_cast<unsigned char>(row[4 * x + 3] * alpha / 100);
  }
  return canvas.DrawPixmap(buf, 0, 0);
}

Status FeatureOverlay::DrawLayer(const MapProjection& proj, ICanvas& canvas) {
  if (!engine_) engine_ = std::make_shared<MapLibreStyleEngine>();
  std::string json;
  Status s = WriteFeatureStyleJson(style_, &json, selection_,
                                   has_selection_style_ ? selection_style_ : FeatureSelectionStyle());
  if (!s.ok()) return s;
  bool rebuild = false;
  if (json != engine_json_ || label_font_ != engine_font_) {
    s = LoadEngine(json, label_font_, engine_.get());
    if (!s.ok()) return s;
    engine_json_ = std::move(json);
    engine_font_ = label_font_;
    rebuild = true;
  }
  if (!source_) {
    source_ = std::make_shared<FeatureVectorSource>(layer_);
    source_->SetFilter([this](const Feature& f) { return Shows(f); });
    renderer_ = std::make_unique<VectorRenderer>(source_, engine_);
    renderer_->SetPickEnabled(false);
    rebuild = true;
  }
  // The definition query filters the features read, so its fields are read too.
  std::vector<bool> fields = UsedFields(layer_->Schema(), engine_json_, definition_query_.text());
  if (fields != source_fields_) {
    source_->SetAttributeFields(fields);
    source_fields_ = std::move(fields);
    rebuild = true;
  }
  // The scene is kept while the layer, the style and the definition query
  // are as it was built from; a layer without a revision is always rebuilt.
  uint64_t revision = 0;
  const bool tracked = layer_->Revision(&revision);
  if (!tracked || revision != scene_revision_ || definition_generation_ != scene_definition_generation_)
    rebuild = true;
  if (rebuild) renderer_->InvalidateScene();
  scene_revision_ = revision;
  scene_definition_generation_ = definition_generation_;
  renderer_->SetDeviceDpi(96.0 * display_scale_);
  // A rebuild at the scale already drawn follows a pan, so the scene takes in
  // a margin the next pans reuse; the first draw at a scale stays fast.
  renderer_->SetSceneMargin(proj.Scale() == scene_scale_ ? kPanSceneMargin : 0.0);
  scene_scale_ = proj.Scale();
  s = renderer_->Render(proj, &canvas);
  features_drawn_ = source_->features_returned();
  return s;
}

void FeatureOverlay::SetLayer(std::shared_ptr<IEditableFeatureLayer> layer) {
  layer_ = std::move(layer);
  source_.reset();
  source_fields_.clear();
  renderer_.reset();
  package_.reset();
  selection_.clear();
  edit_->ClearHistory();
  edit_->CancelSketch();
  if (layer_) SetName(layer_->Name());
}

void FeatureOverlay::HitTestPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                                  std::vector<app::HitItem>& out) {
  (void)proj;
  (void)tolerance_px;
  if (!layer_) return;
  const int64_t id = edit_->FeatureAt(p);
  Feature f;
  if (id == 0 || !layer_->Get(id, &f).ok()) return;
  app::HitItem item;
  item.overlay = this;
  item.feature = static_cast<uint64_t>(id);
  item.hint.tool_tip = FeatureMapTip(f);
  item.hint.status = Name() + ": " + item.hint.tool_tip;
  out.push_back(std::move(item));
}

std::optional<app::ItemInfoSheet> FeatureOverlay::InfoAt(const MapProjection& proj, PixelPoint p,
                                                         double tolerance_px) {
  (void)proj;
  (void)tolerance_px;
  if (!layer_) return std::nullopt;
  const int64_t id = edit_->FeatureAt(p);
  Feature f;
  if (id == 0 || !layer_->Get(id, &f).ok()) return std::nullopt;
  app::ItemInfoSheet sheet;
  sheet.title = FeatureTitle(f);
  sheet.rows.push_back({"Feature", "Layer", Name()});
  sheet.rows.push_back({"Feature", "Geometry", GeometryTypeTitle(f.geometry.type)});
  sheet.rows.push_back({"Feature", "ID", std::to_string(f.id)});
  const FeatureSchema& schema = layer_->Schema();
  for (size_t i = 0; i < schema.fields.size() && i < f.values.size(); ++i) {
    const FieldDef& field = schema.fields[i];
    std::string value = FormatFieldValue(field, f.values[i]);
    if (!value.empty() && !field.unit.empty()) value += " " + field.unit;
    sheet.rows.push_back({"Attributes", field.DisplayTitle(), std::move(value)});
  }
  return sheet;
}

std::string FeatureOverlay::FeatureTitle(const Feature& f) const {
  int field = -1;
  if (layer_) {
    const FeatureSchema& schema = layer_->Schema();
    if (!style_.label.field.empty()) field = schema.Find(style_.label.field);
    if (field < 0) field = schema.Find("name");
    for (size_t i = 0; field < 0 && i < schema.fields.size(); ++i)
      if (schema.fields[i].type == FieldType::kText) field = static_cast<int>(i);
  }
  std::string title;
  if (field >= 0 && static_cast<size_t>(field) < f.values.size()) title = FeatureLabelText(f.values[field]);
  return title.empty() ? "Feature " + std::to_string(f.id) : title;
}

std::string FeatureOverlay::FeatureMapTip(const Feature& f) const {
  if (layer_ && !map_tip_field_.empty()) {
    const int field = layer_->Schema().Find(map_tip_field_);
    if (field >= 0 && static_cast<size_t>(field) < f.values.size()) {
      std::string tip = FeatureLabelText(f.values[field]);
      if (!tip.empty()) return tip;
    }
  }
  return FeatureTitle(f);
}

std::string FeatureOverlay::FeatureHyperlink(const Feature& f) const {
  if (!layer_ || hyperlink_field_.empty()) return std::string();
  const int field = layer_->Schema().Find(hyperlink_field_);
  if (field < 0 || static_cast<size_t>(field) >= f.values.size()) return std::string();
  return FeatureLabelText(f.values[field]);
}

void FeatureOverlay::Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
                            std::vector<app::SearchResult>& out) {
  if (!layer_) return;
  std::vector<Feature> features;
  const Status s = q.area ? QueryShown(*q.area, &features) : AllShown(&features);
  if (!s.ok()) return;
  const size_t before = out.size();
  for (const Feature& f : features) {
    if (cancel.load()) return;
    if (q.max_results > 0 && out.size() - before >= q.max_results) return;
    app::SearchResult r;
    if (!SearchAnchor(f.geometry, &r.position) || !app::SearchAreaAccepts(q, r.position)) continue;
    r.title = FeatureTitle(f);
    if (!app::SearchTextAccepts(q, r.title, &r.match_quality)) continue;
    if (!f.geometry.Envelope(&r.bounds)) r.bounds = GeoRect{r.position, r.position};
    r.detail = std::string("feature \xc2\xb7 ") + Name();
    r.overlay = this;
    r.feature = static_cast<uint64_t>(f.id);
    out.push_back(std::move(r));
  }
}

Status FeatureOverlay::ChangeSchema(const SchemaChange& change) {
  if (!layer_) return Status::Error(kInvalidArg, "no feature layer");
  const FeatureSchema old = layer_->Schema();
  Status s = layer_->ChangeSchema(change);
  if (!s.ok()) return s;
  RemapStyleFields(&style_, old, change);
  source_.reset();
  source_fields_.clear();
  renderer_.reset();
  edit_->CancelSketch();
  edit_->ClearHistory();
  return Status::Ok();
}

Status FeatureOverlay::FileNew() {
  SetLayer(std::make_shared<MemoryFeatureLayer>(Name(), FeatureSchema{}));
  return Status::Ok();
}

Status FeatureOverlay::Load(const std::string& spec) {
  const app::SourceSpec source = app::SplitSourceSpec(spec);
  const std::string ext = LowerExtension(source.path);
  if (ext == kGeoJsonExtension || ext == "json") {
    std::shared_ptr<MemoryFeatureLayer> layer;
    std::string style_json;
    Status s = ReadGeoJsonFile(source.path, &layer, &style_json);
    if (!s.ok()) return s;
    SetLayer(std::move(layer));
    spec_layer_.clear();
    // A plain Save rewrites the file in the format it was read in.
    set_save_format_index(kFormatGeoJson);
    UseStoredStyle(spec, style_json);
    return Status::Ok();
  }
  std::shared_ptr<GeoPackage> package;
  Status s = SharedPackage(source.path, &package);
  if (!s.ok()) return s;
  std::vector<GeoPackageLayerInfo> infos;
  s = package->Layers(&infos);
  if (!s.ok()) return s;
  if (infos.empty()) return Status::Error(kNotFound, source.path + " has no feature tables");
  std::string table = infos.front().table;
  if (!source.layer.empty()) {
    const auto named = std::find_if(infos.begin(), infos.end(),
                                    [&source](const GeoPackageLayerInfo& i) { return i.table == source.layer; });
    if (named == infos.end())
      return Status::Error(kNotFound, source.path + " has no feature table \"" + source.layer + "\"");
    table = named->table;
  }
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  s = package->OpenLayer(table, &layer);
  if (!s.ok()) return s;
  std::string style_json;
  s = package->ReadLayerStyle(table, &style_json);
  if (!s.ok() && s.code != kNotFound) FV_LOG_WARNING(spec << ": layer style: " << s.message);
  SetLayer(layer);
  package_ = std::move(package);
  spec_layer_ = source.layer;
  set_save_format_index(kFormatGeoPackage);
  UseStoredStyle(spec, style_json);
  return Status::Ok();
}

Status FeatureOverlay::ListLayers(const std::string& path, std::vector<std::string>* layers) {
  layers->clear();
  if (LowerExtension(path) != kExtension) return Status::Ok();
  std::shared_ptr<GeoPackage> package;
  Status s = SharedPackage(path, &package);
  if (!s.ok()) return s;
  std::vector<GeoPackageLayerInfo> infos;
  s = package->Layers(&infos);
  if (!s.ok()) return s;
  for (const GeoPackageLayerInfo& i : infos) layers->push_back(i.table);
  return Status::Ok();
}

void FeatureOverlay::UseStoredStyle(const std::string& spec, const std::string& style_json) {
  style_ = FeatureStyle{};
  SetDescription(std::string());
  SetCredits(std::string());
  SetScaleRange(0, 0);
  map_tip_field_.clear();
  hyperlink_field_.clear();
  ClearSelectionStyle();
  definition_query_ = FeatureFilter();
  if (style_json.empty()) return;
  FeatureLayerInfo info;
  if (ReadFeatureLayerInfoJson(style_json, &info).ok()) {
    if (!info.name.empty()) SetName(info.name);
    SetDescription(info.description);
    SetCredits(info.credits);
    SetScaleRange(info.min_scale, info.max_scale);
    map_tip_field_ = info.map_tip_field;
    hyperlink_field_ = info.hyperlink_field;
    if (info.has_selection_style) SetSelectionStyle(info.selection);
    const Status q = SetDefinitionQuery(info.definition_query);
    if (!q.ok()) FV_LOG_WARNING(spec << ": definition query: " << q.message << "; every feature is shown");
  }
  FeatureStyle style;
  const Status s = ReadFeatureStyleJson(style_json, &style);
  if (!s.ok()) {
    FV_LOG_WARNING(spec << ": " << s.message << "; drawn with the default style");
    return;
  }
  style_ = std::move(style);
}

Status FeatureOverlay::FileOpen(const std::string& spec) { return Load(spec); }

Status FeatureOverlay::StyleDocument(bool keep_name, std::string* json) const {
  Status st = WriteFeatureStyleJson(style_, json);
  if (!st.ok()) return st;
  FeatureLayerInfo info;
  if (keep_name && Name() != layer_->Name()) info.name = Name();
  info.description = description();
  info.credits = credits();
  info.min_scale = min_scale();
  info.max_scale = max_scale();
  info.map_tip_field = map_tip_field_;
  info.hyperlink_field = hyperlink_field_;
  info.has_selection_style = has_selection_style_;
  info.selection = selection_style_;
  info.definition_query = definition_query_.text();
  return WriteFeatureLayerInfoJson(info, json);
}

Status FeatureOverlay::FileSaveAs(const std::string& spec, int format_index) {
  if (!layer_) return Status::Error(kInvalidArg, "no feature layer to save");
  std::string style_json;
  Status st = StyleDocument(/*keep_name=*/true, &style_json);
  if (!st.ok()) return st;
  if (format_index == kFormatGeoJson) {
    Status s = WriteGeoJsonFile(*layer_, spec, style_json);
    if (!s.ok()) return s;
    return Load(spec);
  }
  if (format_index != kFormatGeoPackage)
    return Status::Error(kUnsupported, "feature layers save as GeoPackage (0) or GeoJSON (1), asked for " +
                                           std::to_string(format_index));
  const app::SourceSpec target = app::SplitSourceSpec(spec);
  std::error_code ec;
  if (package_ && std::filesystem::equivalent(package_->Path(), target.path, ec)) {
    if (target.layer.empty() && !spec_layer_.empty())
      return Status::Error(kInvalidArg, target.path + " holds this layer with others; save it as another file");
    auto* table = dynamic_cast<GeoPackageFeatureLayer*>(layer_.get());
    if (table == nullptr) return Status::Error(kInternal, "the layer is not a table of " + target.path);
    // This layer's edits and style only: the file's other layers save on their own.
    return table->Save(&style_json);
  }
  Status s = WritePackageFile(*layer_, spec, style_json);
  if (!s.ok()) return s;
  return Load(spec);
}

std::vector<std::string> FeatureOverlay::SaveAsWouldReplace(const std::string& spec, int format_index) {
  const app::SourceSpec target = app::SplitSourceSpec(spec);
  std::error_code ec;
  if (format_index != kFormatGeoPackage || !target.layer.empty() || LowerExtension(target.path) != kExtension ||
      !std::filesystem::exists(target.path, ec))
    return {};
  std::shared_ptr<GeoPackage> package;
  std::vector<GeoPackageLayerInfo> infos;
  if (!SharedPackage(target.path, &package).ok() || !package->Layers(&infos).ok() || infos.size() < 2) return {};
  const auto* own = dynamic_cast<const GeoPackageFeatureLayer*>(layer_.get());
  const bool same_file = package_ == package && own != nullptr;
  // Its own file opened by the plain path: the save commits this table alone.
  if (same_file && spec_layer_.empty()) return {};
  std::vector<std::string> others;
  for (const GeoPackageLayerInfo& i : infos)
    if (!same_file || LowerAscii(i.table) != LowerAscii(own->Table())) others.push_back(i.identifier);
  return others;
}

Status FeatureOverlay::FileSaveAsAddedLayer(const std::string& path, std::string* spec) {
  if (!layer_) return Status::Error(kInvalidArg, "no feature layer to save");
  if (spec == nullptr) return Status::Error(kInvalidArg, "FileSaveAsAddedLayer: null spec");
  std::string style_json;
  Status s = StyleDocument(/*keep_name=*/false, &style_json);
  if (!s.ok()) return s;
  s = AddTableToPackage(path, *layer_, nullptr, Name(), style_json, spec);
  if (!s.ok()) return s;
  return Load(*spec);
}

Status FeatureOverlay::AppendTargets(const std::string& path, std::vector<GeoPackageLayerInfo>* out) const {
  out->clear();
  if (!layer_) return Status::Error(kInvalidArg, "no feature layer");
  std::shared_ptr<GeoPackage> package;
  Status s = SharedPackage(path, &package);
  if (!s.ok()) return s;
  std::vector<GeoPackageLayerInfo> infos;
  s = package->Layers(&infos);
  if (!s.ok()) return s;
  const auto* own = dynamic_cast<const GeoPackageFeatureLayer*>(layer_.get());
  for (const GeoPackageLayerInfo& i : infos) {
    if (own != nullptr && package_ == package && LowerAscii(i.table) == LowerAscii(own->Table())) continue;
    std::shared_ptr<GeoPackageFeatureLayer> table = package->FindOpenLayer(i.table);
    if (!table && !package->OpenLayer(i.table, &table).ok()) continue;  // a table this reader cannot open
    if (SchemasMatchForAppend(layer_->Schema(), table->Schema())) out->push_back(i);
  }
  return Status::Ok();
}

Status FeatureOverlay::ExportSelection(const FeatureExport& to, std::string* spec) {
  if (!layer_) return Status::Error(kInvalidArg, "no feature layer to export");
  if (spec == nullptr) return Status::Error(kInvalidArg, "ExportSelection: null spec");
  if (selection_.empty()) return Status::Error(kInvalidArg, "no features are selected");
  std::string style_json;
  Status s = StyleDocument(/*keep_name=*/false, &style_json);
  if (!s.ok()) return s;

  switch (to.mode) {
    case FeatureExport::Mode::kNewFile: {
      if (PackageIsOpen(to.path))
        return Status::Error(kInvalidArg, to.path + " is open; export into it as a new layer instead");
      std::shared_ptr<MemoryFeatureLayer> copy;
      s = CopyFeatures(*layer_, &selection_, Name(), &copy);
      if (s.ok()) s = WritePackageFile(*copy, to.path, style_json);
      if (!s.ok()) return s;
      std::string table = std::filesystem::path(to.path).stem().string();
      *spec = app::JoinSourceSpec(to.path, table.empty() ? "features" : table);
      return Status::Ok();
    }
    case FeatureExport::Mode::kNewTable:
      return AddTableToPackage(to.path, *layer_, &selection_, Name(), style_json, spec);
    case FeatureExport::Mode::kAppend:
      break;
  }

  std::shared_ptr<GeoPackage> package;
  s = SharedPackage(to.path, &package);
  if (!s.ok()) return s;
  if (!package->Writable()) return Status::Error(kInvalidArg, to.path + " cannot be written");
  // An open table is appended to through its own layer, so the overlay showing it sees the rows.
  std::shared_ptr<GeoPackageFeatureLayer> table = package->FindOpenLayer(to.table);
  if (!table) {
    s = package->OpenLayer(to.table, &table);
    if (!s.ok()) return s;
  }
  if (table.get() == layer_.get()) return Status::Error(kInvalidArg, "cannot append a layer to itself");
  if (table->HasEdits())
    return Status::Error(kInvalidArg, table->Name() + " has unsaved edits; save or revert it before appending to it");
  const FeatureSchema& from = layer_->Schema();
  const FeatureSchema& into = table->Schema();
  if (!SchemasMatchForAppend(from, into))
    return Status::Error(kInvalidArg, table->Name() + " has other fields; export as a new layer instead");
  // The target's field order may differ from the source's.
  std::vector<size_t> source_index(into.fields.size());
  for (size_t i = 0; i < into.fields.size(); ++i)
    for (size_t j = 0; j < from.fields.size(); ++j)
      if (LowerAscii(from.fields[j].name) == LowerAscii(into.fields[i].name)) source_index[i] = j;
  for (int64_t id : selection_) {
    Feature f;
    s = layer_->Get(id, &f);
    if (!s.ok()) break;
    Feature row;
    row.geometry = std::move(f.geometry);
    row.values.resize(into.fields.size());
    for (size_t i = 0; i < into.fields.size(); ++i) row.values[i] = f.values[source_index[i]];
    s = table->Insert(&row);
    if (!s.ok()) break;
  }
  if (s.ok()) s = table->Save();
  if (!s.ok()) {
    table->DiscardEdits();
    return s;
  }
  *spec = app::JoinSourceSpec(to.path, table->Table());
  return Status::Ok();
}

// The edits live in the layer, so reading the table afresh discards them and
// leaves the file's other layers as they are.
Status FeatureOverlay::Revert(const std::string& spec) {
  Status s = Load(spec);
  if (!s.ok()) return s;
  set_dirty(false);
  return Status::Ok();
}

void FeatureOverlay::NameTableInSpec() {
  const auto* table = dynamic_cast<const GeoPackageFeatureLayer*>(layer_.get());
  if (!package_ || table == nullptr || !spec_layer_.empty() || file_spec().empty()) return;
  spec_layer_ = table->Table();
  set_file_spec(app::JoinSourceSpec(app::SplitSourceSpec(file_spec()).path, spec_layer_));
}

Status AddFeatureLayerToPackage(const std::string& path, const std::string& name, const FeatureSchema& schema,
                                std::string* spec) {
  if (spec == nullptr) return Status::Error(kInvalidArg, "AddFeatureLayerToPackage: null spec");
  if (name.empty()) return Status::Error(kInvalidArg, "the layer needs a name");
  Status s = schema.Validate();
  if (!s.ok()) return s;
  if (LowerExtension(path) != FeatureOverlay::kExtension)
    return Status::Error(kInvalidArg, path + " is not a GeoPackage");
  std::shared_ptr<GeoPackage> package;
  s = SharedPackage(path, &package);
  if (!s.ok()) return s;
  if (!package->Writable()) return Status::Error(kInvalidArg, path + " cannot be written");

  std::vector<GeoPackageLayerInfo> infos;
  s = package->Layers(&infos);
  if (!s.ok()) return s;
  std::string table;
  GeoPackageLayerOptions options;
  NewTableNames(infos, name, &table, &options.identifier);
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  std::string style_json;
  s = package->CreateLayer(table, schema, options, &layer);
  if (s.ok()) s = WriteFeatureStyleJson(FeatureStyle(), &style_json);
  if (s.ok()) s = package->WriteLayerStyle(table, style_json);
  // The new table commits alone; the file's open layers keep their unsaved edits.
  if (s.ok()) s = package->SaveTableChanges();
  if (!s.ok()) {
    package->RevertTableChanges();
    return s;
  }
  *spec = app::JoinSourceSpec(path, table);
  return Status::Ok();
}

Status CreateFeatureLayerFile(const std::string& spec, int format, const std::string& name,
                              const FeatureSchema& schema) {
  Status s = schema.Validate();
  if (!s.ok()) return s;
  FeatureOverlay writer(name);
  writer.SetLayer(std::make_shared<MemoryFeatureLayer>(name, schema));
  return writer.FileSaveAs(spec, format);
}

namespace {

/// Renders one feature of `geometry` across `canvas` through the style
/// document `style_json`, as the map draws it. A line keeps `line_margin`
/// pixels clear of the edges; labels use `font`.
Status RenderSwatch(ICanvas& canvas, const std::string& style_json, GeometryType geometry, double line_margin,
                    double scale, const std::string& font) {
  const PixelSize size = canvas.Size();
  const int w = size.width, h = size.height;
  MapLibreStyleEngine engine;
  Status s = LoadEngine(style_json, font, &engine);
  if (!s.ok()) return s;

  // A small projection over the swatch, so the one feature goes through the
  // same renderer as the map.
  MapProjection proj;
  proj.SetSurfaceSize(w, h);
  proj.SetCenter({0.0, 0.0});
  proj.SetScale(100000.0);
  auto geo = [&proj](double x, double y) {
    GeoPoint g;
    proj.SurfaceToGeo(x, y, &g);
    return Position{g.lat, g.lon};
  };
  Feature f;
  f.id = 1;
  switch (geometry) {
    case GeometryType::kPoint:
    case GeometryType::kMultiPoint:
      f.geometry.type = GeometryType::kPoint;
      f.geometry.paths = {{geo(w / 2.0, h / 2.0)}};
      break;
    case GeometryType::kLineString:
    case GeometryType::kMultiLineString: {
      const double m = line_margin;
      f.geometry.type = GeometryType::kLineString;
      f.geometry.paths = {{geo(m, h - m), geo(w / 3.0, m + (h - 2 * m) / 3),
                           geo(2 * w / 3.0, h - m - (h - 2 * m) / 3), geo(w - m, m)}};
      break;
    }
    default: {
      const double m = 2;
      f.geometry.type = GeometryType::kPolygon;
      f.geometry.paths = {{geo(m, m), geo(w - 1 - m, m), geo(w - 1 - m, h - 1 - m),
                           geo(m, h - 1 - m), geo(m, m)}};
      break;
    }
  }
  auto layer = std::make_shared<MemoryFeatureLayer>("swatch", FeatureSchema{});
  Status ins = layer->Insert(&f);
  if (!ins.ok()) return ins;
  VectorRenderer renderer(std::make_shared<FeatureVectorSource>(layer),
                          std::shared_ptr<IStyleEngine>(&engine, [](IStyleEngine*) {}));
  renderer.SetDeviceDpi(96.0 * scale);
  renderer.SetPickEnabled(false);
  return renderer.Render(proj, &canvas);
}

/// The geometry a rule draws, from its facet or type: areas for fill rules
/// and area outlines, lines for other line rules, points for the rest.
GeometryType RuleGeometry(const StyleRule& rule) {
  if (rule.type == "fill" || rule.facet == "fv-fill" || rule.facet == "fv-area-outline") return GeometryType::kPolygon;
  if (rule.type == "line") return GeometryType::kLineString;
  return GeometryType::kPoint;
}

bool IsGeometryTerm(const nlohmann::json& term) {
  return term.is_array() && term.size() >= 2 && term[1] == nlohmann::json::array({"geometry-type"});
}

/// The terms of `filter` that test `["geometry-type"]`, as a filter; "" for
/// none. A swatch keeps these so each rule draws only the geometry it is for.
std::string GeometryTerms(const std::string& filter) {
  const nlohmann::json f = nlohmann::json::parse(filter, nullptr, /*allow_exceptions=*/false);
  if (IsGeometryTerm(f)) return filter;
  if (!f.is_array() || f.empty() || f[0] != "all") return std::string();
  nlohmann::json kept = nlohmann::json::array({"all"});
  for (size_t i = 1; i < f.size(); ++i)
    if (IsGeometryTerm(f[i])) kept.push_back(f[i]);
  if (kept.size() == 1) return std::string();
  return kept.size() == 2 ? kept[1].dump() : kept.dump();
}

}  // namespace

Status DrawFeatureSwatch(ICanvas& canvas, const FeatureSymbol& symbol, GeometryType geometry,
                         double scale) {
  const PixelSize size = canvas.Size();
  const int w = size.width, h = size.height;
  if (w < 4 || h < 4) return Status::Ok();
  if (!(scale > 0)) scale = 1.0;

  FeatureStyle style;
  style.symbol = symbol;
  // A marker no larger than the swatch.
  style.symbol.marker_size_px = std::min(symbol.marker_size_px, (std::min(w, h) - 2) / scale);
  std::string json;
  Status s = WriteFeatureStyleJson(style, &json);
  if (!s.ok()) return s;
  return RenderSwatch(canvas, json, geometry, 3 + symbol.line_width_px * scale / 2 + symbol.casing_px * scale,
                      scale, std::string());
}

Status DrawRulesSwatch(ICanvas& canvas, const std::vector<StyleRule>& rules, GeometryType geometry,
                       double scale, const std::string& font) {
  const PixelSize size = canvas.Size();
  if (size.width < 4 || size.height < 4 || rules.empty()) return Status::Ok();
  if (!(scale > 0)) scale = 1.0;
  FeatureStyle style;
  style.renderer = FeatureRenderer::kRules;
  for (StyleRule r : rules) {
    r.filter = GeometryTerms(r.filter);
    r.min_zoom = 0;
    r.max_zoom = 24;
    r.shown = true;
    // The swatch feature has no fields; a label shows sample text.
    nlohmann::json layout = nlohmann::json::parse(r.layout, nullptr, /*allow_exceptions=*/false);
    if (layout.is_object() && layout.contains("text-field")) {
      layout["text-field"] = "Aa";
      layout.erase("symbol-placement");
      r.layout = layout.dump();
    }
    style.rules.push_back(std::move(r));
  }
  if (geometry == GeometryType::kGeometry || geometry == GeometryType::kGeometryCollection) {
    geometry = GeometryType::kPoint;
    for (const StyleRule& r : rules)
      if (r.type != "symbol") {
        geometry = RuleGeometry(r);
        break;
      }
  }
  std::string json;
  Status s = WriteFeatureStyleJson(style, &json);
  if (!s.ok()) return s;
  return RenderSwatch(canvas, json, geometry, 6, scale, font);
}

}  // namespace fv
