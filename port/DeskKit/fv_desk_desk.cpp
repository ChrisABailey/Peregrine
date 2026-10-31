// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_desk.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "fv_desk_base_map.h"
#include "fv_desk_map_options.h"
#include "fv_desk_overlay_manifest.h"
#include "fv_desk_feature_editor.h"
#include "fv_desk_point_editor.h"
#include "fv_desk_user_settings.h"
#include "fv_route_editor.h"
#include "fv_route_overlay.h"
#include "fv_route_planner.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/feature/points_layer.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/elevation_stack.h"
#include "fvkit/formats/geotiff_dem.h"
#include "fvkit/log.h"
#include "fvkit/overlay/contour_overlay.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/overlay/ta_mask_overlay.h"
#include "fvkit/settings.h"

namespace fv {
namespace desk {
namespace {

/// Opacity of the overlays that are not being edited while an editor is active.
constexpr int kDimmedOverlayPercent = 50;

constexpr char kWorkspaceConfig[] = "workspace";
constexpr char kWorkspacePrefix[] = "session.workspace.";
/// User-settings rows `recent.overlay.<n>.type` and `.path`, most recent first.
constexpr char kRecentPrefix[] = "recent.overlay.";

app::FileTypeDesc WorkspaceFileType() {
  app::FileTypeDesc d;
  d.default_extension = kWorkspaceExtension;
  d.open_filters = {{"Peregrine Workspace (*.fvws)", "*.fvws"}};
  d.save_filters = d.open_filters;
  return d;
}

app::FileTypeDesc CatalogFileType() {
  app::FileTypeDesc d;
  d.default_extension = "sqlite";
  d.open_filters = {{"Peregrine Map Catalog (*.sqlite)", "*.sqlite"}, {"Database (*.db)", "*.db"}};
  d.save_filters = {{"Peregrine Map Catalog (*.sqlite)", "*.sqlite"}};
  return d;
}

/// True for a catalog a second connection can open: a file, not ":memory:".
bool IsCatalogFile(const std::string& path) { return !path.empty() && path != ":memory:"; }

Shortcut Keys(const char* text) {
  Shortcut s;
  Shortcut::Parse(text, &s);
  return s;
}

/// The node at `path` in an editor's tool tree, or null.
const app::MenuNode* NodeAt(const std::vector<app::MenuNode>& tools,
                            const std::vector<size_t>& path) {
  const std::vector<app::MenuNode>* level = &tools;
  const app::MenuNode* node = nullptr;
  for (size_t i : path) {
    if (i >= level->size()) return nullptr;
    node = &(*level)[i];
    level = &node->children;
  }
  return node;
}

std::string PathId(const std::vector<size_t>& path) {
  std::string id = "editor.tool";
  for (size_t i : path) id += "." + std::to_string(i);
  return id;
}

/// True when `rest` is "<n>.<field>" with n >= `count`: a configuration row
/// left over from an earlier, longer save.
bool IsStaleRow(const std::string& rest, int count) {
  const size_t dot = rest.find('.');
  if (dot == std::string::npos || dot == 0) return false;
  for (size_t i = 0; i < dot; ++i)
    if (rest[i] < '0' || rest[i] > '9') return false;
  return std::atoi(rest.substr(0, dot).c_str()) >= count;
}

}  // namespace

std::string FormatScale(double scale_denom) {
  if (!(scale_denom > 0) || !std::isfinite(scale_denom)) return std::string();
  std::string digits = std::to_string(std::llround(scale_denom));
  std::string out;
  const size_t n = digits.size();
  for (size_t i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return "1:" + out;
}

// MARK: Shell tap

/// Forwards every AppShell call to the DeskShell and refreshes the editor
/// menu when the editor changes.
class Desk::ShellTap : public app::AppShell {
 public:
  explicit ShellTap(Desk& desk) : desk_(desk) {}
  SaveAnswer AskSave(const std::string& name) override { return Shell().AskSave(name); }
  std::vector<std::string> ChooseFilesToOpen(const app::FileTypeDesc& d) override {
    return Shell().ChooseFilesToOpen(d);
  }
  std::pair<std::string, int> ChooseSaveSpec(const app::FileTypeDesc& d,
                                             const std::string& suggested) override {
    return Shell().ChooseSaveSpec(d, suggested);
  }
  std::optional<int> ChooseFromList(const std::string& title,
                                    const std::vector<std::string>& rows) override {
    return Shell().ChooseFromList(title, rows);
  }
  bool ConfirmRevert(const std::string& spec) override { return Shell().ConfirmRevert(spec); }
  void SetCursor(app::CursorId c) override { Shell().SetCursor(c); }
  void ShowHint(const app::HintText& h) override {
    desk_.SetOverlayHint(h);
    Shell().ShowHint(h);
  }
  void ShowContextMenu(PixelPoint at, const app::MenuNode& root) override {
    Shell().ShowContextMenu(at, root);
  }
  void RequestInvalidate() override { Shell().RequestInvalidate(); }
  void OnEditorChanged(const app::TypeId& id, app::OverlayEditor* editor) override {
    Shell().OnEditorChanged(id, editor);
    desk_.RegisterEditorCommands();
    desk_.RebuildMenus();
  }
  void ReportError(const Status& s) override {
    FV_LOG_ERROR(s.message);
    Shell().ReportError(s);
  }

 private:
  DeskShell& Shell() { return desk_.shell_; }
  Desk& desk_;
};

// MARK: Stack hook

/// Re-lists the open file overlays when the stack or a file name changes.
class Desk::StackHook : public StackObserver {
 public:
  explicit StackHook(Desk& desk) : desk_(desk) {}
  void OverlayAdded(Overlay& o) override {
    desk_.NoteRecent(o);
    // An elevation overlay with no source draws nothing, which reads as broken.
    if (!desk_.BindOverlay(o)) {
      const app::OverlayTypeDesc* d = desk_.types_.Find(o.type_id());
      const std::string what = d != nullptr ? d->display_name : o.Name();
      desk_.Report(Status::Error(kNotFound, what + " needs elevation data (DTED or lidar); the catalog has none."));
    }
    Changed();
  }
  void OverlayRemoved(Overlay& o) override {
    desk_.DropFeatureViews(o);
    desk_.undo_.RemoveOwner(&o);
    Changed();
  }
  void OverlayFileSpecChanged(Overlay&) override { Changed(); }
  void OverlayOrderChanged() override { ++desk_.layers_generation_; }
  void CurrentChanged(Overlay*, Overlay*) override { ++desk_.layers_generation_; }
  void OverlayDirtyChanged(Overlay&) override { ++desk_.layers_generation_; }

 private:
  void Changed() {
    ++desk_.layers_generation_;
    desk_.RegisterInstanceCommands();
    desk_.RebuildMenus();
  }
  Desk& desk_;
};

// MARK: Construction

Desk::Desk(DeskShell& shell, Settings& settings, MapGroups groups)
    : shell_(shell), settings_(settings), groups_(std::move(groups)), menus_(commands_) {
  tap_ = std::make_unique<ShellTap>(*this);
  search_ = std::make_unique<DeskSearch>();
  overlays_.SetTypeRegistry(&types_);
  session_ = std::make_unique<app::OverlaySession>(types_, overlays_, *tap_, settings_);
  editors_ = std::make_unique<app::EditorManager>(types_, overlays_, *tap_);
  session_->SetEditorManager(editors_.get());
  editors_->SetSession(session_.get());
  // Everything but the overlay being edited draws at half strength.
  editors_->SetDimOthers(kDimmedOverlayPercent);
  overlays_.SetOffscreenFont(HostLabelFont());
  pick_ = std::make_unique<app::PickSession>(overlays_, *tap_);
  stack_hook_ = std::make_unique<StackHook>(*this);
  overlays_.AddObserver(stack_hook_.get());

  Status s = app::RegisterBuiltinOverlayTypes(types_);
  if (!s.ok()) Warn("built-in overlay types: " + s.message);
  s = RegisterPointEditor(types_, *editors_, [this] {
    const Status st = EditSelectedFeature();
    if (!st.ok()) Report(st);
  });
  if (!s.ok()) Warn("points editor: " + s.message);
  // The layer dialogs live on the feature editor's palette; each runs the
  // Overlay command of the same name, registered below.
  std::vector<FeatureLayerTool> layer_tools;
  struct LayerCommand {
    const char* id;
    const char* label;
    const char* icon;
  };
  for (const LayerCommand& c : {LayerCommand{"overlay.attribute_table", "Attribute Table…", "attribute_table"},
                                LayerCommand{"overlay.edit_feature", "Edit Feature…", "edit_point"},
                                LayerCommand{"overlay.style", "Layer Style…", "layer_style"},
                                LayerCommand{"overlay.style_feature", "Style This Feature…", "style_feature"},
                                LayerCommand{"overlay.fields", "Layer Fields…", "layer_fields"},
                                LayerCommand{"overlay.export_selection", "Export Selection…", "export_selection"}}) {
    const std::string id = c.id;
    FeatureLayerTool t;
    t.label = c.label;
    t.icon = c.icon;
    t.enabled = [this, id] { return commands_.IsEnabled(id); };
    t.run = [this, id] { (void)commands_.Execute(id); };
    layer_tools.push_back(std::move(t));
  }
  s = RegisterFeatureEditor(types_, *editors_, std::move(layer_tools));
  if (!s.ok()) Warn("feature editor: " + s.message);
  undo_.SetOnChange([this] {
    if (table_) {
      const Status st = table_->Reload();
      if (!st.ok()) Warn("attribute table: " + st.message);
    }
    ++feature_views_gen_;
    shell_.RequestInvalidate();
  });
  s = RegisterRouteOverlayType(types_);
  if (s.ok()) s = RegisterRouteEditor(types_, *editors_, [this] { OpenRouteProfile(); });
  if (!s.ok()) Warn("route overlay type: " + s.message);
  ResolveRoutePlanner();

  view_ = std::make_unique<view::MapView>(view::Viewport::Make(GeoPoint{0, 0}, 1.0e7),
                                          view::LadderKind::kUniform, nullptr);
  map_options_ = BuiltinMapOptions();
  overlay_options_ = {RoutingOptions([this] { ResolveRoutePlanner(); })};
  menus_.SetOnChange([this] { shell_.MenusChanged(); });
  RegisterStaticCommands();
  Refresh();
}

Desk::~Desk() {
  overlays_.RemoveObserver(stack_hook_.get());
  menus_.SetOnChange(nullptr);
}

void Desk::Refresh() {
  RegisterGroupCommands();
  RegisterOverlayTypeCommands();
  RegisterInstanceCommands();
  RegisterRecentCommands();
  RegisterEditorCommands();
  RebuildMenus();
}

std::vector<app::TypeId> Desk::LoadOverlayManifests(const std::vector<std::string>& dirs) {
  std::vector<app::TypeId> ids = RegisterOverlayManifests(types_, dirs, &warnings_);
  Refresh();
  return ids;
}

Status Desk::Execute(const std::string& id) { return commands_.Execute(id); }

void Desk::RebuildMenus() { menus_.Rebuild(editor_menu_ ? &*editor_menu_ : nullptr); }

void Desk::Warn(std::string w, const char* file, int line) {
  LogWrite(LogLevel::kWarning, file, line, w);
  warnings_.push_back(std::move(w));
}

app::FlowResult Desk::Report(const Status& s) {
  FV_LOG_ERROR(s.message);
  shell_.ReportError(s);
  return app::FlowResult::kFailed;
}

// MARK: Commands

void Desk::RegisterStaticCommands() {
  auto add = [this](const char* id, const char* label, const char* keys,
                    std::function<void()> action, std::function<bool()> enabled = nullptr,
                    std::function<bool()> checked = nullptr, const char* icon = "") {
    Command c;
    c.id = id;
    c.label = label;
    c.icon = icon;
    c.shortcut = Keys(keys);
    c.action = std::move(action);
    c.enabled = std::move(enabled);
    c.checked = std::move(checked);
    const Status s = commands_.Register(std::move(c));
    if (!s.ok()) Warn(s.message);
  };
  auto has_target = [this] { return FileTarget() != nullptr; };

  add("file.save", "Save", "Primary+S",
      [this] { if (Overlay* o = FileTarget()) session_->Save(*o); }, has_target);
  add("file.save_as", "Save As…", "Primary+Shift+S",
      [this] { if (Overlay* o = FileTarget()) session_->SaveAs(*o); }, has_target);
  add("file.close", "Close", "Primary+W",
      [this] { if (Overlay* o = FileTarget()) session_->Close(*o); }, has_target);
  add("file.open_workspace", "Open Workspace…", "", [this] {
    const std::vector<std::string> files = shell_.ChooseFilesToOpen(WorkspaceFileType());
    if (!files.empty()) OpenWorkspace(files.front());
  });
  add("file.save_workspace", "Save Workspace…", "", [this] {
    const auto spec = shell_.ChooseSaveSpec(WorkspaceFileType(),
                                            std::string("Untitled.") + kWorkspaceExtension);
    if (!spec.first.empty()) SaveWorkspace(spec.first);
  });
  add("edit.undo", "Undo", "Primary+Z", [this] {
    const Status st = undo_.Undo();
    if (!st.ok()) Report(st);
  }, [this] { return undo_.CanUndo(); }, nullptr, "undo");
  add("edit.redo", "Redo", "Primary+Shift+Z", [this] {
    const Status st = undo_.Redo();
    if (!st.ok()) Report(st);
  }, [this] { return undo_.CanRedo(); }, nullptr, "redo");
  add("app.quit", "Quit", "Primary+Q", [this] {
    if (session_->Exit() == app::FlowResult::kDone) shell_.Quit();
  });

  add("map.zoom_in", "Zoom In", "Primary+=", [this] { view_->Step(+1); }, nullptr, nullptr,
      "zoom_in");
  add("map.zoom_out", "Zoom Out", "Primary+-", [this] { view_->Step(-1); }, nullptr, nullptr,
      "zoom_out");
  add("map.goto", "Go To / Find…", "Primary+F", [this] { shell_.ShowFind(); });
  add("map.recenter", "Recenter on Data", "", [this] { Recenter(); },
      [this] { return CurrentGroup() != nullptr; }, nullptr, "recenter");
  add("map.pan.north", "North", "Up", [this] { PanScreens(0, -0.5); });
  add("map.pan.south", "South", "Down", [this] { PanScreens(0, 0.5); });
  add("map.pan.east", "East", "Right", [this] { PanScreens(0.5, 0); });
  add("map.pan.west", "West", "Left", [this] { PanScreens(-0.5, 0); });

  size_t n = 0;
  const ProjectionType* types = AllProjectionTypes(&n);
  for (size_t i = 0; i < n; ++i) {
    const ProjectionType t = types[i];
    const std::string id = std::string("map.projection.") + ProjectionKey(t);
    add(id.c_str(), ProjectionTitle(t), "",
        [this, t] { view_->SetViewport(view_->View().WithProjectionType(t)); }, nullptr,
        [this, t] { return view_->View().Type() == t; });
  }

  add("overlay.open", "Open Overlay…", "Primary+Shift+O",
      [this] { session_->OpenFileOverlays(app::TypeId()); }, [this] {
        for (const app::OverlayTypeDesc* d : types_.All())
          if (d->file && !d->display_name.empty()) return true;
        return false;
      });
  add("map.options", "Options…", "", [this] { shell_.ShowMapOptions(MapOptions()); },
      [this] { return !map_options_.empty(); });
  add("overlay.options", "Options…", "",
      [this] { shell_.ShowOverlayOptions(OverlayOptions()); });
  add("overlay.layers", "Layers…", "Primary+L", [this] { shell_.ShowLayers(); });
  add("overlay.layers_panel", "Layers", "",
      [this] { layers_shown_ ? shell_.HideLayers() : shell_.ShowLayers(); }, nullptr,
      [this] { return layers_shown_; }, "layers");
  add("overlay.attribute_table", "Attribute Table…", "", [this] {
    const Status st = OpenAttributeTable();
    if (!st.ok()) Report(st);
  },
      [this] { return FeatureLayerOf(overlays_.FindShared(overlays_.current())) != nullptr; });
  add("overlay.edit_feature", "Edit Feature…", "", [this] {
    const Status st = EditSelectedFeature();
    if (!st.ok()) Report(st);
  }, [this] {
    const Overlay* o = overlays_.current();
    return o != nullptr && SelectedFeatureOf(*o) != 0 && FeatureLayerOf(overlays_.FindShared(o)) != nullptr;
  });
  add("overlay.style", "Layer Style…", "", [this] {
    const Status st = OpenLayerStyle();
    if (!st.ok()) Report(st);
  }, [this] { return dynamic_cast<const FeatureOverlay*>(overlays_.current()) != nullptr; });
  // Off the menus: Overlay ▸ New ▸ Feature Layer… runs the same dialog. Kept for shells that run it by id.
  add("overlay.new_layer", "New Feature Layer…", "", [this] { OpenNewLayer(); });
  add("overlay.fields", "Layer Fields…", "", [this] {
    const Status st = OpenLayerFields();
    if (!st.ok()) Report(st);
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->layer() != nullptr;
  });
  add("overlay.style_feature", "Style This Feature…", "", [this] {
    const Status st = StyleSelectedFeature();
    if (!st.ok()) Report(st);
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->selected() != 0;
  });
  add("overlay.export_selection", "Export Selection…", "", [this] {
    const Status st = ExportSelection();
    if (!st.ok()) Report(st);
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->layer() != nullptr && !f->selection().empty();
  });
  RegisterLayerManagerCommands();
  RegisterRuleCommands();
  RegisterCatalogCommands();
}

void Desk::RegisterLayerManagerCommands() {
  auto add = [this](const char* id, const char* label, std::function<void()> action,
                    std::function<bool()> enabled) {
    Command c;
    c.id = id;
    c.label = label;
    c.action = std::move(action);
    c.enabled = std::move(enabled);
    const Status s = commands_.Register(std::move(c));
    if (!s.ok()) Warn(s.message);
  };
  auto has_current = [this] { return overlays_.current() != nullptr; };
  auto source_shown = [this](SourceShown not_this) {
    const std::vector<SourceRow> rows = sources_.Rows();
    const int at = CurrentSource(rows);
    return at >= 0 && rows[at].shown != not_this;
  };
  auto zoom_box = [](const std::vector<std::shared_ptr<Overlay>>& overlays, GeoRect* box) {
    bool any = false;
    for (const std::shared_ptr<Overlay>& o : overlays) {
      GeoRect b;
      if (!LayerExtent(o, &b)) continue;
      if (!any) *box = b;
      box->ll.lat = std::min(box->ll.lat, b.ll.lat);
      box->ll.lon = std::min(box->ll.lon, b.ll.lon);
      box->ur.lat = std::max(box->ur.lat, b.ur.lat);
      box->ur.lon = std::max(box->ur.lon, b.ur.lon);
      any = true;
    }
    return any;
  };

  add("layers.properties", "Properties…", [this] {
    // A static overlay's properties are its type's overlay options.
    if (const Overlay* o = overlays_.current()) {
      const app::OverlayTypeDesc* d = types_.Find(o->type_id());
      std::shared_ptr<OptionsModel> options = OverlayOptions();
      if (d != nullptr && !d->file && options->SelectPage(d->id)) {
        shell_.ShowOverlayOptions(std::move(options));
        return;
      }
    }
    const Status st = OpenLayerProperties(PropertiesTab::kGeneral);
    if (!st.ok()) Report(st);
  }, has_current);
  add("layers.select_by_attributes", "Select by Attributes…", [this] {
    const Status st = OpenSelectByAttributes();
    if (!st.ok()) Report(st);
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->layer() != nullptr;
  });
  add("layers.clear_selection", "Clear Selection", [this] { ClearSelection(); }, [this] {
    const Overlay* o = overlays_.current();
    return o != nullptr && SelectedFeatureOf(*o) != 0;
  });
  add("layers.zoom_to", "Zoom to Layer", [this, zoom_box] {
    GeoRect box;
    if (zoom_box({overlays_.FindShared(overlays_.current())}, &box)) ZoomTo(box);
  }, [this] {
    GeoRect box;
    return LayerExtent(overlays_.FindShared(overlays_.current()), &box);
  });
  add("layers.close", "Close", [this] {
    Overlay* o = overlays_.current();
    if (o == nullptr) return;
    if (types_.IsFile(o->type_id())) session_->Close(*o);
    else session_->ToggleStatic(o->type_id());
  }, has_current);

  add("layers.source.properties", "Properties…", [this] {
    const std::vector<SourceRow> rows = sources_.Rows();
    const int at = CurrentSource(rows);
    if (at < 0) return;
    CloseLayerProperties();
    properties_title_ = "Source Properties — " + rows[at].name;
    properties_ = SourceProperties(overlays_, rows[at]);
    properties_tab_ = PropertiesTab::kSource;
    shell_.ShowLayerProperties();
  }, has_current);
  add("layers.source.show_all", "Show All Layers", [this] {
    const int at = CurrentSource(sources_.Rows());
    if (at >= 0) SetSourceVisible(static_cast<size_t>(at), true);
  }, [source_shown] { return source_shown(SourceShown::kAll); });
  add("layers.source.hide_all", "Hide All Layers", [this] {
    const int at = CurrentSource(sources_.Rows());
    if (at >= 0) SetSourceVisible(static_cast<size_t>(at), false);
  }, [source_shown] { return source_shown(SourceShown::kNone); });
  add("layers.source.zoom_to", "Zoom to Source", [this, zoom_box] {
    GeoRect box;
    if (zoom_box(CurrentSourceOverlays(), &box)) ZoomTo(box);
  }, [this, zoom_box] {
    GeoRect box;
    return zoom_box(CurrentSourceOverlays(), &box);
  });
  add("layers.source.new_layer", "New Layer…", [this] {
    const std::vector<SourceRow> rows = sources_.Rows();
    const int at = CurrentSource(rows);
    if (at >= 0) OpenNewLayer(rows[at].path);
  }, [this] {
    const std::vector<SourceRow> rows = sources_.Rows();
    const int at = CurrentSource(rows);
    if (at < 0) return false;
    std::string ext = std::filesystem::path(rows[at].path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".gpkg";
  });
  add("layers.source.close", "Close", [this] {
    for (const std::shared_ptr<Overlay>& o : CurrentSourceOverlays()) {
      const app::FlowResult r = types_.IsFile(o->type_id()) ? session_->Close(*o) : session_->ToggleStatic(o->type_id());
      if (r != app::FlowResult::kDone) return;  // a cancelled save prompt keeps the rest open
    }
  }, has_current);
}

int Desk::CurrentSource(const std::vector<SourceRow>& rows) const {
  for (size_t i = 0; i < rows.size(); ++i)
    if (rows[i].current) return static_cast<int>(i);
  return -1;
}

std::vector<std::shared_ptr<Overlay>> Desk::CurrentSourceOverlays() const {
  std::vector<std::shared_ptr<Overlay>> out;
  const std::vector<SourceRow> rows = sources_.Rows();
  const int at = CurrentSource(rows);
  if (at < 0) return out;
  const std::vector<LayerRow> layers = layers_.Rows();
  for (size_t i : rows[at].layers)
    if (std::shared_ptr<Overlay> o = overlays_.FindShared(layers[i].overlay)) out.push_back(std::move(o));
  return out;
}

Status Desk::OpenLayerProperties(PropertiesTab tab) {
  const std::shared_ptr<Overlay> o = overlays_.FindShared(overlays_.current());
  if (!o) return Status::Error(kNotFound, "no layer is selected");
  if (properties_overlay_ != o.get()) CloseLayerProperties();
  properties_title_ = "Layer Properties — " + o->Name();
  properties_ = LayerProperties(overlays_, *o);
  properties_tab_ = PropertiesTab::kSource;
  if (shell_.HostsPropertiesTabs()) {
    properties_overlay_ = o.get();
    if (!general_) {
      Status s = LayerGeneralModel::Open(o, &general_);
      if (!s.ok()) return s;
    }
    // An overlay with neither opacity nor features has no Display tab.
    if (!display_) LayerDisplayModel::Open(o, &display_);
    const auto feature = std::dynamic_pointer_cast<FeatureOverlay>(o);
    if (feature && feature->layer() != nullptr) {
      if (!selection_) {
        Status s = LayerSelectionModel::Open(feature, &selection_);
        if (!s.ok()) return s;
      }
      if (!query_) {
        Status s = QueryBuilderModel::Open(feature, feature->definition_query().text(), &query_);
        if (!s.ok()) return s;
      }
      if (!PropertiesOwnsStyle()) {
        std::shared_ptr<LayerStyleModel> style;
        Status s = LayerStyleModel::Open(feature, 0, "Layer Style — " + o->Name(), &style);
        if (!s.ok()) return s;
        style_ = std::move(style);
      }
      if (!PropertiesOwnsFields()) {
        std::shared_ptr<LayerFieldsModel> fields;
        Status s = LayerFieldsModel::Open(feature, "Layer Fields — " + o->Name(), &fields);
        if (!s.ok()) return s;
        fields_ = std::move(fields);
      }
    }
    const std::vector<PropertiesTab> tabs = PropertiesTabs();
    properties_tab_ = std::find(tabs.begin(), tabs.end(), tab) != tabs.end() ? tab : PropertiesTab::kGeneral;
  }
  ++feature_views_gen_;
  shell_.ShowLayerProperties();
  return Status::Ok();
}

std::vector<PropertiesTab> Desk::PropertiesTabs() const {
  std::vector<PropertiesTab> tabs{PropertiesTab::kSource};
  if (general_) tabs.push_back(PropertiesTab::kGeneral);
  if (display_) tabs.push_back(PropertiesTab::kDisplay);
  if (PropertiesOwnsStyle()) {
    tabs.push_back(PropertiesTab::kSymbology);
    tabs.push_back(PropertiesTab::kLabels);
  }
  if (PropertiesOwnsFields()) tabs.push_back(PropertiesTab::kFields);
  if (selection_) tabs.push_back(PropertiesTab::kSelection);
  if (query_) tabs.push_back(PropertiesTab::kDefinitionQuery);
  return tabs;
}

bool Desk::PropertiesOwnsStyle() const {
  return properties_overlay_ != nullptr && style_ && style_->overlay() == properties_overlay_ &&
         style_->feature() == 0;
}

bool Desk::PropertiesOwnsFields() const {
  return properties_overlay_ != nullptr && fields_ && fields_->overlay() == properties_overlay_;
}

void Desk::ReopenPropertiesStyle() {
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(properties_overlay_));
  std::shared_ptr<LayerStyleModel> style;
  if (o && LayerStyleModel::Open(o, 0, "Layer Style — " + o->Name(), &style).ok()) style_ = std::move(style);
  else style_.reset();
}

bool Desk::LayerPropertiesDirty() const {
  return (general_ && general_->dirty()) || (display_ && display_->dirty()) ||
         (PropertiesOwnsStyle() && style_->dirty()) || (PropertiesOwnsFields() && fields_->dirty()) ||
         (selection_ && selection_->dirty()) || (query_ && query_->dirty());
}

Status Desk::ApplyLayerProperties() {
  if (display_ && display_->dirty()) {
    const auto feature = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(display_->overlay()));
    const bool fields_changed = feature && (feature->map_tip_field() != display_->display().map_tip_field ||
                                            feature->hyperlink_field() != display_->display().hyperlink_field);
    Status s = display_->Apply();
    if (!s.ok()) return s;
    // The file keeps the map tip and hyperlink fields; opacity stays in the workspace.
    if (fields_changed && feature->AsPersistence() != nullptr) feature->AsPersistence()->set_dirty(true);
    ++layers_generation_;
    shell_.RequestInvalidate();
  }
  if (general_ && general_->dirty()) {
    Status s = general_->Apply();
    if (!s.ok()) return s;
    const std::shared_ptr<Overlay> o = overlays_.FindShared(general_->overlay());
    // A feature layer's file keeps these settings; other layers keep them in the workspace.
    if (o && dynamic_cast<FeatureOverlay*>(o.get()) != nullptr && o->AsPersistence() != nullptr)
      o->AsPersistence()->set_dirty(true);
    if (o) {
      properties_title_ = "Layer Properties — " + o->Name();
      properties_ = LayerProperties(overlays_, *o);
    }
    ++layers_generation_;
    ++feature_views_gen_;
    shell_.RequestInvalidate();
  }
  if (PropertiesOwnsStyle() && style_->dirty()) {
    Status s = ApplyLayerStyle();
    if (!s.ok()) return s;
  }
  if (PropertiesOwnsFields() && fields_->dirty()) {
    Status s = ApplyLayerFields();
    if (!s.ok()) return s;
  }
  const auto feature = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(properties_overlay_));
  auto dirty_file = [&feature] {
    if (feature && feature->AsPersistence() != nullptr) feature->AsPersistence()->set_dirty(true);
  };
  if (selection_ && selection_->dirty()) {
    Status s = selection_->Apply();
    if (!s.ok()) return s;
    dirty_file();
    shell_.RequestInvalidate();
  }
  if (query_ && query_->dirty()) {
    std::string json;
    Status s = query_->Expression(&json);
    if (!s.ok()) return s;
    if (!feature) return Status::Error(kNotFound, "the layer has closed");
    s = feature->SetDefinitionQuery(json);
    if (!s.ok()) return s;
    query_->MarkApplied();
    dirty_file();
    if (table_ && table_->overlay() == feature.get()) {
      s = table_->Reload();
      if (!s.ok()) Warn("attribute table: " + s.message);
    }
    ++feature_views_gen_;
    shell_.RequestInvalidate();
  }
  return Status::Ok();
}

Status Desk::RevertLayerProperties() {
  Status first = Status::Ok();
  auto keep = [&first](const Status& s) {
    if (first.ok() && !s.ok()) first = s;
  };
  if (general_) keep(general_->Revert());
  if (display_) keep(display_->Revert());
  if (PropertiesOwnsStyle()) keep(style_->Revert());
  if (PropertiesOwnsFields()) keep(fields_->Revert());
  if (selection_) keep(selection_->Revert());
  if (query_) query_->Revert();
  ++feature_views_gen_;
  return first;
}

void Desk::CloseLayerProperties() {
  if (PropertiesOwnsStyle()) style_.reset();
  if (PropertiesOwnsFields()) fields_.reset();
  general_.reset();
  display_.reset();
  selection_.reset();
  query_.reset();
  properties_overlay_ = nullptr;
  ++feature_views_gen_;
}

void Desk::RegisterCatalogCommands() {
  auto add = [this](const char* id, const char* label, const char* keys,
                    std::function<void()> action, std::function<bool()> enabled) {
    Command c;
    c.id = id;
    c.label = label;
    c.shortcut = Keys(keys);
    c.action = std::move(action);
    c.enabled = std::move(enabled);
    const Status s = commands_.Register(std::move(c));
    if (!s.ok()) Warn(s.message);
  };
  auto idle = [this] { return !Building(); };
  auto idle_with_catalog = [this] { return !Building() && catalog_ != nullptr; };

  add("map.catalog_open", "Open Map Catalog…", "Primary+Alt+O", [this] {
    const std::vector<std::string> files = shell_.ChooseFilesToOpen(CatalogFileType());
    if (files.empty()) return;
    const Status s = OpenCatalog(files.front());
    if (!s.ok()) {
      Report(s);
      return;
    }
    if (CurrentGroup() != nullptr) Recenter();
  }, idle);

  add("map.sources", "Map Data Sources…", "", [this] {
    if (!IsCatalogFile(catalog_path_)) {
      const auto spec = shell_.ChooseSaveSpec(CatalogFileType(), "Peregrine Catalog.sqlite");
      if (spec.first.empty()) return;
      const Status s = UseCatalogFile(spec.first, true);
      if (!s.ok()) {
        Report(s);
        return;
      }
    }
    shell_.ShowDataSources();
  }, idle);

  add("map.generate_coverage", "Generate Coverage", "", [this] {
    const Status s = GenerateCoverage();
    if (!s.ok()) Report(s);
  }, [this] { return !Building() && IsCatalogFile(catalog_path_) && catalog_ != nullptr; });
}

void Desk::Recenter() {
  const MapGroup* g = CurrentGroup();
  std::vector<CoverageRow> rows;
  if (g == nullptr || !catalog_->SelectByGeoRect(GeoRect::World(), &rows).ok()) return;
  std::vector<GeoRect> frames;
  GeoRect box;
  for (const CoverageRow& r : rows) {
    if (std::find(g->formats.begin(), g->formats.end(), r.format) == g->formats.end()) continue;
    // A frame across the antimeridian is unwrapped east; the camera
    // normalizes the centre.
    GeoRect b = r.bounds;
    if (b.CrossesAntimeridian()) b.ur.lon += 360.0;
    if (frames.empty()) box = b;
    box.ll.lat = std::min(box.ll.lat, b.ll.lat);
    box.ll.lon = std::min(box.ll.lon, b.ll.lon);
    box.ur.lat = std::max(box.ur.lat, b.ur.lat);
    box.ur.lon = std::max(box.ur.lon, b.ur.lon);
    frames.push_back(b);
  }
  if (frames.empty()) return;
  GeoPoint c{(box.ll.lat + box.ur.lat) / 2, (box.ll.lon + box.ur.lon) / 2};
  // Separate clusters of data leave the box centre over nothing; use the
  // centre of the frame nearest it instead.
  const auto holds = [&c](const GeoRect& b) {
    return b.ll.lat <= c.lat && c.lat <= b.ur.lat && b.ll.lon <= c.lon && c.lon <= b.ur.lon;
  };
  if (std::none_of(frames.begin(), frames.end(), holds)) {
    double best = 0;
    GeoPoint nearest = c;
    for (const GeoRect& b : frames) {
      const GeoPoint m{(b.ll.lat + b.ur.lat) / 2, (b.ll.lon + b.ur.lon) / 2};
      const double d = (m.lat - c.lat) * (m.lat - c.lat) + (m.lon - c.lon) * (m.lon - c.lon);
      if (&b == &frames.front() || d < best) {
        best = d;
        nearest = m;
      }
    }
    c = nearest;
  }
  GoTo(c, view_->View().ScaleDenom());
}

void Desk::RegisterGroupCommands() {
  commands_.RemovePrefix("map.group.");
  for (const MapGroup* g : available_) {
    Command c;
    c.id = "map.group." + g->id;
    c.label = g->title;
    c.shortcut = Keys(g->shortcut.c_str());
    const std::string gid = g->id;
    c.checked = [this, gid] { return group_id_ == gid; };
    c.action = [this, gid] { SelectGroup(gid); };
    commands_.Register(std::move(c));
  }
}

void Desk::RegisterOverlayTypeCommands() {
  commands_.RemovePrefix("overlay.toggle.");
  commands_.RemovePrefix("overlay.new.");
  commands_.RemovePrefix("editor.mode.");
  for (const app::OverlayTypeDesc* d : types_.All()) {
    if (d->display_name.empty()) continue;
    const app::TypeId id = d->id;
    Command c;
    c.label = d->display_name;
    c.icon = d->icon;
    if (!d->file) {
      if (d->user_controllable) {
        c.id = "overlay.toggle." + id;
        c.checked = [this, id] { return overlays_.FirstOfType(id) != nullptr; };
        c.action = [this, id] { session_->ToggleStatic(id); };
        commands_.Register(c);
      }
    } else {
      c.id = "overlay.new." + id;
      c.action = [this, id] { session_->NewFileOverlay(id); };
      // A feature layer needs its geometry, fields and file chosen first.
      if (id == FeatureOverlay::kTypeId) {
        c.label = "Feature Layer…";
        c.action = [this] { OpenNewLayer(); };
      }
      commands_.Register(c);
    }
    if (d->editor_factory) {
      Command m;
      m.id = "editor.mode." + id;
      m.label = d->display_name;
      m.icon = d->icon;
      m.checked = [this, id] { return editors_->CurrentMode() == id; };
      m.action = [this, id] { editors_->ToggleEditor(id); };
      commands_.Register(std::move(m));
    }
  }
}

std::string Desk::DocumentName() const {
  std::shared_ptr<Overlay> doc = overlays_.FindShared(overlays_.current());
  if (!doc || !types_.IsFile(doc->type_id())) {
    doc.reset();
    const auto& stack = overlays_.Overlays();
    for (auto it = stack.rbegin(); it != stack.rend() && !doc; ++it)
      if (types_.IsFile((*it)->type_id())) doc = *it;
  }
  if (!doc) return std::string();
  if (app::Persistence* p = doc->AsPersistence()) {
    if (!p->file_spec().empty()) return std::filesystem::path(p->file_spec()).filename().string();
  }
  const app::OverlayTypeDesc* d = types_.Find(doc->type_id());
  const std::string ext = d != nullptr && d->file ? d->file->default_extension : std::string();
  return "unnamed." + (ext.empty() ? std::string("fvrte") : ext);
}

void Desk::RegisterRecentCommands() {
  commands_.RemovePrefix("file.recent.");
  for (size_t i = 0; i < recent_.size(); ++i) {
    Command c;
    c.id = "file.recent." + std::to_string(i);
    c.label = std::filesystem::path(recent_[i].path).filename().string();
    c.action = [this, i] { OpenRecent(i); };
    commands_.Register(std::move(c));
  }
}

void Desk::NoteRecent(Overlay& o) {
  if (!types_.IsFile(o.type_id())) return;
  const app::Persistence* p = o.AsPersistence();
  if (p == nullptr || p->file_spec().empty()) return;
  // Every layer of a multi-layer file is one entry: reopening the path opens them all.
  const RecentOverlay entry{o.type_id(), app::SplitSourceSpec(p->file_spec()).path};
  recent_.erase(std::remove(recent_.begin(), recent_.end(), entry), recent_.end());
  recent_.insert(recent_.begin(), entry);
  if (recent_.size() > kMaxRecentOverlays) recent_.resize(kMaxRecentOverlays);
  StoreRecent();
  RegisterRecentCommands();
}

void Desk::OpenRecent(size_t i) {
  if (i >= recent_.size()) return;
  const RecentOverlay entry = recent_[i];
  const app::FlowResult r = session_->OpenFile(entry.type, entry.path);
  if (r == app::FlowResult::kCanceled) return;
  recent_.erase(std::remove(recent_.begin(), recent_.end(), entry), recent_.end());
  // An already-open file is only made current, so no overlay is added to note it.
  if (r == app::FlowResult::kDone) recent_.insert(recent_.begin(), entry);
  StoreRecent();
  RegisterRecentCommands();
  RebuildMenus();
}

void Desk::StoreRecent() {
  if (user_settings_ == nullptr) return;
  user_settings_->ErasePrefix(kRecentPrefix);
  for (size_t i = 0; i < recent_.size(); ++i) {
    const std::string key = kRecentPrefix + std::to_string(i);
    user_settings_->Set(key + ".type", recent_[i].type);
    user_settings_->Set(key + ".path", recent_[i].path);
  }
}

void Desk::SetUserSettings(UserSettings* user) {
  user_settings_ = user;
  if (user == nullptr) return;
  recent_.clear();
  for (size_t i = 0; i < kMaxRecentOverlays; ++i) {
    const std::string key = kRecentPrefix + std::to_string(i);
    RecentOverlay entry{user->Get(key + ".type"), user->Get(key + ".path")};
    if (entry.type.empty() || entry.path.empty()) break;
    recent_.push_back(std::move(entry));
  }
  RegisterRecentCommands();
  RebuildMenus();
}

void Desk::RegisterInstanceCommands() {
  commands_.RemovePrefix("overlay.instance.");
  const auto& stack = overlays_.Overlays();
  int k = 0;
  // Top of the stack first, as the layers panel lists them.
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    const std::shared_ptr<Overlay>& o = *it;
    if (!types_.IsFile(o->type_id())) continue;
    Command c;
    c.id = "overlay.instance." + std::to_string(k++);
    c.label = o->Name();
    if (app::Persistence* p = o->AsPersistence()) {
      const std::string path = app::SplitSourceSpec(p->file_spec()).path;
      const std::string file = std::filesystem::path(path).filename().string();
      // A feature layer is listed by its own name under its file; other
      // documents by their file name.
      if (!file.empty() && o->type_id() == FeatureOverlay::kTypeId)
        c.submenu = file;
      else if (!file.empty())
        c.label = file;
    }
    const std::weak_ptr<Overlay> weak = o;
    c.checked = [this, weak] {
      const auto sp = weak.lock();
      return sp && overlays_.current() == sp.get();
    };
    c.action = [this, weak] {
      if (const auto sp = weak.lock()) overlays_.MakeCurrent(sp);
    };
    commands_.Register(std::move(c));
  }
}

void Desk::RegisterEditorCommands() {
  commands_.RemovePrefix("editor.tool.");
  editor_menu_.reset();
  app::OverlayEditor* editor = editors_->CurrentEditor();
  if (editor == nullptr) return;
  const app::OverlayTypeDesc* desc = types_.Find(editors_->CurrentMode());

  // The tree is re-read from the editor on every query, so a tool's enabled
  // and checked state stay live while the menu is open.
  auto live = [this](const std::vector<size_t>& path, auto&& use) {
    app::OverlayEditor* ed = editors_->CurrentEditor();
    if (ed == nullptr) return use(nullptr);
    const std::vector<app::MenuNode> tools = ed->Tools();
    return use(NodeAt(tools, path));
  };

  std::function<std::vector<MenuSlot>(const std::vector<app::MenuNode>&, std::vector<size_t>)>
      walk = [&](const std::vector<app::MenuNode>& nodes, std::vector<size_t> path) {
        std::vector<MenuSlot> slots;
        for (size_t i = 0; i < nodes.size(); ++i) {
          const app::MenuNode& n = nodes[i];
          std::vector<size_t> here = path;
          here.push_back(i);
          if (n.is_separator()) {
            slots.push_back(MenuSlot::Sep());
          } else if (!n.children.empty()) {
            slots.push_back(MenuSlot::Sub(n.label, walk(n.children, here)));
          } else {
            Command c;
            c.id = PathId(here);
            c.label = n.label;
            c.icon = n.icon;
            c.enabled = [live, here] {
              return live(here, [](const app::MenuNode* m) { return m && m->enabled && m->action; });
            };
            c.checked = [live, here] {
              return live(here, [](const app::MenuNode* m) { return m && m->checked; });
            };
            c.action = [live, here] {
              std::function<void()> act =
                  live(here, [](const app::MenuNode* m) { return m ? m->action : nullptr; });
              if (act) act();
            };
            slots.push_back(MenuSlot::Cmd(c.id));
            commands_.Register(std::move(c));
          }
        }
        return slots;
      };

  MenuSpec spec;
  spec.id = "editor";
  spec.title = desc ? desc->display_name : editors_->CurrentMode();
  spec.items = walk(editor->Tools(), {});
  editor_menu_ = std::move(spec);
}

Overlay* Desk::FileTarget() const {
  Overlay* cur = overlays_.current();
  if (cur == nullptr || !types_.IsFile(cur->type_id()) || cur->AsPersistence() == nullptr)
    return nullptr;
  return cur;
}

// MARK: Catalog and group

Status Desk::OpenCatalog(const std::string& path) {
  std::error_code ec;
  // Catalog::Open creates a database that does not exist; a named catalog
  // that is missing is an error here, not an empty catalog.
  if (path != ":memory:" && !std::filesystem::exists(path, ec))
    return Status::Error(kNotFound, "no catalog at " + path);
  auto c = std::make_shared<Catalog>();
  const Status s = c->Open(path);
  if (!s.ok()) return s;
  SetCatalog(std::move(c), path);
  return Status::Ok();
}

Status Desk::UseCatalogFile(const std::string& path, bool create) {
  if (!create) return OpenCatalog(path);
  auto c = std::make_shared<Catalog>();
  const Status s = c->Open(path);
  if (!s.ok()) return s;
  SetCatalog(std::move(c), path);
  return Status::Ok();
}

std::vector<std::string> Desk::ScanRoots() const {
  if (!catalog_) return {};
  std::string saved;
  if (catalog_->Meta(kScanRootsMetaKey, &saved).ok()) {
    std::vector<std::string> roots;
    size_t at = 0;
    while (at <= saved.size()) {
      const size_t nl = std::min(saved.find('\n', at), saved.size());
      if (nl > at) roots.push_back(saved.substr(at, nl - at));
      at = nl + 1;
    }
    return roots;
  }
  std::vector<DataSourceRow> sources;
  if (!catalog_->DataSources(&sources).ok()) return {};
  std::vector<std::pair<std::string, std::string>> paths;
  for (const DataSourceRow& r : sources) paths.emplace_back(r.path, r.format);
  return RootsFromSources(paths);
}

Status Desk::SaveScanRoots(const std::vector<std::string>& roots) {
  std::string text;
  for (const std::string& r : roots) text += r + "\n";
  return catalog_->SetMeta(kScanRootsMetaKey, text);
}

Status Desk::AddScanRoot(const std::string& path) {
  if (Building()) return Status::Error(kUnsupported, "a catalog build is running");
  if (!catalog_ || !IsCatalogFile(catalog_path_))
    return Status::Error(kInvalidArg, "open or create a catalog file first");
  const std::string root = AbsolutePath(path);
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec))
    return Status::Error(kNotFound, "no directory at " + root);
  std::vector<std::string> roots = ScanRoots();
  for (const std::string& r : roots) {
    if (PathWithin(root, r))
      return Status::Error(kInvalidArg, root + " is already scanned as part of " + r);
  }
  roots.erase(std::remove_if(roots.begin(), roots.end(),
                             [&](const std::string& r) { return PathWithin(r, root); }),
              roots.end());
  roots.push_back(root);
  std::sort(roots.begin(), roots.end());
  return SaveScanRoots(roots);
}

Status Desk::RemoveScanRoot(const std::string& path) {
  if (Building()) return Status::Error(kUnsupported, "a catalog build is running");
  if (!catalog_ || !IsCatalogFile(catalog_path_))
    return Status::Error(kInvalidArg, "open or create a catalog file first");
  std::vector<std::string> roots = ScanRoots();
  const auto it = std::find(roots.begin(), roots.end(), path);
  if (it == roots.end()) return Status::Error(kNotFound, path + " is not in the list");
  roots.erase(it);
  return SaveScanRoots(roots);
}

Status Desk::GenerateCoverage() {
  if (Building()) return Status::Error(kUnsupported, "a catalog build is already running");
  if (!catalog_ || !IsCatalogFile(catalog_path_))
    return Status::Error(kInvalidArg, "open or create a catalog file first");
  const std::vector<std::string> roots = ScanRoots();
  std::string missing;
  std::vector<ScanStep> steps;
  for (const std::string& root : roots) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
      missing += (missing.empty() ? "" : ", ") + root;
      continue;
    }
    for (ScanStep& step : PlanScan(root)) steps.push_back(std::move(step));
  }
  if (!missing.empty())
    return Status::Error(kNotFound, "cannot reach " + missing +
                                        "; reconnect it or remove it from Map Data Sources");
  if (!roots.empty() && steps.empty()) {
    std::string listed;
    for (const std::string& root : roots) listed += (listed.empty() ? "" : ", ") + root;
    return Status::Error(kNotFound, "found no map data folder (" + ScanFolderNames() + ") in " + listed);
  }
  // A list derived from the old data sources becomes the saved list.
  const Status s = SaveScanRoots(roots);
  if (!s.ok()) return s;
  build_had_data_ = !available_.empty();
  build_ = std::make_unique<CatalogBuild>(catalog_path_, std::move(steps), true);
  return Status::Ok();
}

void Desk::CancelCatalogBuild() {
  if (build_) build_->Cancel();
}

bool Desk::PollCatalogBuild() {
  if (!build_ || !build_->Progress().finished) return false;
  const std::string summary = build_->Summary();
  // The build thread logged these as they happened.
  for (const std::string& e : build_->Errors()) warnings_.push_back("catalog build: " + e);
  build_.reset();
  CatalogChanged();
  if (!build_had_data_ && CurrentGroup() != nullptr) Recenter();
  FV_LOG_INFO(summary);
  shell_.ShowNotice(summary);
  return true;
}

void Desk::WaitForCatalogBuild() {
  if (build_) build_->Wait();
  PollCatalogBuild();
}

void Desk::SetCatalog(std::shared_ptr<Catalog> catalog, const std::string& path) {
  catalog_ = std::move(catalog);
  catalog_path_ = path;
  CatalogChanged();
}

void Desk::CatalogChanged() {
  ResolveElevation();
  available_.clear();
  if (catalog_) available_ = groups_.WithData(CatalogFormats(*catalog_));
  if (CurrentGroup() == nullptr)
    group_id_ = available_.empty() ? std::string() : available_.front()->id;
  ApplyGroupToView();
  RegisterGroupCommands();
  RebuildMenus();
}

namespace {

/// Lidar DEMs stacked over the DTED tree; either may be absent.
std::unique_ptr<IElevationSource> OpenElevation(const std::string& dted_dir,
                                                const std::vector<std::string>& lidar_dirs) {
  std::unique_ptr<IElevationSource> dted;
  if (!dted_dir.empty()) dted = std::make_unique<DtedElevationSource>(dted_dir);
  if (lidar_dirs.empty()) return dted;
  auto lidar = std::make_shared<GeoTiffDemSource>(lidar_dirs);
  if (lidar->FileCount() == 0) return dted;
  return std::make_unique<ElevationStack>(
      std::vector<std::shared_ptr<IElevationSource>>{lidar, std::move(dted)});
}

}  // namespace

void Desk::ResolveElevation() {
  ++elevation_gen_;
  elevation_.reset();
  pointer_elevation_.reset();
  std::vector<DataSourceRow> rows;
  std::string dted_dir;
  std::vector<std::string> lidar_dirs;
  if (catalog_ && catalog_->DataSources(&rows).ok()) {
    for (const DataSourceRow& r : rows) {
      std::error_code ec;
      if (!std::filesystem::is_directory(r.path, ec)) continue;
      if (r.format == "dted" && dted_dir.empty()) dted_dir = r.path;
      if (r.format == "lidar" && r.frames > 0) lidar_dirs.push_back(r.path);
    }
  }
  elevation_ = OpenElevation(dted_dir, lidar_dirs);
  pointer_elevation_ = OpenElevation(dted_dir, lidar_dirs);
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays()) BindOverlay(*o);
}

bool Desk::BindOverlay(Overlay& overlay) {
  if (auto* r = dynamic_cast<RouteOverlay*>(&overlay)) {
    r->SetManager(&overlays_);
    r->SetPlanner(planner_.get());
    r->SetAvoidHillsPenalty(
        settings_.GetDouble("routing.avoid_hills_penalty", kDefaultAvoidHillsPenalty));
    // A new route is priced with the configured profile; an opened one keeps its own.
    if (r->waypoints().empty() && r->profile().empty() && !r->is_dirty()) {
      const std::string profile = settings_.GetString("routing.profile", "");
      if (!profile.empty()) {
        r->SetProfile(profile);
        r->set_dirty(false);
      }
    }
    return true;
  }
  if (auto* p = dynamic_cast<PointOverlay*>(&overlay)) {
    p->SetManager(&overlays_);
    return true;
  }
  if (auto* f = dynamic_cast<FeatureOverlay*>(&overlay)) {
    f->SetManager(&overlays_);
    f->edit().SetUndoStack(&undo_);
    f->SetLabelFont(HostLabelFont());
    f->SetDisplayScale(view_->View().DisplayScale());
    return true;
  }
  if (auto* c = dynamic_cast<ContourOverlay*>(&overlay)) {
    c->SetElevationSource(elevation_);
  } else if (auto* t = dynamic_cast<TAMaskOverlay*>(&overlay)) {
    t->SetElevationSource(elevation_);
  } else {
    return true;
  }
  return elevation_ != nullptr;
}

void Desk::DisplayScaleChanged() {
  const double scale = view_->View().DisplayScale();
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (auto* f = dynamic_cast<FeatureOverlay*>(o.get())) f->SetDisplayScale(scale);
}

Status Desk::SetLayerVisible(size_t row, bool visible) {
  Status s = layers_.SetVisible(row, visible);
  if (s.ok()) {
    ++layers_generation_;
    shell_.RequestInvalidate();
  }
  return s;
}

Status Desk::SetLayerOpacity(size_t row, int percent) {
  Status s = layers_.SetOpacity(row, percent);
  if (s.ok()) {
    ++layers_generation_;
    shell_.RequestInvalidate();
  }
  return s;
}

Status Desk::MoveLayer(size_t from, size_t to) {
  Status s = layers_.Move(from, to);
  if (s.ok()) shell_.RequestInvalidate();
  return s;
}

Status Desk::SelectLayer(size_t row) {
  if (!rule_id_.empty()) {
    rule_id_.clear();
    ++layers_generation_;
  }
  return layers_.Select(row);
}

Status Desk::SetSourceVisible(size_t source, bool visible) {
  Status s = sources_.SetVisible(source, visible);
  if (s.ok()) {
    ++layers_generation_;
    shell_.RequestInvalidate();
  }
  return s;
}

Status Desk::MoveSource(size_t from, size_t to) {
  Status s = sources_.Move(from, to);
  if (s.ok()) shell_.RequestInvalidate();
  return s;
}

Status Desk::SelectSource(size_t source) { return sources_.Select(source); }

void Desk::ZoomTo(const GeoRect& box) {
  const view::Viewport& v = view_->View();
  const GeoPoint center{(box.ll.lat + box.ur.lat) / 2, (box.ll.lon + box.ur.lon) / 2};
  // Metres on the ground over metres on the screen, with a tenth to spare.
  const double kMetersPerDegree = 111320.0;
  const double width_m = (box.ur.lon - box.ll.lon) * kMetersPerDegree * std::cos(center.lat * 3.14159265358979323846 / 180.0);
  const double height_m = (box.ur.lat - box.ll.lat) * kMetersPerDegree;
  const double screen_w = v.WidthPt() * v.MmPerPoint() / 1000.0;
  const double screen_h = v.HeightPt() * v.MmPerPoint() / 1000.0;
  double scale = 0;
  if (screen_w > 0 && screen_h > 0) scale = 1.1 * std::max(width_m / screen_w, height_m / screen_h);
  GoTo(center, scale > 0 ? scale : v.ScaleDenom());
}

std::shared_ptr<IEditableFeatureLayer> Desk::FeatureLayerOf(const std::shared_ptr<Overlay>& overlay) {
  if (auto f = std::dynamic_pointer_cast<FeatureOverlay>(overlay)) return f->layer();
  if (auto p = std::dynamic_pointer_cast<PointOverlay>(overlay)) return std::make_shared<PointsFeatureLayer>(p);
  return nullptr;
}

int64_t Desk::SelectedFeatureOf(const Overlay& overlay) {
  if (const auto* f = dynamic_cast<const FeatureOverlay*>(&overlay)) return f->selected();
  if (const auto* p = dynamic_cast<const PointOverlay*>(&overlay)) return p->selected();
  return 0;
}

bool Desk::SelectFeatureIn(Overlay& overlay, int64_t id) {
  if (auto* f = dynamic_cast<FeatureOverlay*>(&overlay)) f->SetSelected(id);
  else if (auto* p = dynamic_cast<PointOverlay*>(&overlay)) p->SetSelected(id);
  else return false;
  return true;
}

Status Desk::EditFeature(const Overlay* overlay, int64_t id) {
  const std::shared_ptr<Overlay> o = overlays_.FindShared(overlay);
  std::shared_ptr<IEditableFeatureLayer> layer = FeatureLayerOf(o);
  if (!layer) return Status::Error(kNotFound, "the overlay has no features to edit");
  Feature f;
  Status s = layer->Get(id, &f);
  if (!s.ok()) return s;
  const bool point_set = dynamic_cast<const PointOverlay*>(o.get()) != nullptr;
  std::string title;
  if (const auto* fo = dynamic_cast<const FeatureOverlay*>(o.get())) title = fo->FeatureTitle(f);
  else if (const auto* name = std::get_if<std::string>(&f.values[PointsFeatureLayer::kName])) title = *name;
  title = (point_set ? "Edit Point" : "Edit Feature") + (title.empty() ? "" : " — " + title);
  std::shared_ptr<FeatureFormModel> form;
  s = FeatureFormModel::Open(std::move(layer), id, std::move(title), o.get(), &form);
  if (!s.ok()) return s;
  form_ = std::move(form);
  ++feature_views_gen_;
  shell_.ShowFeatureForm();
  return Status::Ok();
}

Status Desk::EditSelectedFeature() {
  const Overlay* o = overlays_.current();
  const int64_t id = o != nullptr ? SelectedFeatureOf(*o) : 0;
  if (id == 0) return Status::Error(kNotFound, "select a feature to edit first");
  return EditFeature(o, id);
}

Status Desk::OpenAttributeTable() {
  const std::shared_ptr<Overlay> o = overlays_.FindShared(overlays_.current());
  std::shared_ptr<IEditableFeatureLayer> layer = FeatureLayerOf(o);
  if (!layer) return Status::Error(kNotFound, "the current overlay has no attribute table");
  std::shared_ptr<AttributeTableModel> table;
  Status s = AttributeTableModel::Open(std::move(layer), o->Name(), o.get(), &table);
  if (!s.ok()) return s;
  if (const auto feature = std::dynamic_pointer_cast<FeatureOverlay>(o)) {
    const std::weak_ptr<FeatureOverlay> weak = feature;
    table->SetFeatureFilter([weak](const Feature& f) {
      const std::shared_ptr<FeatureOverlay> fo = weak.lock();
      return !fo || fo->Shows(f);
    });
    if (!feature->definition_query().empty()) {
      s = table->Reload();
      if (!s.ok()) return s;
    }
  }
  table_ = std::move(table);
  ++feature_views_gen_;
  shell_.ShowAttributeTable();
  return Status::Ok();
}

Status Desk::ApplyFeatureForm() {
  if (!form_) return Status::Error(kNotFound, "no feature form is open");
  const std::shared_ptr<Overlay> o = overlays_.FindShared(form_->overlay());
  auto* features = dynamic_cast<FeatureOverlay*>(o.get());
  Feature before;
  const bool undoable = features != nullptr && form_->layer()->Get(form_->feature_id(), &before).ok();
  Status s = form_->Apply();
  if (!s.ok()) return s;
  if (undoable) features->edit().RecordEdit(before, "Edit Attributes");
  if (o && o->AsPersistence() != nullptr) o->AsPersistence()->set_dirty(true);
  if (table_ && table_->overlay() == form_->overlay()) {
    s = table_->Reload();
    if (!s.ok()) Warn("attribute table: " + s.message);
  }
  ++feature_views_gen_;
  shell_.RequestInvalidate();
  return Status::Ok();
}

void Desk::CloseFeatureForm() {
  if (!form_) return;
  form_.reset();
  ++feature_views_gen_;
}

void Desk::CloseAttributeTable() {
  if (!table_) return;
  table_.reset();
  ++feature_views_gen_;
}

void Desk::DropFeatureViews(const Overlay& overlay) {
  if (properties_overlay_ == &overlay) CloseLayerProperties();
  if (form_ && form_->overlay() == &overlay) CloseFeatureForm();
  if (table_ && table_->overlay() == &overlay) CloseAttributeTable();
  if (style_ && style_->overlay() == &overlay) CloseLayerStyle();
  if (fields_ && fields_->overlay() == &overlay) CloseLayerFields();
  if (select_query_ && select_query_->overlay() == &overlay) CloseSelectByAttributes();
  if (rule_filter_ && rule_filter_->overlay() == &overlay) CloseRuleFilter();
  if (rule_style_ && rule_style_->overlay() == &overlay) CloseRuleStyle();
}

Status Desk::OpenSelectByAttributes() {
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(overlays_.current()));
  if (!o || !o->layer()) return Status::Error(kNotFound, "the current overlay is not a feature layer");
  if (!select_query_ || select_query_->overlay() != o.get()) {
    std::shared_ptr<QueryBuilderModel> q;
    Status s = QueryBuilderModel::Open(o, std::string(), &q);
    if (!s.ok()) return s;
    select_query_ = std::move(q);
  }
  ++feature_views_gen_;
  shell_.ShowSelectByAttributes();
  return Status::Ok();
}

Status Desk::ApplySelectByAttributes(SelectionMethod method, size_t* selected) {
  if (!select_query_) return Status::Error(kNotFound, "Select by Attributes is not open");
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(select_query_->overlay()));
  if (!o) return Status::Error(kNotFound, "the layer has closed");
  std::vector<int64_t> matches;
  Status s = select_query_->Matching(true, &matches, nullptr);
  if (!s.ok()) return s;
  std::vector<int64_t> next;
  switch (method) {
    case SelectionMethod::kNew:
      next = std::move(matches);
      break;
    case SelectionMethod::kAdd:
      next = o->selection();
      next.insert(next.end(), matches.begin(), matches.end());
      break;
    case SelectionMethod::kRemove:
      for (int64_t id : o->selection())
        if (std::find(matches.begin(), matches.end(), id) == matches.end()) next.push_back(id);
      break;
    case SelectionMethod::kWithin:
      for (int64_t id : o->selection())
        if (std::find(matches.begin(), matches.end(), id) != matches.end()) next.push_back(id);
      break;
  }
  o->SetSelection(std::move(next));
  if (selected != nullptr) *selected = o->selection().size();
  ++feature_views_gen_;
  shell_.RequestInvalidate();
  return Status::Ok();
}

void Desk::CloseSelectByAttributes() {
  if (!select_query_) return;
  select_query_.reset();
  ++feature_views_gen_;
}

Status Desk::ClearSelection() {
  Overlay* o = overlays_.current();
  if (o == nullptr || !SelectFeatureIn(*o, 0)) return Status::Error(kNotFound, "the current layer has no features");
  ++feature_views_gen_;
  shell_.RequestInvalidate();
  return Status::Ok();
}

Status Desk::OpenLayerStyle() {
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(overlays_.current()));
  if (!o) return Status::Error(kNotFound, "the current overlay is not a feature layer");
  if (shell_.HostsPropertiesTabs() && o->layer() != nullptr) return OpenLayerProperties(PropertiesTab::kSymbology);
  std::shared_ptr<LayerStyleModel> style;
  Status s = LayerStyleModel::Open(o, 0, "Layer Style — " + o->Name(), &style);
  if (!s.ok()) return s;
  style_ = std::move(style);
  ++feature_views_gen_;
  shell_.ShowLayerStyle();
  return Status::Ok();
}

Status Desk::StyleSelectedFeature() {
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(overlays_.current()));
  if (!o || o->selected() == 0) return Status::Error(kNotFound, "select a feature to style first");
  Feature f;
  Status s = o->layer() ? o->layer()->Get(o->selected(), &f) : Status::Error(kNotFound, "no feature layer");
  if (!s.ok()) return s;
  std::shared_ptr<LayerStyleModel> style;
  s = LayerStyleModel::Open(o, o->selected(), "Feature Style — " + o->FeatureTitle(f), &style);
  if (!s.ok()) return s;
  style_ = std::move(style);
  ++feature_views_gen_;
  shell_.ShowLayerStyle();
  return Status::Ok();
}

Status Desk::ExportSelection() {
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(overlays_.current()));
  if (!o || o->selection().empty()) return Status::Error(kNotFound, "select features to export first");
  app::FileTypeDesc type;
  type.default_extension = FeatureOverlay::kExtension;
  type.open_filters = {{"GeoPackage (*.gpkg)", "*.gpkg"}};
  type.save_filters = type.open_filters;
  const size_t count = o->selection().size();
  const std::string what = std::to_string(count) + (count == 1 ? " feature" : " features");

  // An existing file is picked with the open panel, so the save panel never asks to replace it.
  std::vector<std::string> open_paths;
  for (const std::shared_ptr<Overlay>& layer : overlays_.OfType(FeatureOverlay::kTypeId)) {
    const auto& package = static_cast<const FeatureOverlay&>(*layer).package();
    if (package && package->Writable() &&
        std::find(open_paths.begin(), open_paths.end(), package->Path()) == open_paths.end())
      open_paths.push_back(package->Path());
  }
  std::vector<std::string> rows = {"New GeoPackage…", "Existing GeoPackage…"};
  for (const std::string& p : open_paths) rows.push_back(std::filesystem::path(p).filename().string());
  const std::optional<int> where = shell_.ChooseFromList("Export " + what + " to", rows);
  if (!where || *where < 0 || *where >= static_cast<int>(rows.size())) return Status::Ok();

  FeatureExport to;
  if (*where == 0) {
    to.path = shell_.ChooseSaveSpec(type, o->Name() + ".gpkg").first;
    if (to.path.empty()) return Status::Ok();
  } else {
    if (*where == 1) {
      const std::vector<std::string> paths = shell_.ChooseFilesToOpen(type);
      if (paths.empty()) return Status::Ok();
      to.path = paths.front();
    } else {
      to.path = open_paths[static_cast<size_t>(*where - 2)];
    }
    to.mode = FeatureExport::Mode::kNewTable;
    std::vector<GeoPackageLayerInfo> targets;
    Status s = o->AppendTargets(to.path, &targets);
    if (!s.ok()) return s;
    // Without a table of the same schema there is nothing to choose: it goes in as a new layer.
    if (!targets.empty()) {
      std::vector<std::string> choices = {"New layer \"" + o->Name() + "\""};
      for (const GeoPackageLayerInfo& t : targets) choices.push_back("Add to \"" + t.identifier + "\"");
      const std::optional<int> answer = shell_.ChooseFromList(
          "Export " + what + " to " + std::filesystem::path(to.path).filename().string(), choices);
      if (!answer || *answer < 0 || *answer >= static_cast<int>(choices.size())) return Status::Ok();
      if (*answer > 0) {
        to.mode = FeatureExport::Mode::kAppend;
        to.table = targets[static_cast<size_t>(*answer - 1)].table;
      }
    }
  }
  const std::string file = std::filesystem::path(to.path).filename().string();
  std::string spec;
  Status s = o->ExportSelection(to, &spec);
  if (!s.ok()) return s;
  SetFeedback("Exported " + std::to_string(count) + (count == 1 ? " feature to " : " features to ") +
              app::SplitSourceSpec(spec).layer + " in " + file);
  // An appended table may be on the map or in the attribute table.
  if (table_) {
    const Status st = table_->Reload();
    if (!st.ok()) Warn("attribute table: " + st.message);
  }
  ++feature_views_gen_;
  shell_.RequestInvalidate();
  return Status::Ok();
}

Status Desk::ApplyLayerStyle() {
  if (!style_) return Status::Error(kNotFound, "no style dialog is open");
  Status s = style_->Apply();
  if (!s.ok()) return s;
  const std::shared_ptr<Overlay> o = overlays_.FindShared(style_->overlay());
  if (o && o->AsPersistence() != nullptr) o->AsPersistence()->set_dirty(true);
  ++feature_views_gen_;
  shell_.RequestInvalidate();
  return Status::Ok();
}

void Desk::CloseLayerStyle() {
  if (!style_) return;
  style_.reset();
  ++feature_views_gen_;
}

Status Desk::OpenLayerFields() {
  const auto o = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(overlays_.current()));
  if (!o) return Status::Error(kNotFound, "the current overlay is not a feature layer");
  if (shell_.HostsPropertiesTabs() && o->layer() != nullptr) return OpenLayerProperties(PropertiesTab::kFields);
  std::shared_ptr<LayerFieldsModel> fields;
  Status s = LayerFieldsModel::Open(o, "Layer Fields — " + o->Name(), &fields);
  if (!s.ok()) return s;
  fields_ = std::move(fields);
  ++feature_views_gen_;
  shell_.ShowLayerFields();
  return Status::Ok();
}

std::string Desk::LayerFieldsLoss() const {
  if (!fields_) return std::string();
  SchemaChangeImpact impact;
  if (!fields_->Assess(&impact).ok()) return std::string();
  impact.missing_required.clear();
  std::string text = impact.Describe();
  for (const std::string& sentence : fields_->StyleBreaks()) text += (text.empty() ? "" : " ") + sentence;
  return text;
}

Status Desk::ApplyLayerFields() {
  if (!fields_) return Status::Error(kNotFound, "no Layer Fields dialog is open");
  Status s = fields_->Apply();
  if (!s.ok()) return s;
  const Overlay* id = fields_->overlay();
  const std::shared_ptr<Overlay> o = overlays_.FindShared(id);
  if (o && o->AsPersistence() != nullptr) o->AsPersistence()->set_dirty(true);
  if (form_ && form_->overlay() == id) CloseFeatureForm();
  if (PropertiesOwnsStyle() && style_->overlay() == id) ReopenPropertiesStyle();
  else if (style_ && style_->overlay() == id) CloseLayerStyle();
  if (table_ && table_->overlay() == id) {
    s = table_->Reload();
    if (!s.ok()) Warn("attribute table: " + s.message);
  }
  ++feature_views_gen_;
  shell_.RequestInvalidate();
  return Status::Ok();
}

void Desk::CloseLayerFields() {
  if (!fields_) return;
  fields_.reset();
  ++feature_views_gen_;
}

void Desk::OpenNewLayer(const std::string& package) {
  new_layer_ = std::make_shared<NewLayerModel>();
  if (!package.empty()) {
    new_layer_->SetTarget(NewLayerTarget::kPackage);
    new_layer_->SetPath(package);
  }
  ++feature_views_gen_;
  shell_.ShowNewLayer();
}

Status Desk::CreateNewLayer() {
  if (!new_layer_) return Status::Error(kNotFound, "no New Layer dialog is open");
  Status s = new_layer_->Validate();
  if (!s.ok()) return s;
  const std::string path = new_layer_->path();
  const NewLayerTarget target = new_layer_->target();
  if (target == NewLayerTarget::kUnsaved) {
    if (session_->NewFileOverlay(FeatureOverlay::kTypeId) != app::FlowResult::kDone) return session_->last_error();
    auto* made = dynamic_cast<FeatureOverlay*>(overlays_.current());
    if (made == nullptr) return Status::Error(kInternal, "the new layer is not current");
    made->SetName(new_layer_->name());
    made->SetLayer(std::make_shared<MemoryFeatureLayer>(new_layer_->name(), new_layer_->schema()));
    CloseNewLayer();
    ++layers_generation_;
    shell_.RequestInvalidate();
    return Status::Ok();
  }
  if (target == NewLayerTarget::kNewFile && overlays_.FindByFileSpec(FeatureOverlay::kTypeId, path))
    return Status::Error(kInvalidArg, path + " is open; close it before replacing it");
  std::string spec;
  s = new_layer_->Create(&spec);
  if (!s.ok()) return s;
  if (target == NewLayerTarget::kPackage) {
    // A layer opened by the bare path now shares the file; its spec names its own table.
    for (const std::shared_ptr<Overlay>& o : overlays_.OfType(FeatureOverlay::kTypeId))
      if (auto* f = dynamic_cast<FeatureOverlay*>(o.get()); f != nullptr && f->file_spec() == path)
        f->NameTableInSpec();
  }
  const app::FlowResult r = session_->OpenFile(FeatureOverlay::kTypeId, spec);
  if (r != app::FlowResult::kDone) {
    const Status& e = session_->last_error();
    return e.ok() ? Status::Error(kInternal, "opening " + spec + " was cancelled") : e;
  }
  CloseNewLayer();
  shell_.RequestInvalidate();
  return Status::Ok();
}

void Desk::CloseNewLayer() {
  if (!new_layer_) return;
  new_layer_.reset();
  ++feature_views_gen_;
}

Status Desk::SelectTableRow(size_t row) {
  if (!table_) return Status::Error(kNotFound, "no attribute table is open");
  const int64_t id = table_->FeatureAt(row);
  if (id == 0) return Status::Error(kInvalidArg, "no table row " + std::to_string(row));
  const std::shared_ptr<Overlay> o = overlays_.FindShared(table_->overlay());
  if (!o || !SelectFeatureIn(*o, id)) return Status::Error(kNotFound, "the table's overlay has closed");
  shell_.RequestInvalidate();
  return Status::Ok();
}

int Desk::SelectedTableRow() const {
  if (!table_) return -1;
  const std::shared_ptr<Overlay> o = overlays_.FindShared(table_->overlay());
  const int64_t id = o ? SelectedFeatureOf(*o) : 0;
  return id != 0 ? table_->RowOf(id) : -1;
}

Status Desk::EditTableRow(size_t row) {
  if (!table_) return Status::Error(kNotFound, "no attribute table is open");
  const int64_t id = table_->FeatureAt(row);
  if (id == 0) return Status::Error(kInvalidArg, "no table row " + std::to_string(row));
  Status s = SelectTableRow(row);
  if (!s.ok()) return s;
  return EditFeature(table_->overlay(), id);
}

void Desk::ResolveRoutePlanner() {
  const std::string graph = settings_.GetString("routing.graph", "");
  const std::string rules = settings_.GetString("routing.rules", "");
  const std::string paths = graph + '\n' + rules;
  const double penalty =
      settings_.GetDouble("routing.avoid_hills_penalty", kDefaultAvoidHillsPenalty);
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (auto* r = dynamic_cast<RouteOverlay*>(o.get())) r->SetAvoidHillsPenalty(penalty);
  if (planner_paths_ && *planner_paths_ == paths) return;
  planner_paths_ = paths;
  // The planner loads its graph lazily, on the first Follow Roads.
  std::unique_ptr<RoutePlanner> next;
  if (!graph.empty()) next = std::make_unique<RoutePlanner>(graph, rules);
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (auto* r = dynamic_cast<RouteOverlay*>(o.get())) r->SetPlanner(next.get());
  planner_ = std::move(next);
}

const MapGroup* Desk::CurrentGroup() const {
  for (const MapGroup* g : available_)
    if (g->id == group_id_) return g;
  return nullptr;
}

Status Desk::SelectGroup(const std::string& id) {
  if (groups_.Find(id) == nullptr) return Status::Error(kNotFound, "no map group '" + id + "'");
  if (std::none_of(available_.begin(), available_.end(),
                   [&id](const MapGroup* g) { return g->id == id; }))
    return Status::Error(kUnsupported, "map group '" + id + "' has no data in the catalog");
  if (id == group_id_) return Status::Ok();
  group_id_ = id;
  ApplyGroupToView();
  return Status::Ok();
}

void Desk::PanScreens(double fx, double fy) {
  const view::Viewport& v = view_->View();
  view_->SetViewport(v.Panned(-fx * v.WidthPt(), -fy * v.HeightPt()));
}

// MARK: Route elevation profile

Status Desk::OpenRouteProfile() {
  std::shared_ptr<Overlay> route;
  Overlay* edited = editors_->edited();
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (o.get() == edited && dynamic_cast<RouteOverlay*>(o.get())) route = o;
  if (!route && dynamic_cast<RouteOverlay*>(overlays_.current_ptr().get()))
    route = overlays_.current_ptr();
  if (!route || !RouteGeoPath(static_cast<const RouteOverlay&>(*route))) {
    const Status s = Status::Error(kNotFound, "The route needs two waypoints to profile.");
    Report(s);
    return s;
  }
  profile_route_ = route;
  profile_open_ = true;
  profile_key_.clear();
  UpdateRouteProfile();
  shell_.ShowRouteProfile();
  return Status::Ok();
}

void Desk::CloseRouteProfile() {
  profile_open_ = false;
  profile_route_.reset();
  profile_key_.clear();
  profile_.Clear(std::string());
}

bool Desk::UpdateRouteProfile() {
  if (!profile_open_) return false;
  const std::shared_ptr<Overlay> o = profile_route_.lock();
  auto* route = dynamic_cast<RouteOverlay*>(o.get());
  std::vector<size_t> waypoints;
  const std::optional<analysis::GeoPath> path =
      route ? RouteGeoPath(*route, &waypoints) : std::optional<analysis::GeoPath>();
  const analysis::RangeUnit units =
      ParseRangeUnit(settings_.GetString("analysis.units", "nm"));
  const int samples = settings_.GetInt("analysis.profile_samples", 200);

  // FNV-1a over everything the chart is sampled from.
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](const void* p, size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  };
  const int u = static_cast<int>(units);
  mix(&elevation_gen_, sizeof elevation_gen_);
  mix(&u, sizeof u);
  mix(&samples, sizeof samples);
  if (path) {
    const int type = static_cast<int>(path->line_type());
    mix(&type, sizeof type);
    for (const GeoPoint& p : path->points()) mix(&p, sizeof p);
  }
  std::string key(sizeof h + 1, '\0');
  key[0] = path ? 'p' : (route ? 'r' : 'n');
  std::memcpy(&key[1], &h, sizeof h);
  if (key == profile_key_) return false;
  profile_key_ = std::move(key);

  profile_.SetUnits(units);
  if (route == nullptr) {
    profile_.Clear("The route was closed.");
  } else if (!path) {
    profile_.Clear("Nothing to profile — the route needs two waypoints.");
  } else {
    profile_title_ = "Terrain Profile — " + (route->Name().empty() ? "Route" : route->Name());
    profile_.Sample(pointer_elevation_.get(), *path, samples, waypoints);
  }
  return true;
}

bool Desk::ElevationFeetAt(const GeoPoint& p, double* feet) {
  float meters = 0;
  if (!pointer_elevation_ || !pointer_elevation_->GetElevation(p, &meters).ok()) return false;
  *feet = meters / 0.3048;
  return true;
}

void Desk::GoTo(const GeoPoint& center, double scale_denom) {
  const view::Viewport& v = view_->View();
  view_->SetViewport(v.WithCamera(center, scale_denom, v.Rotation()));
  // Panning alone keeps the product; a jump chooses it again.
  ApplyGroupToView();
}

std::vector<DeskSearchRow> Desk::Search(const std::string& text, bool map_view_only) {
  ResolveRoutePlanner();
  const view::Viewport& v = view_->View();
  DeskSearchInput in;
  in.text = text;
  in.overlays = &overlays_;
  in.planner = planner_.get();
  in.catalog = catalog_;
  if (view_->HasProduct()) in.product = view_->Product();
  in.near = v.Center();
  if (v.HasSurface()) in.view = v.Projection().VmapBounds();
  in.map_view_only = map_view_only;
  in.enc_dir = CurrentVectorMapConfig().enc_dir;
  return search_->Search(in);
}

void Desk::GoToSearchRow(const DeskSearchRow& row) {
  constexpr double kNearestFrameDenom = 2500.0;
  const view::Viewport& v = view_->View();
  const double fit = SearchFitScale(row.bounds, v.WidthPt(), v.HeightPt(), v.MmPerPoint(),
                                    kNearestFrameDenom);
  if (fit > 0.0) {
    const GeoPoint mid{0.5 * (row.bounds.ll.lat + row.bounds.ur.lat),
                       0.5 * (row.bounds.ll.lon + row.bounds.ur.lon)};
    GoTo(mid, fit);
  } else {
    GoTo(row.position, v.ScaleDenom());
  }
}

std::vector<view::LadderProduct> Desk::ProductsAt(const GeoPoint& p) const {
  const MapGroup* g = CurrentGroup();
  if (g == nullptr || !catalog_) return {};
  return view::CatalogProductsAt(
      *catalog_, p, g->formats,
      [g](const std::string& format, const std::string& series_key) {
        return g->NominalScaleOf(format, series_key);
      });
}

void Desk::ApplyGroupToView() {
  const MapGroup* g = CurrentGroup();
  if (g == nullptr) {
    view_->SetGroup(view::LadderKind::kUniform, nullptr);
    return;
  }
  view_->SetGroup(g->ladder, [this](const GeoPoint& p) { return ProductsAt(p); },
                  g->uniform_factor);
}

// MARK: Workspace

Workspace Desk::CaptureWorkspace() {
  Workspace w;
  w.catalog_path = catalog_path_;
  if (const MapGroup* g = CurrentGroup()) w.map_group = g->id;
  if (view_->HasProduct()) {
    w.product_format = view_->Product().format;
    w.product_series_key = view_->Product().series_key;
  }
  const view::Viewport& v = view_->View();
  w.center = v.Center();
  w.scale_denom = v.ScaleDenom();
  w.rotation_deg = v.Rotation();
  w.projection = v.Type();

  const Status s = session_->SaveConfiguration(kWorkspaceConfig);
  if (s.ok()) {
    const std::string prefix = kWorkspacePrefix;
    const int count = settings_.GetInt(prefix + "count", 0);
    for (const std::string& key : settings_.Keys()) {
      if (key.compare(0, prefix.size(), prefix) != 0) continue;
      const std::string rest = key.substr(prefix.size());
      if (!IsStaleRow(rest, count)) w.overlays[rest] = settings_.GetString(key);
    }
  } else {
    Warn("overlay configuration: " + s.message);
  }
  // The session logged these as it recorded them.
  for (const std::string& line : session_->warnings()) warnings_.push_back(line);
  session_->ClearWarnings();
  w.active_editor = editors_->CurrentMode();
  return w;
}

app::FlowResult Desk::ApplyWorkspace(const Workspace& w) {
  const app::FlowResult closed = session_->CloseAll();
  if (closed != app::FlowResult::kDone) return closed;
  editors_->SetMode(app::TypeId());

  if (!w.catalog_path.empty() && w.catalog_path != catalog_path_) {
    const Status s = OpenCatalog(w.catalog_path);
    if (!s.ok()) Warn("catalog: " + s.message);
  }
  if (!w.map_group.empty()) {
    const Status s = SelectGroup(w.map_group);
    if (!s.ok()) Warn(s.message);
  }

  // The group is re-applied at the restored camera so the ladder chooses
  // there, then the saved camera and series are put back over its choice.
  const view::Viewport cam = view_->View()
                                 .WithProjectionType(w.projection)
                                 .WithCamera(w.center, w.scale_denom, w.rotation_deg);
  view_->SetViewport(cam);
  ApplyGroupToView();
  view_->SetViewport(cam);
  if (!w.product_format.empty()) {
    bool found = false;
    for (const view::LadderProduct& p : ProductsAt(w.center)) {
      if (p.format == w.product_format && p.series_key == w.product_series_key) {
        view_->SetProduct(p);
        found = true;
        break;
      }
    }
    if (!found)
      Warn("series " + w.product_format + " " + w.product_series_key +
                          " has no data at the saved view");
  }

  if (w.overlays.count("count")) {
    for (const auto& kv : w.overlays) settings_.Set(kWorkspacePrefix + kv.first, kv.second);
    const Status s = session_->RestoreConfiguration(kWorkspaceConfig);
    if (!s.ok()) Warn("overlays: " + s.message);
    // The session logged these as it recorded them.
    for (const std::string& line : session_->warnings()) warnings_.push_back(line);
    session_->ClearWarnings();
  }

  if (!w.active_editor.empty() &&
      editors_->SetMode(w.active_editor) != app::FlowResult::kDone)
    Warn("editor '" + w.active_editor + "' could not be resumed");
  return app::FlowResult::kDone;
}

app::FlowResult Desk::SaveWorkspace(const std::string& path) {
  const Status s = CaptureWorkspace().Save(path);
  return s.ok() ? app::FlowResult::kDone : Report(s);
}

app::FlowResult Desk::OpenWorkspace(const std::string& path) {
  Workspace w;
  const Status s = w.Load(path);
  if (!s.ok()) return Report(s);
  undo_.Clear();
  return ApplyWorkspace(w);
}

// MARK: Options

std::shared_ptr<OptionsModel> Desk::OverlayOptions() const {
  return std::make_shared<OptionsModel>(types_, settings_, overlay_options_);
}

Status Desk::ApplyOverlayOptions(OptionsModel& model) {
  const Status s = model.Apply(settings_, overlays_, user_settings_, &warnings_);
  shell_.RequestInvalidate();
  return s;
}

std::shared_ptr<OptionsModel> Desk::MapOptions() const {
  return std::make_shared<OptionsModel>(groups_, map_options_, settings_);
}

Status Desk::ApplyMapOptions(OptionsModel& model) {
  const bool changed = model.dirty();
  const Status s = model.Apply(settings_, overlays_, user_settings_, &warnings_);
  if (changed) {
    ++map_style_gen_;
    shell_.RequestInvalidate();
  }
  return s;
}

void Desk::RegisterMapOptions(MapOptionsSource source) {
  for (MapOptionsSource& m : map_options_) {
    if (m.group_id == source.group_id) {
      m = std::move(source);
      return;
    }
  }
  map_options_.push_back(std::move(source));
}

void Desk::SettingsLoaded() {
  ResolveRoutePlanner();
  for (const MapOptionsSource& m : map_options_) {
    if (!m.make || !m.apply) continue;
    std::unique_ptr<app::Properties> props = m.make();
    if (props == nullptr) continue;
    props->LoadFrom(settings_, m.prefix, &warnings_);
    m.apply(*props);
  }
  ++map_style_gen_;
  shell_.RequestInvalidate();
}

// MARK: Editing input

bool Desk::Editing() const { return !editors_->CurrentMode().empty(); }

namespace {
MouseEvent PixelEvent(const view::Viewport& v, double x_pt, double y_pt) {
  MouseEvent e;
  e.x = static_cast<int>(std::lround(x_pt * v.DisplayScale()));
  e.y = static_cast<int>(std::lround(y_pt * v.DisplayScale()));
  return e;
}
}  // namespace

bool Desk::OverlayPointerDown(double x_pt, double y_pt) { return OverlayPointerDown(x_pt, y_pt, false); }

bool Desk::OverlayPointerDown(double x_pt, double y_pt, bool shift) {
  return OverlayPointerDown(x_pt, y_pt, shift, 1);
}

bool Desk::OverlayPointerDown(double x_pt, double y_pt, bool shift, int clicks) {
  if (!Editing() && overlays_.mouse_capture() == nullptr) return false;
  MouseEvent e = PixelEvent(view_->View(), x_pt, y_pt);
  e.shift = shift;
  e.clicks = clicks;
  const bool used = overlays_.RouteMouseDown(e);
  UpdateEditHint(e);
  if (auto* f = dynamic_cast<FeatureOverlay*>(overlays_.current()); f != nullptr && f->edit().TakeOpenRequest()) {
    const Status st = EditSelectedFeature();
    if (!st.ok()) Report(st);
  }
  return used;
}

bool Desk::OverlayPointerDrag(double x_pt, double y_pt) {
  if (!Editing() && overlays_.mouse_capture() == nullptr) return false;
  return overlays_.RouteMouseMove(PixelEvent(view_->View(), x_pt, y_pt));
}

bool Desk::OverlayPointerUp(double x_pt, double y_pt) {
  if (!Editing() && overlays_.mouse_capture() == nullptr) return false;
  const MouseEvent e = PixelEvent(view_->View(), x_pt, y_pt);
  const bool used = overlays_.RouteMouseUp(e);
  UpdateEditHint(e);
  return used;
}

void Desk::OverlayHover(double x_pt, double y_pt) {
  const view::Viewport& v = view_->View();
  if (!v.HasSurface()) return;
  const MouseEvent e = PixelEvent(v, x_pt, y_pt);
  pick_->UpdateHover(v.Projection(), PixelPoint{e.x, e.y});
  UpdateEditHint(e);
}

app::MenuNode Desk::MapContextMenu(double x_pt, double y_pt) {
  const view::Viewport& v = view_->View();
  if (!v.HasSurface()) return app::MenuNode();
  const MouseEvent e = PixelEvent(v, x_pt, y_pt);
  const PixelPoint p{e.x, e.y};
  const MapProjection& proj = v.Projection();
  static const char kGetInfo[] = "Get Info\xE2\x80\xA6";
  std::vector<std::string> titles;
  app::MenuNode root = pick_->BuildContextMenu(proj, p, [&](Overlay& o, app::MenuNode& section) {
    app::ItemInfo* info = o.AsItemInfo();
    if (info == nullptr) return;
    std::optional<app::ItemInfoSheet> sheet = info->InfoAt(proj, p, pick_->tolerance_px);
    if (!sheet) return;
    const std::weak_ptr<Overlay> weak = overlays_.FindShared(&o);
    titles.push_back(sheet->title);
    app::MenuNode edit;
    if (!sheet->edit_label.empty()) {
      edit.label = sheet->edit_label;
      edit.action = [this, weak, select = sheet->select] {
        if (const auto sp = weak.lock()) {
          const Status s = EditItem(sp, select);
          if (!s.ok()) shell_.ReportError(s);
        }
      };
    }
    app::MenuNode get;
    get.label = kGetInfo;
    get.action = [this, weak, sheet = std::move(*sheet)] {
      if (weak.expired()) return;
      item_info_ = sheet;
      shell_.ShowItemInfo();
    };
    section.children.push_back(std::move(get));
    if (!edit.label.empty()) section.children.push_back(std::move(edit));
  });
  if (titles.size() > 1) {
    size_t k = 0;
    for (app::MenuNode& child : root.children) {
      if (child.label == kGetInfo && k < titles.size())
        child.label = "Get Info: " + titles[k++] + "\xE2\x80\xA6";
    }
  }
  return root;
}

Status Desk::EditItem(const std::shared_ptr<Overlay>& overlay, const std::function<void()>& select) {
  if (!overlay || !overlays_.Contains(overlay)) return Status::Error(kNotFound, "the layer has closed");
  // Current first: a mode entered afterwards adopts the current overlay of its
  // type, and one already active moves its focus (and the raise) to it.
  Status s = overlays_.MakeCurrent(overlay);
  if (!s.ok()) return s;
  if (editors_->CurrentMode() != overlay->type_id() &&
      editors_->SetMode(overlay->type_id()) != app::FlowResult::kDone) {
    return editors_->last_error();
  }
  if (select) select();
  shell_.RequestInvalidate();
  return Status::Ok();
}

void Desk::OverlayHoverExit() {
  pick_->ClearHover();
  edit_hint_.clear();
  ComposeHoverHint();
}

void Desk::UpdateEditHint(const MouseEvent& e) {
  std::string hint;
  if (Editing()) {
    if (Overlay* o = editors_->edited(); o != nullptr) {
      if (app::EditHint* h = o->AsEditHint()) hint = h->EditHintAt(PixelPoint{e.x, e.y});
    }
  }
  if (hint == edit_hint_) return;
  edit_hint_ = std::move(hint);
  ComposeHoverHint();
}

void Desk::SetOverlayHint(const app::HintText& hint) {
  overlay_hint_ = hint;
  ComposeHoverHint();
}

void Desk::SetMapHint(app::HintText hint) {
  if (hint.tool_tip == map_hint_.tool_tip && hint.status == map_hint_.status) return;
  map_hint_ = std::move(hint);
  ComposeHoverHint();
}

void Desk::ComposeHoverHint() {
  // The status bar has no floating tip, so a tool tip alone goes there too.
  auto line = [](const app::HintText& h) { return h.status.empty() ? h.tool_tip : h.status; };
  if (!edit_hint_.empty()) feedback_ = edit_hint_;
  else if (!overlay_hint_.empty()) feedback_ = line(overlay_hint_);
  else feedback_ = line(map_hint_);
  tool_tip_ = !overlay_hint_.empty() ? overlay_hint_.tool_tip : map_hint_.tool_tip;
}

bool Desk::OverlayKeyDown(const KeyEvent& e) {
  if (!Editing() && overlays_.mouse_capture() == nullptr) return false;
  return overlays_.RouteKeyDown(e);
}

// MARK: Status

StatusBar Desk::CurrentStatus() const {
  StatusBar bar;
  bar.feedback = feedback_;
  bar.scale = FormatScale(view_->View().ScaleDenom());
  if (view_->HasProduct())
    bar.product = view_->Product().format + " " + view_->Product().series_key;
  if (available_.empty()) {
    bar.message = "No map data in the catalog";
  } else if (view_->LastOutcome() == view::LadderOutcome::kEndOfLadder) {
    bar.message = "No further map at this point";
  } else if (!view_->HasProduct()) {
    const MapGroup* g = CurrentGroup();
    if (g != nullptr) bar.message = "No " + g->title + " map here";
  }
  return bar;
}

}  // namespace desk
}  // namespace fv
