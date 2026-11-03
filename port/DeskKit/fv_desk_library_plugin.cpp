// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_library_plugin.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "fv_desk_desk.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/properties.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/draw_interrupt.h"
#include "fvkit/log.h"
#include "fvkit/scale_table.h"
#include "fvkit/symbol/builtin.h"

namespace fv {
namespace desk {

using json = nlohmann::json;

namespace {

// MARK: Errors

/// The message the current plugin call attached with host->set_error.
thread_local std::string t_plugin_error;

void HostSetError(const char* message) { t_plugin_error = message != nullptr ? message : ""; }

void HostLog(int level, const char* message) {
  const char* m = message != nullptr ? message : "";
  switch (level) {
    case FV_LEVEL_ERROR: FV_LOG_ERROR("plugin: " << m); break;
    case FV_LEVEL_WARNING: FV_LOG_WARNING("plugin: " << m); break;
    case FV_LEVEL_DEBUG: FV_LOG_DEBUG("plugin: " << m); break;
    default: FV_LOG_INFO("plugin: " << m); break;
  }
}

/// Clears the plugin's last error; call before every plugin function that
/// returns a status.
void BeginCall() { t_plugin_error.clear(); }

/// A plugin's return code as a Status, with the message it set.
Status FromPlugin(int rc, const std::string& what) {
  if (rc == FV_OK) return Status::Ok();
  const bool known = (rc <= kOk && rc >= kNotProjectable) || rc == kInternal;
  const std::string why = t_plugin_error.empty() ? "error " + std::to_string(rc) : t_plugin_error;
  return Status::Error(known ? rc : kInternal, what + ": " + why);
}

// MARK: The view

struct ViewHost {
  const MapProjection* proj;
};

int ViewGeoToScreen(const fv_view* v, fv_geo_point p, double* x, double* y) {
  const auto* h = static_cast<const ViewHost*>(v->host);
  return h->proj->GeoToSurface(GeoPoint{p.lat, p.lon}, x, y).code;
}

int ViewScreenToGeo(const fv_view* v, double x, double y, fv_geo_point* out) {
  const auto* h = static_cast<const ViewHost*>(v->host);
  GeoPoint g;
  const Status s = h->proj->SurfaceToGeo(x, y, &g);
  if (s.ok()) *out = fv_geo_point{g.lat, g.lon};
  return s.code;
}

/// Fills `v` for `proj`; `host` must outlive every use of `v`.
void MakeView(const MapProjection& proj, ViewHost* host, fv_view* v) {
  host->proj = &proj;
  *v = fv_view{};
  v->struct_size = sizeof(fv_view);
  const PixelSize size = proj.SurfaceSize();
  v->width = size.width;
  v->height = size.height;
  const GeoRect b = proj.VmapBounds();
  v->south_west = fv_geo_point{b.ll.lat, b.ll.lon};
  v->north_east = fv_geo_point{b.ur.lat, b.ur.lon};
  v->scale_denominator = ScaleDenominatorFor(proj);
  v->projection = static_cast<int>(proj.Type());
  v->rotation_deg = proj.Rotation();
  // Overlay::OnDraw is not told the display scale; built-in overlays draw at 1 too.
  v->dpi_scale = 1.0;
  v->host = host;
  v->geo_to_screen = ViewGeoToScreen;
  v->screen_to_geo = ViewScreenToGeo;
}

// MARK: Drawing

FvColor ToColor(fv_color c) { return FvColor{c.r, c.g, c.b, c.a}; }

struct DrawHost {
  const MapProjection* proj;
  ICanvas* canvas;
  GeoDraw* geo;
  BuiltinSymbolLibrary* symbols;
};

const DrawHost& Host(const fv_draw_api* d) { return *static_cast<const DrawHost*>(d->host); }

GeoLineStyle ToLineStyle(const fv_line_style* s) {
  if (s == nullptr) return SolidGeoLine(FvColor{0, 0, 0, 255}, 1);
  const int width = std::max(1, static_cast<int>(std::lround(s->width_px)));
  const char* preset =
      FV_HAS_FIELD(s, fv_line_style, preset) && s->preset != nullptr ? s->preset : line_preset::kSolid;
  GeoLineStyle style = PresetGeoLine(preset, ToColor(s->color), width);
  if (FV_HAS_FIELD(s, fv_line_style, casing_px) && s->casing_px > 0)
    AddCasing(&style, ToColor(s->casing_color), static_cast<int>(std::lround(s->casing_px)));
  return style;
}

LineKind ToLineKind(int kind) {
  switch (kind) {
    case FV_LINE_RHUMB: return LineKind::kRhumb;
    case FV_LINE_GREAT: return LineKind::kGreatCircle;
    default: return LineKind::kSimple;
  }
}

std::vector<GeoPoint> ToPoints(const fv_geo_point* points, size_t count) {
  std::vector<GeoPoint> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) out.push_back(GeoPoint{points[i].lat, points[i].lon});
  return out;
}

int DrawPolyline(const fv_draw_api* d, const fv_geo_point* points, size_t count, int closed,
                 int line_kind, const fv_line_style* style) {
  if (points == nullptr || count < 2) return FV_ERR_INVALID_ARG;
  return Host(d).geo->DrawGeoPolyline(ToPoints(points, count), ToLineKind(line_kind),
                                      ToLineStyle(style), closed != 0).code;
}

int DrawPolygon(const fv_draw_api* d, const fv_geo_point* points, size_t count,
                const fv_fill_style* fill, const fv_line_style* outline) {
  if (points == nullptr || count < 3) return FV_ERR_INVALID_ARG;
  const DrawHost& h = Host(d);
  if (fill != nullptr) {
    std::vector<PixelPoint> ring;
    ring.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      double x = 0, y = 0;
      if (!h.proj->GeoToSurfaceUnwrapped(GeoPoint{points[i].lat, points[i].lon}, &x, &y).ok())
        continue;
      ring.push_back(PixelPoint{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))});
    }
    if (ring.size() >= 3) {
      Brush brush{ToColor(fill->color)};
      const Status s = h.canvas->DrawPolyPolygon({ring}, &brush, nullptr);
      if (!s.ok()) return s.code;
    }
  }
  if (outline == nullptr) return FV_OK;
  return h.geo->DrawGeoPolyline(ToPoints(points, count), LineKind::kSimple, ToLineStyle(outline),
                                /*closed=*/true).code;
}

int DrawCircle(const fv_draw_api* d, fv_geo_point c, double radius_m, const fv_line_style* style) {
  return Host(d).geo->DrawGeoCircle(GeoPoint{c.lat, c.lon}, radius_m, ToLineStyle(style)).code;
}

int DrawEllipse(const fv_draw_api* d, fv_geo_point c, double vert_m, double horz_m,
                double rotation_deg, const fv_line_style* style) {
  return Host(d).geo->DrawGeoEllipse(GeoPoint{c.lat, c.lon}, vert_m, horz_m, rotation_deg,
                                     ToLineStyle(style)).code;
}

int DrawArc(const fv_draw_api* d, fv_geo_point c, double radius_m, double start_deg,
            double sweep_deg, const fv_line_style* style) {
  return Host(d).geo->DrawGeoArc(GeoPoint{c.lat, c.lon}, radius_m, start_deg, sweep_deg,
                                 ToLineStyle(style)).code;
}

/// Colours the built-in shapes for this stamp and returns the style to stamp with.
PointSymbolStyle PrepareSymbol(const DrawHost& h, const char* id, const fv_symbol_style* s) {
  PointSymbolStyle style;
  style.valid = true;
  style.symbol_id = id != nullptr ? id : "";
  FvColor color{0, 0, 0, 255};
  if (s != nullptr) {
    color = ToColor(s->color);
    if (FV_HAS_FIELD(s, fv_symbol_style, rotation_deg)) style.rotation_deg = s->rotation_deg;
    if (FV_HAS_FIELD(s, fv_symbol_style, scale) && s->scale > 0) style.scale = s->scale;
  }
  if (h.symbols->color().r != color.r || h.symbols->color().g != color.g ||
      h.symbols->color().b != color.b || h.symbols->color().a != color.a)
    h.symbols->SetColor(color);
  return style;
}

int DrawSymbol(const fv_draw_api* d, fv_geo_point at, const char* id, const fv_symbol_style* s) {
  const DrawHost& h = Host(d);
  const PointSymbolStyle style = PrepareSymbol(h, id, s);
  return h.geo->DrawSymbol(GeoPoint{at.lat, at.lon}, style.symbol_id, style).code;
}

int DrawSymbolPx(const fv_draw_api* d, double x, double y, const char* id,
                 const fv_symbol_style* s) {
  const DrawHost& h = Host(d);
  const PointSymbolStyle style = PrepareSymbol(h, id, s);
  return h.geo->DrawSymbolAtPixel(x, y, style.symbol_id, style).code;
}

LabelStyle ToLabelStyle(const char* text, const fv_label_style* s) {
  LabelStyle style;
  style.valid = true;
  style.text = text != nullptr ? text : "";
  style.style.color = FvColor{0, 0, 0, 255};
  if (s == nullptr) return style;
  style.style.color = ToColor(s->color);
  if (s->size_px > 0) style.style.size = s->size_px;
  if (FV_HAS_FIELD(s, fv_label_style, halo_px) && s->halo_px > 0) {
    style.halo_color = ToColor(s->halo_color);
    style.halo_width = s->halo_px;
  }
  if (FV_HAS_FIELD(s, fv_label_style, valign)) {
    if (s->halign >= FV_HALIGN_LEFT && s->halign <= FV_HALIGN_RIGHT)
      style.halign = static_cast<LabelHAlign>(s->halign);
    if (s->valign >= FV_VALIGN_BASELINE && s->valign <= FV_VALIGN_TOP)
      style.valign = static_cast<LabelVAlign>(s->valign);
  }
  if (FV_HAS_FIELD(s, fv_label_style, dy)) {
    style.dx = s->dx;
    style.dy = s->dy;
  }
  return style;
}

int DrawLabel(const fv_draw_api* d, fv_geo_point at, const char* text, const fv_label_style* s) {
  const LabelStyle style = ToLabelStyle(text, s);
  return Host(d).geo->DrawLabel(GeoPoint{at.lat, at.lon}, style.text, style).code;
}

int DrawLabelPx(const fv_draw_api* d, double x, double y, const char* text,
                const fv_label_style* s) {
  const LabelStyle style = ToLabelStyle(text, s);
  return Host(d).geo->DrawLabelAtPixel(x, y, style.text, style).code;
}

int DrawCancelled(const fv_draw_api*) { return DrawInterrupted() ? 1 : 0; }

// MARK: Properties schema

bool ParsePropertyType(const std::string& name, app::PropertyType* out) {
  static const std::map<std::string, app::PropertyType> kTypes = {
      {"bool", app::PropertyType::kBool},     {"int", app::PropertyType::kInt},
      {"double", app::PropertyType::kDouble}, {"string", app::PropertyType::kString},
      {"color", app::PropertyType::kColor},   {"choice", app::PropertyType::kChoice},
      {"path", app::PropertyType::kPath}};
  auto it = kTypes.find(name);
  if (it == kTypes.end()) return false;
  *out = it->second;
  return true;
}

/// A JSON default as the text PropertyValue::FromString reads.
std::string DefaultText(const json& j) {
  if (j.is_string()) return j.get<std::string>();
  if (j.is_boolean()) return j.get<bool>() ? "true" : "false";
  if (j.is_number()) return j.dump();
  return std::string();
}

/// Parses a `properties_schema` document. Entries that do not parse are
/// skipped and logged.
std::vector<app::PropertySpec> ParseSchema(const char* text, const std::string& source) {
  std::vector<app::PropertySpec> specs;
  if (text == nullptr) return specs;
  const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (!doc.is_array()) {
    FV_LOG_WARNING("plugin: " << source << ": properties_schema is not a JSON array");
    return specs;
  }
  for (const json& e : doc) {
    app::PropertySpec spec;
    if (!e.is_object() || !e.contains("key") || !e["key"].is_string() || !e.contains("type") ||
        !e["type"].is_string() || !ParsePropertyType(e["type"].get<std::string>(), &spec.type)) {
      FV_LOG_WARNING("plugin: " << source << ": skipped a property without a key and known type");
      continue;
    }
    spec.key = e["key"].get<std::string>();
    spec.label = e.value("label", spec.key);
    spec.group = e.value("group", std::string());
    spec.help = e.value("help", std::string());
    spec.min = e.value("min", 0.0);
    spec.max = e.value("max", 0.0);
    if (e.contains("choices") && e["choices"].is_array())
      for (const json& c : e["choices"])
        if (c.is_string()) spec.choices.push_back(c.get<std::string>());
    spec.default_value.type = spec.type;
    if (e.contains("default") && !spec.default_value.FromString(DefaultText(e["default"])))
      FV_LOG_WARNING("plugin: " << source << ": property '" << spec.key << "' has a bad default");
    specs.push_back(std::move(spec));
  }
  return specs;
}

// MARK: The overlay adapter

/// True when the overlay class is large enough to hold `field` and supplies it.
#define CLASS_HAS(cls, field) (FV_HAS_FIELD(&(cls), fv_overlay_class, field) && (cls).field != nullptr)

/// An fv::Overlay backed by a plugin's overlay class.
class LibraryOverlay : public Overlay,
                       public app::Properties,
                       public app::Persistence,
                       public app::HitTest {
 public:
  LibraryOverlay(const PluginLibrary& lib, const fv_overlay_class& cls, void* self,
                 const std::string& display_name)
      : Overlay(display_name), lib_(lib), cls_(cls), self_(self) {
    if (CLASS_HAS(cls_, properties_schema) && CLASS_HAS(cls_, get_property) &&
        CLASS_HAS(cls_, set_property)) {
      has_properties_ = true;
      specs_ = ParseSchema(cls_.properties_schema(self_), lib_.path);
    }
  }
  ~LibraryOverlay() override { cls_.destroy(self_); }

  app::Properties* AsProperties() override { return has_properties_ ? this : nullptr; }
  app::Persistence* AsPersistence() override {
    return CLASS_HAS(cls_, open) ? this : nullptr;
  }
  app::HitTest* AsHitTest() override { return CLASS_HAS(cls_, hit_test) ? this : nullptr; }

  Status OnDraw(const MapProjection& proj, ICanvas& canvas) override {
    {
      std::lock_guard<std::mutex> lock(last_proj_mutex_);
      last_proj_ = proj;
    }
    ViewHost vh{};
    fv_view view;
    MakeView(proj, &vh, &view);
    BuiltinSymbolLibrary symbols;
    GeoDraw geo(proj, &canvas, &symbols);
    DrawHost dh{&proj, &canvas, &geo, &symbols};
    fv_draw_api api{};
    api.struct_size = sizeof(fv_draw_api);
    api.host = &dh;
    api.polyline = DrawPolyline;
    api.polygon = DrawPolygon;
    api.circle = DrawCircle;
    api.ellipse = DrawEllipse;
    api.arc = DrawArc;
    api.symbol = DrawSymbol;
    api.symbol_px = DrawSymbolPx;
    api.label = DrawLabel;
    api.label_px = DrawLabelPx;
    api.cancelled = DrawCancelled;
    BeginCall();
    return FromPlugin(cls_.draw(self_, &view, &api), Name() + " draw");
  }

  bool OnMouseMove(const MouseEvent& e) override { return Mouse(FV_MOUSE_MOVE, e, 0); }
  bool OnMouseDown(const MouseEvent& e) override { return Mouse(FV_MOUSE_DOWN, e, 0); }
  bool OnMouseUp(const MouseEvent& e) override { return Mouse(FV_MOUSE_UP, e, 0); }
  bool OnDoubleClick(const MouseEvent& e) override { return Mouse(FV_MOUSE_DOUBLE_CLICK, e, 0); }
  bool OnMouseWheel(const MouseEvent& e, double delta) override {
    return Mouse(FV_MOUSE_WHEEL, e, delta);
  }

  // app::Properties
  const std::vector<app::PropertySpec>& Describe() const override { return specs_; }

  Status GetProperty(const std::string& key, app::PropertyValue* out) const override {
    const app::PropertySpec* spec = FindSpec(key);
    if (spec == nullptr) return Status::Error(kNotFound, "no property " + key);
    const char* text = cls_.get_property(self_, key.c_str());
    if (text == nullptr) return Status::Error(kNotFound, "no property " + key);
    app::PropertyValue v;
    v.type = spec->type;
    if (!v.FromString(text))
      return Status::Error(kInternal, "property " + key + ": plugin answered '" + text + "'");
    *out = std::move(v);
    return Status::Ok();
  }

  Status SetProperty(const std::string& key, const app::PropertyValue& value) override {
    const app::PropertySpec* spec = FindSpec(key);
    if (spec == nullptr) return Status::Error(kNotFound, "no property " + key);
    if (value.type != spec->type) return Status::Error(kInvalidArg, "property " + key + ": wrong type");
    BeginCall();
    const Status s = FromPlugin(cls_.set_property(self_, key.c_str(), value.ToString().c_str()),
                                "property " + key);
    if (s.ok() && AsPersistence() != nullptr) set_dirty(true);
    return s;
  }

  // app::Persistence
  Status FileNew() override { return Status::Ok(); }

  Status FileOpen(const std::string& spec) override {
    BeginCall();
    const Status s = FromPlugin(cls_.open(self_, spec.c_str()), "open " + spec);
    if (!s.ok()) return s;
    set_file_spec(spec);
    set_has_been_saved(true);
    set_dirty(false);
    return s;
  }

  Status FileSaveAs(const std::string& spec, int format_index) override {
    (void)format_index;
    if (!CLASS_HAS(cls_, save)) return Status::Error(kUnsupported, Name() + " cannot be saved");
    BeginCall();
    const Status s = FromPlugin(cls_.save(self_, spec.c_str()), "save " + spec);
    if (!s.ok()) return s;
    set_file_spec(spec);
    set_has_been_saved(true);
    set_dirty(false);
    return s;
  }

  // app::HitTest
  void HitTestPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                    std::vector<app::HitItem>& out) override {
    ViewHost vh{};
    fv_view view;
    MakeView(proj, &vh, &view);
    fv_hit hit{};
    hit.struct_size = sizeof(fv_hit);
    if (cls_.hit_test(self_, &view, p.x, p.y, tolerance_px, &hit) != 1) return;
    app::HitItem item;
    item.overlay = this;
    item.feature = hit.feature;
    item.distance_px = hit.distance_px;
    if (hit.tooltip != nullptr) item.hint.tool_tip = hit.tooltip;
    if (hit.status != nullptr) item.hint.status = hit.status;
    out.push_back(std::move(item));
  }

 private:
  /// Passes a mouse event with the last frame's view. A handled press marks
  /// a document dirty, since API 1 has no other way to report an edit.
  bool Mouse(int kind, const MouseEvent& e, double wheel) {
    if (!CLASS_HAS(cls_, on_mouse)) return false;
    std::optional<MapProjection> proj;
    {
      std::lock_guard<std::mutex> lock(last_proj_mutex_);
      proj = last_proj_;
    }
    ViewHost vh{};
    fv_view view;
    if (proj) MakeView(*proj, &vh, &view);
    fv_mouse_event ev{};
    ev.struct_size = sizeof(fv_mouse_event);
    ev.kind = kind;
    ev.x = e.x;
    ev.y = e.y;
    ev.button = e.button;
    ev.shift = e.shift ? 1 : 0;
    ev.ctrl = e.ctrl ? 1 : 0;
    ev.clicks = e.clicks;
    ev.wheel_delta = wheel;
    const bool handled = cls_.on_mouse(self_, proj ? &view : nullptr, &ev) != 0;
    if (handled && kind == FV_MOUSE_DOWN && AsPersistence() != nullptr) set_dirty(true);
    return handled;
  }

  const PluginLibrary& lib_;
  const fv_overlay_class& cls_;
  void* self_;
  bool has_properties_ = false;
  std::vector<app::PropertySpec> specs_;
  std::mutex last_proj_mutex_;
  std::optional<MapProjection> last_proj_;
};

// MARK: Tools

struct ToolHost {
  ToolContext* context;
};

int ToolOpenFile(const fv_tool_context* c, const char* path) {
  if (path == nullptr) return FV_ERR_INVALID_ARG;
  const Status s = static_cast<const ToolHost*>(c->ctx)->context->OpenFile(path);
  if (s.code == kUnsupported) t_plugin_error = s.message;
  if (s.ok()) return FV_OK;
  return s.code == kUnsupported || s.code == kInterrupted ? s.code : FV_ERR_IO;
}

void ToolRequestRedraw(const fv_tool_context* c) {
  static_cast<const ToolHost*>(c->ctx)->context->RequestRedraw();
}

// MARK: Loading

struct LoadedTable {
  std::mutex mutex;
  std::map<std::string, std::unique_ptr<PluginLibrary>> loaded;  ///< path → library
  std::map<std::string, Status> failed;                          ///< path → why
};

LoadedTable& Loaded() {
  static LoadedTable* t = new LoadedTable;  // never destroyed: plugins outlive static teardown
  return *t;
}

/// Opens `path` and returns its entry point, or null with `why` set.
fv_plugin_entry_fn OpenLibrary(const std::string& path, std::string* why) {
#if defined(_WIN32)
  const std::wstring wide = std::filesystem::path(path).wstring();
  HMODULE h = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (h == nullptr) {
    *why = "cannot load " + path + " (error " + std::to_string(GetLastError()) + ")";
    return nullptr;
  }
  auto entry = reinterpret_cast<fv_plugin_entry_fn>(GetProcAddress(h, FV_PLUGIN_ENTRY_SYMBOL));
#else
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (h == nullptr) {
    const char* e = dlerror();
    *why = e != nullptr ? e : "cannot load " + path;
    return nullptr;
  }
  auto entry = reinterpret_cast<fv_plugin_entry_fn>(dlsym(h, FV_PLUGIN_ENTRY_SYMBOL));
#endif
  if (entry == nullptr) *why = path + " has no " FV_PLUGIN_ENTRY_SYMBOL " function";
  return entry;
}

/// Checks the tables a plugin returned. Empty when they are usable.
std::string ValidateApi(const fv_plugin_api& api) {
  if (api.overlay_count > 0 && api.overlays == nullptr) return "overlay_count without overlays";
  if (api.tool_count > 0 && api.tools == nullptr) return "tool_count without tools";
  for (size_t i = 0; i < api.overlay_count; ++i) {
    const fv_overlay_class& c = api.overlays[i];
    if (c.struct_size < offsetof(fv_overlay_class, draw) + sizeof(c.draw))
      return "overlay class " + std::to_string(i) + " is too small";
    if (c.name == nullptr || c.create == nullptr || c.destroy == nullptr || c.draw == nullptr)
      return "overlay class " + std::to_string(i) + " lacks a name, create, destroy or draw";
  }
  for (size_t i = 0; i < api.tool_count; ++i) {
    const fv_tool_class& c = api.tools[i];
    if (c.struct_size < offsetof(fv_tool_class, run) + sizeof(c.run) || c.name == nullptr ||
        c.run == nullptr)
      return "tool class " + std::to_string(i) + " lacks a name or run";
  }
  return std::string();
}

}  // namespace

const fv_overlay_class* PluginLibrary::FindOverlay(const std::string& name) const {
  for (size_t i = 0; i < api.overlay_count; ++i)
    if (name == api.overlays[i].name) return &api.overlays[i];
  return nullptr;
}

const fv_tool_class* PluginLibrary::FindTool(const std::string& name) const {
  for (size_t i = 0; i < api.tool_count; ++i)
    if (name == api.tools[i].name) return &api.tools[i];
  return nullptr;
}

std::vector<std::string> PluginPlatformKeys() {
#if defined(__APPLE__) && defined(__aarch64__)
  return {"macos-arm64", "macos"};
#elif defined(__APPLE__)
  return {"macos-x86_64", "macos"};
#elif defined(__linux__) && defined(__aarch64__)
  return {"linux-aarch64"};
#elif defined(__linux__)
  return {"linux-x86_64"};
#elif defined(_WIN32)
  return {"windows-x86_64"};
#else
  return {};
#endif
}

std::string SelectPluginLibrary(const std::map<std::string, std::string>& libraries) {
  for (const std::string& key : PluginPlatformKeys()) {
    auto it = libraries.find(key);
    if (it != libraries.end()) return it->second;
  }
  return std::string();
}

Status LoadPluginLibrary(const std::string& path, const std::string& package_dir,
                         const std::string& host_version, const PluginLibrary** out) {
  LoadedTable& t = Loaded();
  std::lock_guard<std::mutex> lock(t.mutex);
  if (auto it = t.loaded.find(path); it != t.loaded.end()) {
    *out = it->second.get();
    return Status::Ok();
  }
  if (auto it = t.failed.find(path); it != t.failed.end()) return it->second;
  auto fail = [&t, &path](int code, const std::string& why) {
    const Status s = Status::Error(code, why);
    t.failed[path] = s;
    FV_LOG_WARNING("plugin: " << why);
    return s;
  };

  std::string why;
  const fv_plugin_entry_fn entry = OpenLibrary(path, &why);
  if (entry == nullptr) return fail(kIoError, why);

  auto lib = std::make_unique<PluginLibrary>();
  lib->path = path;
  lib->package_dir = package_dir;
  lib->host_version = host_version;
  lib->host.struct_size = sizeof(fv_host_api);
  lib->host.api_major = FV_PLUGIN_API_MAJOR;
  lib->host.api_minor = FV_PLUGIN_API_MINOR;
  lib->host.host_version = lib->host_version.c_str();
  lib->host.package_dir = lib->package_dir.c_str();
  lib->host.log = HostLog;
  lib->host.set_error = HostSetError;
  lib->api.struct_size = sizeof(fv_plugin_api);

  BeginCall();
  const int rc = entry(&lib->host, &lib->api);
  if (rc != FV_OK) {
    const Status s = FromPlugin(rc, path + " refused to load");
    return fail(s.code, s.message);
  }
  if (lib->api.api_major != FV_PLUGIN_API_MAJOR)
    return fail(kUnsupported, "built for plugin API " + std::to_string(lib->api.api_major) +
                                  "; this Peregrine hosts API " +
                                  std::to_string(FV_PLUGIN_API_MAJOR));
  const std::string bad = ValidateApi(lib->api);
  if (!bad.empty()) return fail(kInvalidArg, path + ": " + bad);

  FV_LOG_INFO("plugin: loaded " << path << " (API " << lib->api.api_major << "."
                                << lib->api.api_minor << ")");
  *out = lib.get();
  t.loaded.emplace(path, std::move(lib));
  return Status::Ok();
}

std::shared_ptr<Overlay> CreateLibraryOverlay(const PluginLibrary& lib,
                                              const fv_overlay_class& cls,
                                              const std::string& type_id,
                                              const std::string& display_name) {
  BeginCall();
  void* self = cls.create(&lib.host, type_id.c_str());
  if (self == nullptr) {
    FV_LOG_WARNING("plugin: " << type_id << ": create failed"
                              << (t_plugin_error.empty() ? "" : ": " + t_plugin_error));
    return nullptr;
  }
  return std::make_shared<LibraryOverlay>(lib, cls, self, display_name);
}

ToolRun MakeLibraryTool(const PluginLibrary& lib, const fv_tool_class& cls) {
  const PluginLibrary* l = &lib;
  const fv_tool_class* c = &cls;
  return [l, c](ToolContext& context) {
    ToolHost host{&context};
    fv_tool_context tc{};
    tc.struct_size = sizeof(fv_tool_context);
    tc.host = &l->host;
    tc.ctx = &host;
    tc.open_file = ToolOpenFile;
    tc.request_redraw = ToolRequestRedraw;
    BeginCall();
    return FromPlugin(c->run(&tc), c->name);
  };
}

}  // namespace desk
}  // namespace fv
