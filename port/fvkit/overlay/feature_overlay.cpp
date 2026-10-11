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

#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/feature/geojson.h"
#include "fvkit/feature/vector_source.h"
#include "fvkit/log.h"
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

/// A MapLibre engine loaded from style JSON, labels on.
Status LoadEngine(const std::string& json, const std::string& font, MapLibreStyleEngine* engine) {
  Status s = engine->LoadText(json);
  if (!s.ok()) return s;
  engine->SetLabelFont(font);
  engine->SetDrawLabels(true);
  return Status::Ok();
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
  if (!layer_->Query(box, &features).ok()) return;
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
  Status s = WriteFeatureStyleJson(style_, &json, selection_);
  if (!s.ok()) return s;
  if (json != engine_json_ || label_font_ != engine_font_) {
    s = LoadEngine(json, label_font_, engine_.get());
    if (!s.ok()) return s;
    engine_json_ = std::move(json);
    engine_font_ = label_font_;
  }
  if (!source_) {
    source_ = std::make_shared<FeatureVectorSource>(layer_);
    renderer_ = std::make_unique<VectorRenderer>(source_, engine_);
    renderer_->SetPickEnabled(false);
  }
  renderer_->SetDeviceDpi(96.0 * display_scale_);
  // Features change under the overlay without the style knowing.
  renderer_->InvalidateScene();
  s = renderer_->Render(proj, &canvas);
  features_drawn_ = source_->features_returned();
  return s;
}

void FeatureOverlay::SetLayer(std::shared_ptr<IEditableFeatureLayer> layer) {
  layer_ = std::move(layer);
  source_.reset();
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
  item.hint.tool_tip = FeatureTitle(f);
  item.hint.status = Name() + ": " + item.hint.tool_tip;
  out.push_back(std::move(item));
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

void FeatureOverlay::Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
                            std::vector<app::SearchResult>& out) {
  if (!layer_) return;
  std::vector<Feature> features;
  const Status s = q.area ? layer_->Query(*q.area, &features) : layer_->All(&features);
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
  const std::string ext = LowerExtension(spec);
  if (ext == kGeoJsonExtension || ext == "json") {
    std::shared_ptr<MemoryFeatureLayer> layer;
    std::string style_json;
    Status s = ReadGeoJsonFile(spec, &layer, &style_json);
    if (!s.ok()) return s;
    SetLayer(std::move(layer));
    UseStoredStyle(spec, style_json);
    return Status::Ok();
  }
  std::shared_ptr<GeoPackage> package;
  Status s = GeoPackage::Open(spec, true, &package);
  if (!s.ok()) s = GeoPackage::Open(spec, false, &package);
  if (!s.ok()) return s;
  std::vector<GeoPackageLayerInfo> infos;
  s = package->Layers(&infos);
  if (!s.ok()) return s;
  if (infos.empty()) return Status::Error(kNotFound, spec + " has no feature tables");
  // One overlay shows one layer; a file with several opens its first.
  std::shared_ptr<GeoPackageFeatureLayer> layer;
  s = package->OpenLayer(infos.front().table, &layer);
  if (!s.ok()) return s;
  std::string style_json;
  s = package->ReadLayerStyle(infos.front().table, &style_json);
  if (!s.ok() && s.code != kNotFound) FV_LOG_WARNING(spec << ": layer style: " << s.message);
  SetLayer(layer);
  package_ = std::move(package);
  UseStoredStyle(spec, style_json);
  return Status::Ok();
}

void FeatureOverlay::UseStoredStyle(const std::string& spec, const std::string& style_json) {
  style_ = FeatureStyle{};
  if (style_json.empty()) return;
  FeatureStyle style;
  const Status s = ReadFeatureStyleJson(style_json, &style);
  if (!s.ok()) {
    FV_LOG_WARNING(spec << ": " << s.message << "; drawn with the default style");
    return;
  }
  style_ = std::move(style);
}

Status FeatureOverlay::FileOpen(const std::string& spec) { return Load(spec); }

Status FeatureOverlay::FileSaveAs(const std::string& spec, int format_index) {
  if (!layer_) return Status::Error(kInvalidArg, "no feature layer to save");
  std::string style_json;
  Status st = WriteFeatureStyleJson(style_, &style_json);
  if (!st.ok()) return st;
  if (format_index == kFormatGeoJson) {
    Status s = WriteGeoJsonFile(*layer_, spec, style_json);
    if (!s.ok()) return s;
    return Load(spec);
  }
  if (format_index != kFormatGeoPackage)
    return Status::Error(kUnsupported, "feature layers save as GeoPackage (0) or GeoJSON (1), asked for " +
                                           std::to_string(format_index));
  std::error_code ec;
  if (package_ && std::filesystem::equivalent(package_->Path(), spec, ec)) {
    const auto* table = dynamic_cast<const GeoPackageFeatureLayer*>(layer_.get());
    if (table != nullptr) {
      st = package_->WriteLayerStyle(table->Table(), style_json);
      if (!st.ok()) return st;
    }
    return package_->Save();
  }

  // Written beside the target and moved over it, so a failed write leaves the old file.
  const std::string temp = spec + ".saving";
  std::filesystem::remove(temp, ec);
  {
    std::shared_ptr<GeoPackage> out;
    Status s = GeoPackage::Create(temp, &out);
    if (!s.ok()) return s;
    std::string table = std::filesystem::path(spec).stem().string();
    if (table.empty()) table = "features";
    s = out->WriteLayer(*layer_, table, nullptr);
    if (s.ok()) s = out->WriteLayerStyle(table, style_json);
    if (s.ok()) s = out->Save();
    if (!s.ok()) {
      out.reset();
      std::filesystem::remove(temp, ec);
      return s;
    }
  }
  std::filesystem::rename(temp, spec, ec);
  if (ec) return Status::Error(kIoError, "cannot replace " + spec + ": " + ec.message());
  return Load(spec);
}

Status FeatureOverlay::Revert(const std::string& spec) {
  if (package_) {
    Status s = package_->Revert();
    if (!s.ok()) return s;
  }
  Status s = Load(spec);
  if (!s.ok()) return s;
  set_dirty(false);
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
  MapLibreStyleEngine engine;
  std::string json;
  Status s = WriteFeatureStyleJson(style, &json);
  if (!s.ok()) return s;
  s = LoadEngine(json, std::string(), &engine);
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
      const double m = 3 + symbol.line_width_px * scale / 2 + symbol.casing_px * scale;
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

}  // namespace fv
