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
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "fv_desk_desk.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/overlay_host.h"
#include "fvkit/app/properties.h"
#include "fvkit/app/search.h"
#include "fvkit/canvas/geo_draw.h"
#include "fvkit/draw_interrupt.h"
#include "fvkit/log.h"
#include "fvkit/scale_table.h"
#include "fvkit/symbol/builtin.h"

/// The host's handle on one plugin overlay. Owned by its adapter, and by any
/// menu action that outlives a call; `overlay` is null once the adapter is
/// gone. The mutex orders a plugin thread's request_redraw against teardown.
struct fv_overlay_ref {
  std::mutex mutex;
  fv::Overlay* overlay = nullptr;
  const fv_overlay_class* cls = nullptr;
  void* self = nullptr;
};

/// The host's handle on one plugin editor; `editor` is null once it is gone.
struct fv_editor_ref {
  fv::app::OverlayEditor* editor = nullptr;
  const fv::desk::PluginLibrary* lib = nullptr;
  const fv_editor_class* cls = nullptr;
  void* self = nullptr;
};

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

// MARK: Builders

/// What an fv_menu fills: one node, and the action an item id becomes.
/// A submenu is named by its parent and index, not a pointer, because the
/// parent's children may reallocate while the plugin still fills it.
struct MenuHost {
  app::MenuNode* root;     ///< the top menu; null for a submenu
  const MenuHost* parent;  ///< the enclosing menu of a submenu
  size_t index;            ///< the submenu's place in its parent's children
  std::function<std::function<void()>(const std::string& id)> action;
  std::deque<MenuHost>* hosts;  ///< storage for submenus, alive for the call
  std::deque<fv_menu>* menus;
};

const MenuHost& Host(const fv_menu* m) { return *static_cast<const MenuHost*>(m->host); }

app::MenuNode& Node(const MenuHost& h) {
  return h.root != nullptr ? *h.root : Node(*h.parent).children[h.index];
}

void MenuItem(const fv_menu* m, const char* id, const char* label, const char* icon, int flags) {
  if (id == nullptr || label == nullptr || *label == '\0') return;
  const MenuHost& h = Host(m);
  app::MenuNode item;
  item.label = label;
  item.icon = icon != nullptr ? icon : "";
  item.enabled = (flags & FV_ITEM_DISABLED) == 0;
  item.checked = (flags & FV_ITEM_CHECKED) != 0;
  item.action = h.action(id);
  Node(h).children.push_back(std::move(item));
}

void MenuSeparator(const fv_menu* m) { Node(Host(m)).children.push_back(app::MenuNode()); }

fv_menu MakeMenu(const MenuHost* host) {
  fv_menu m{};
  m.struct_size = sizeof(fv_menu);
  m.host = host;
  m.item = MenuItem;
  m.separator = MenuSeparator;
  m.submenu = [](const fv_menu* parent, const char* label) -> const fv_menu* {
    const MenuHost& h = Host(parent);
    app::MenuNode sub;
    sub.label = label != nullptr && *label != '\0' ? label : "\xE2\x80\xA6";
    app::MenuNode& parent_node = Node(h);
    parent_node.children.push_back(std::move(sub));
    h.hosts->push_back(MenuHost{nullptr, &h, parent_node.children.size() - 1, h.action, h.hosts, h.menus});
    h.menus->push_back(MakeMenu(&h.hosts->back()));
    return &h.menus->back();
  };
  return m;
}

/// Calls `fill` with a menu that appends to `node`; item ids become `action(id)`.
template <typename Fill>
void BuildMenu(app::MenuNode* node, std::function<std::function<void()>(const std::string&)> action,
               Fill fill) {
  std::deque<MenuHost> hosts;
  std::deque<fv_menu> menus;
  hosts.push_back(MenuHost{node, nullptr, 0, std::move(action), &hosts, &menus});
  menus.push_back(MakeMenu(&hosts.back()));
  fill(&menus.back());
}

const char* OrEmpty(const char* s) { return s != nullptr ? s : ""; }

app::ItemInfoSheet& Sheet(const fv_info* i) {
  return *static_cast<app::ItemInfoSheet*>(const_cast<void*>(i->host));
}

fv_info MakeInfo(app::ItemInfoSheet* sheet) {
  fv_info i{};
  i.struct_size = sizeof(fv_info);
  i.host = sheet;
  i.title = [](const fv_info* i, const char* t) { Sheet(i).title = OrEmpty(t); };
  i.row = [](const fv_info* i, const char* section, const char* key, const char* value) {
    Sheet(i).rows.push_back(app::InfoRow{OrEmpty(section), OrEmpty(key), OrEmpty(value)});
  };
  i.edit_label = [](const fv_info* i, const char* l) { Sheet(i).edit_label = OrEmpty(l); };
  return i;
}

struct SnapHost {
  std::vector<app::SnapToItem>* out;
  Overlay* overlay;
};

struct SearchHost {
  const app::SearchQuery* query;
  const std::atomic<bool>* cancel;
  std::vector<app::SearchResult>* out;
  size_t added = 0;
};

SearchHost& Host(const fv_search* q) { return *static_cast<SearchHost*>(const_cast<void*>(q->host)); }

// MARK: The overlay adapter

/// True when the class is large enough to hold `field` and supplies it.
#define CLASS_HAS(cls, field) (FV_HAS_FIELD(&(cls), fv_overlay_class, field) && (cls).field != nullptr)
#define EDITOR_HAS(cls, field) (FV_HAS_FIELD(&(cls), fv_editor_class, field) && (cls).field != nullptr)

/// An fv::Overlay backed by a plugin's overlay class.
class LibraryOverlay : public Overlay,
                       public app::Properties,
                       public app::Persistence,
                       public app::HitTest,
                       public app::EditHint,
                       public app::SnapTo,
                       public app::ContextMenu,
                       public app::ItemInfo,
                       public app::SearchProvider,
                       public app::EditTarget,
                       public app::HostAware {
 public:
  LibraryOverlay(const PluginLibrary& lib, const fv_overlay_class& cls,
                 std::shared_ptr<fv_overlay_ref> ref, const std::string& display_name)
      : Overlay(display_name), lib_(lib), cls_(cls), ref_(std::move(ref)), self_(ref_->self) {
    {
      std::lock_guard<std::mutex> lock(ref_->mutex);
      ref_->overlay = this;
    }
    if (CLASS_HAS(cls_, properties_schema) && CLASS_HAS(cls_, get_property) &&
        CLASS_HAS(cls_, set_property)) {
      has_properties_ = true;
      specs_ = ParseSchema(cls_.properties_schema(self_), lib_.path);
    }
  }
  ~LibraryOverlay() override {
    // Detached first: a plugin worker that calls request_redraw while
    // `destroy` joins it finds nothing rather than a half-destroyed adapter.
    {
      std::lock_guard<std::mutex> lock(ref_->mutex);
      ref_->overlay = nullptr;
    }
    cls_.destroy(self_);
  }

  const PluginLibrary& library() const { return lib_; }
  void* self() const { return self_; }

  app::HostAware* AsHostAware() override { return this; }
  app::Properties* AsProperties() override { return has_properties_ ? this : nullptr; }
  app::Persistence* AsPersistence() override { return CLASS_HAS(cls_, open) ? this : nullptr; }
  app::HitTest* AsHitTest() override { return CLASS_HAS(cls_, hit_test) ? this : nullptr; }
  app::EditHint* AsEditHint() override { return CLASS_HAS(cls_, edit_hint) ? this : nullptr; }
  app::SnapTo* AsSnapTo() override { return CLASS_HAS(cls_, snap_to) ? this : nullptr; }
  app::ContextMenu* AsContextMenu() override {
    return CLASS_HAS(cls_, context_menu) && CLASS_HAS(cls_, menu_command) ? this : nullptr;
  }
  app::ItemInfo* AsItemInfo() override { return CLASS_HAS(cls_, item_info) ? this : nullptr; }
  app::SearchProvider* AsSearch() override { return CLASS_HAS(cls_, search) ? this : nullptr; }
  app::EditTarget* AsEditTarget() override {
    return CLASS_HAS(cls_, enter_edit_focus) || CLASS_HAS(cls_, release_edit_focus) ||
                   CLASS_HAS(cls_, undo) || CLASS_HAS(cls_, redo)
               ? this
               : nullptr;
  }

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

  bool OnKeyDown(const KeyEvent& e) override {
    if (!CLASS_HAS(cls_, on_key)) return false;
    fv_key_event ev{};
    ev.struct_size = sizeof(fv_key_event);
    ev.key = e.key;
    ev.text = e.text;
    ev.shift = e.shift ? 1 : 0;
    ev.ctrl = e.ctrl ? 1 : 0;
    ev.alt = e.alt ? 1 : 0;
    ev.meta = e.meta ? 1 : 0;
    return cls_.on_key(self_, &ev) != 0;
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
    return FromPlugin(cls_.set_property(self_, key.c_str(), value.ToString().c_str()),
                      "property " + key);
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
    if (!CLASS_HAS(cls_, save)) return Status::Error(kUnsupported, Name() + " cannot be saved");
    BeginCall();
    const Status s = FromPlugin(cls_.save(self_, spec.c_str(), format_index), "save " + spec);
    if (!s.ok()) return s;
    set_file_spec(spec);
    set_has_been_saved(true);
    set_dirty(false);
    return s;
  }

  bool SupportsRevert() const override { return CLASS_HAS(cls_, revert); }

  Status Revert(const std::string& spec) override {
    if (!CLASS_HAS(cls_, revert)) return app::Persistence::Revert(spec);
    BeginCall();
    const Status s = FromPlugin(cls_.revert(self_, spec.c_str()), "revert " + spec);
    if (s.ok()) set_dirty(false);
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

  // app::EditHint
  std::string EditHintAt(PixelPoint p) override { return OrEmpty(cls_.edit_hint(self_, p.x, p.y)); }

  // app::SnapTo
  void SnapToPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                   std::vector<app::SnapToItem>& out) override {
    ViewHost vh{};
    fv_view view;
    MakeView(proj, &vh, &view);
    SnapHost host{&out, this};
    fv_snap_list list{};
    list.struct_size = sizeof(fv_snap_list);
    list.host = &host;
    list.add = [](const fv_snap_list* l, fv_geo_point g, const char* description, double d) {
      const auto* h = static_cast<const SnapHost*>(l->host);
      h->out->push_back(app::SnapToItem{GeoPoint{g.lat, g.lon}, OrEmpty(description), h->overlay, d});
    };
    cls_.snap_to(self_, &view, p.x, p.y, tolerance_px, &list);
  }

  // app::ContextMenu
  void AppendMenuItems(const MapProjection& proj, PixelPoint p, app::MenuNode& menu) override {
    ViewHost vh{};
    fv_view view;
    MakeView(proj, &vh, &view);
    const std::shared_ptr<fv_overlay_ref> ref = ref_;
    BuildMenu(&menu, [ref](const std::string& id) -> std::function<void()> {
      return [ref, id] {
        // The menu may outlive the overlay; a closed one ignores the choice.
        if (ref->overlay == nullptr) return;
        BeginCall();
        const Status s = FromPlugin(ref->cls->menu_command(ref->self, id.c_str()), id);
        if (!s.ok()) FV_LOG_WARNING("plugin: " << s.message);
      };
    }, [&](const fv_menu* m) { cls_.context_menu(self_, &view, p.x, p.y, m); });
  }

  // app::ItemInfo
  std::optional<app::ItemInfoSheet> InfoAt(const MapProjection& proj, PixelPoint p,
                                           double tolerance_px) override {
    ViewHost vh{};
    fv_view view;
    MakeView(proj, &vh, &view);
    app::ItemInfoSheet sheet;
    const fv_info info = MakeInfo(&sheet);
    if (cls_.item_info(self_, &view, p.x, p.y, tolerance_px, &info) != 1) return std::nullopt;
    if (sheet.title.empty()) sheet.title = Name();
    return sheet;
  }

  // app::SearchProvider
  void Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
              std::vector<app::SearchResult>& out) override {
    SearchHost host{&q, &cancel, &out};
    fv_search s{};
    s.struct_size = sizeof(fv_search);
    s.host = &host;
    s.text = q.text.c_str();
    if (q.area) {
      s.has_area = 1;
      s.south_west = fv_geo_point{q.area->ll.lat, q.area->ll.lon};
      s.north_east = fv_geo_point{q.area->ur.lat, q.area->ur.lon};
    }
    s.max_results = q.max_results;
    s.match = [](const fv_search* q, const char* label) {
      return app::TextMatchQuality(Host(q).query->text, OrEmpty(label));
    };
    s.in_area = [](const fv_search* q, fv_geo_point p) {
      return app::SearchAreaAccepts(*Host(q).query, GeoPoint{p.lat, p.lon}) ? 1 : 0;
    };
    s.cancelled = [](const fv_search* q) { return Host(q).cancel->load() ? 1 : 0; };
    s.add = [](const fv_search* q, uint64_t feature, const char* title, const char* detail,
               fv_geo_point at, fv_geo_point sw, fv_geo_point ne, int quality) {
      SearchHost& h = Host(q);
      if (h.query->max_results != 0 && h.added >= h.query->max_results) return;
      app::SearchResult r;
      r.title = OrEmpty(title);
      r.detail = OrEmpty(detail);
      r.position = GeoPoint{at.lat, at.lon};
      r.bounds = GeoRect{GeoPoint{sw.lat, sw.lon}, GeoPoint{ne.lat, ne.lon}};
      r.match_quality = quality;
      r.feature = feature;
      h.out->push_back(std::move(r));
      ++h.added;
    };
    cls_.search(self_, &s);
  }

  // app::EditTarget
  void EnterEditFocus() override {
    if (CLASS_HAS(cls_, enter_edit_focus)) cls_.enter_edit_focus(self_);
  }
  void ReleaseEditFocus() override {
    if (CLASS_HAS(cls_, release_edit_focus)) cls_.release_edit_focus(self_);
  }
  bool CanUndo() const override { return CLASS_HAS(cls_, can_undo) && cls_.can_undo(self_) != 0; }
  void Undo() override {
    if (CLASS_HAS(cls_, undo)) cls_.undo(self_);
  }
  bool CanRedo() const override { return CLASS_HAS(cls_, can_redo) && cls_.can_redo(self_) != 0; }
  void Redo() override {
    if (CLASS_HAS(cls_, redo)) cls_.redo(self_);
  }

 private:
  /// Passes a mouse event with the last frame's view.
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
    return cls_.on_mouse(self_, proj ? &view : nullptr, &ev) != 0;
  }

  const PluginLibrary& lib_;
  const fv_overlay_class& cls_;
  std::shared_ptr<fv_overlay_ref> ref_;
  void* self_;
  bool has_properties_ = false;
  std::vector<app::PropertySpec> specs_;
  std::mutex last_proj_mutex_;
  std::optional<MapProjection> last_proj_;
};

// MARK: The editor adapter

app::CursorId ToCursor(int c) {
  switch (c) {
    case FV_CURSOR_ARROW: return app::CursorId::kDefault;
    case FV_CURSOR_HAND: return app::CursorId::kHand;
    case FV_CURSOR_MOVE: return app::CursorId::kMove;
    case FV_CURSOR_NO: return app::CursorId::kNo;
    case FV_CURSOR_WAIT: return app::CursorId::kWait;
    default: return app::CursorId::kCrosshair;
  }
}

/// An app::OverlayEditor backed by a plugin's editor class.
class LibraryEditor : public app::OverlayEditor {
 public:
  explicit LibraryEditor(std::shared_ptr<fv_editor_ref> ref) : ref_(std::move(ref)), cls_(*ref_->cls) {
    ref_->editor = this;
  }
  ~LibraryEditor() override {
    ref_->editor = nullptr;
    cls_.destroy(ref_->self);
  }

  Status Activate() override {
    if (!EDITOR_HAS(cls_, activate)) return Status::Ok();
    BeginCall();
    return FromPlugin(cls_.activate(ref_->self), std::string(cls_.name) + " activate");
  }
  Status Deactivate() override {
    if (!EDITOR_HAS(cls_, deactivate)) return Status::Ok();
    BeginCall();
    return FromPlugin(cls_.deactivate(ref_->self), std::string(cls_.name) + " deactivate");
  }

  app::CursorId DefaultCursor() const override { return ToCursor(cls_.cursor); }

  app::EditorUiConstraints UiConstraints() const override {
    app::EditorUiConstraints c;
    c.disable_rotation = (cls_.flags & FV_EDITOR_DISABLE_ROTATION) != 0;
    c.disable_projection_change = (cls_.flags & FV_EDITOR_DISABLE_PROJECTION_CHANGE) != 0;
    c.requires_north_up = (cls_.flags & FV_EDITOR_REQUIRES_NORTH_UP) != 0;
    c.disable_overlay_reorder = (cls_.flags & FV_EDITOR_DISABLE_OVERLAY_REORDER) != 0;
    return c;
  }

  bool AutoEnterOnCreate() const override { return (cls_.flags & FV_EDITOR_NO_AUTO_CREATE) == 0; }

  std::vector<app::MenuNode> Tools() const override {
    if (!EDITOR_HAS(cls_, tools) || !EDITOR_HAS(cls_, run_tool)) return {};
    app::MenuNode root;
    const std::shared_ptr<fv_editor_ref> ref = ref_;
    BuildMenu(&root, [ref](const std::string& id) -> std::function<void()> {
      return [ref, id] {
        if (ref->editor == nullptr) return;
        BeginCall();
        const Status s = FromPlugin(ref->cls->run_tool(ref->self, id.c_str()), id);
        if (!s.ok()) FV_LOG_WARNING("plugin: " << s.message);
      };
    }, [&](const fv_menu* m) { cls_.tools(ref_->self, m); });
    return std::move(root.children);
  }

 private:
  std::shared_ptr<fv_editor_ref> ref_;
  const fv_editor_class& cls_;
};

// MARK: Host services

void HostRequestRedraw(fv_overlay_ref* ref) {
  if (ref == nullptr) return;
  // Held through the call so the adapter cannot be destroyed under it.
  std::lock_guard<std::mutex> lock(ref->mutex);
  if (ref->overlay == nullptr) return;
  if (app::HostAware* a = ref->overlay->AsHostAware())
    if (app::OverlayHost* h = a->host()) h->RequestRedraw();
}

/// The host of a UI-thread call's overlay, or null.
app::OverlayHost* HostOf(fv_overlay_ref* ref) {
  if (ref == nullptr || ref->overlay == nullptr) return nullptr;
  app::HostAware* a = ref->overlay->AsHostAware();
  return a != nullptr ? a->host() : nullptr;
}

int HostCaptureMouse(fv_overlay_ref* ref) {
  app::OverlayHost* h = HostOf(ref);
  if (h == nullptr) return FV_ERR_NOT_FOUND;
  const Status s = h->CaptureMouse(*ref->overlay);
  if (!s.ok()) t_plugin_error = s.message;
  return s.ok() ? FV_OK : FV_ERR_NOT_FOUND;
}

void HostReleaseMouse(fv_overlay_ref* ref) {
  if (app::OverlayHost* h = HostOf(ref)) h->ReleaseMouse(*ref->overlay);
}

void HostSetDirty(fv_overlay_ref* ref, int dirty) {
  if (ref == nullptr || ref->overlay == nullptr) return;
  if (app::Persistence* p = ref->overlay->AsPersistence()) p->set_dirty(dirty != 0);
}

int HostElevationAt(fv_overlay_ref* ref, fv_geo_point p, double* metres) {
  if (metres == nullptr) return FV_ERR_INVALID_ARG;
  app::OverlayHost* h = HostOf(ref);
  if (h == nullptr) return FV_ERR_NOT_FOUND;
  return h->ElevationAt(GeoPoint{p.lat, p.lon}, metres) ? FV_OK : FV_ERR_OUT_OF_COVERAGE;
}

void HostShowStatus(fv_overlay_ref* ref, const char* text) {
  if (app::OverlayHost* h = HostOf(ref)) h->ShowStatus(OrEmpty(text));
}

void* HostEdited(fv_editor_ref* ref) {
  if (ref == nullptr || ref->editor == nullptr) return nullptr;
  const auto* o = dynamic_cast<const LibraryOverlay*>(ref->editor->edited());
  return o != nullptr && &o->library() == ref->lib ? o->self() : nullptr;
}

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
  if (api.editor_count > 0 && api.editors == nullptr) return "editor_count without editors";
  for (size_t i = 0; i < api.editor_count; ++i) {
    const fv_editor_class& c = api.editors[i];
    if (c.struct_size < offsetof(fv_editor_class, destroy) + sizeof(c.destroy) ||
        c.name == nullptr || c.create == nullptr || c.destroy == nullptr)
      return "editor class " + std::to_string(i) + " lacks a name, create or destroy";
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

const fv_editor_class* PluginLibrary::FindEditor(const std::string& name) const {
  for (size_t i = 0; i < api.editor_count; ++i)
    if (name == api.editors[i].name) return &api.editors[i];
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
  lib->host.request_redraw = HostRequestRedraw;
  lib->host.capture_mouse = HostCaptureMouse;
  lib->host.release_mouse = HostReleaseMouse;
  lib->host.set_dirty = HostSetDirty;
  lib->host.elevation_at = HostElevationAt;
  lib->host.show_status = HostShowStatus;
  lib->host.edited = HostEdited;
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
  auto ref = std::make_shared<fv_overlay_ref>();
  ref->cls = &cls;
  BeginCall();
  ref->self = cls.create(&lib.host, ref.get(), type_id.c_str());
  if (ref->self == nullptr) {
    FV_LOG_WARNING("plugin: " << type_id << ": create failed"
                              << (t_plugin_error.empty() ? "" : ": " + t_plugin_error));
    return nullptr;
  }
  return std::make_shared<LibraryOverlay>(lib, cls, std::move(ref), display_name);
}

std::function<std::unique_ptr<app::OverlayEditor>()> MakeLibraryEditorFactory(
    const PluginLibrary& lib, const fv_editor_class& cls) {
  const PluginLibrary* l = &lib;
  const fv_editor_class* c = &cls;
  return [l, c]() -> std::unique_ptr<app::OverlayEditor> {
    auto ref = std::make_shared<fv_editor_ref>();
    ref->lib = l;
    ref->cls = c;
    BeginCall();
    ref->self = c->create(&l->host, ref.get());
    if (ref->self == nullptr) {
      FV_LOG_WARNING("plugin: editor " << c->name << ": create failed"
                                       << (t_plugin_error.empty() ? "" : ": " + t_plugin_error));
      return nullptr;
    }
    return std::make_unique<LibraryEditor>(std::move(ref));
  };
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
