// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_desk.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "fv_desk_base_map.h"
#include "fv_desk_enc_import.h"
#include "fv_desk_map_options.h"
#include "fv_desk_overlay_manifest.h"
#include "fv_desk_feature_editor.h"
#include "fv_desk_moving_map_editor.h"
#include "fv_desk_point_editor.h"
#include "fv_desk_user_settings.h"
#include "fv_route_editor.h"
#include "fv_route_overlay.h"
#include "fv_road_network.h"
#include "fv_route_planner.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/app/overlay_host.h"
#include "fvkit/app/source_spec.h"
#include "fvkit/feature/feature_source.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/feature/points_layer.h"
#include "fvkit/formats/dted.h"
#include "fvkit/formats/elevation_stack.h"
#include "fvkit/formats/geotiff_dem.h"
#include "fvkit/log.h"
#include "fvkit/overlay/contour_overlay.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/overlay/ta_mask_overlay.h"
#include "fvkit/settings.h"

namespace fv {
namespace desk {
namespace {

/// Opacity of the overlays that are not being edited while an editor is active.
constexpr int kDimmedOverlayPercent = 50;
/// One press of Rotate Left or Rotate Right, in degrees.
constexpr double kRotateStepDeg = 15.0;
/// One press of Larger or Smaller Chart Symbols, and the range they keep to.
constexpr double kSymbolScaleStep = 1.25;
constexpr double kMinSymbolScale = 0.2, kMaxSymbolScale = 8.0;
constexpr char kSymbolScaleKey[] = "view.symbol_scale";
constexpr char kPlaybackRateKey[] = "ownship.playback_rate";

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

// MARK: Overlay host

/// The Desk's side of app::OverlayHost, shared by every HostAware overlay.
class Desk::OverlayHostImpl : public app::OverlayHost {
 public:
  explicit OverlayHostImpl(Desk& desk) : desk_(desk) {}

  void RequestRedraw() override { redraw_.store(true); }
  bool TakeRedraw() { return redraw_.exchange(false); }

  Status CaptureMouse(Overlay& overlay) override { return desk_.overlays_.CaptureMouse(&overlay); }
  void ReleaseMouse(Overlay& overlay) override {
    if (desk_.overlays_.mouse_capture() == &overlay) desk_.overlays_.ReleaseMouse();
  }
  bool ElevationAt(const GeoPoint& p, double* metres) override {
    float m = 0;
    if (!desk_.pointer_elevation_ || !desk_.pointer_elevation_->GetElevation(p, &m).ok())
      return false;
    *metres = m;
    return true;
  }
  std::shared_ptr<IElevationSource> OpenElevation() override;
  std::string LabelFont() const override { return HostLabelFont(); }
  double DisplayScale() const override { return desk_.view_->View().DisplayScale(); }
  void PushUndo(app::UndoEntry entry) override { desk_.undo_.Push(std::move(entry)); }
  app::UndoStack& History() override { return desk_.undo_; }
  std::vector<app::SnapToItem> SnapCandidates(const MapProjection& proj, PixelPoint p,
                                              double tolerance_px) override {
    return app::SnapCandidates(desk_.overlays_, proj, p, tolerance_px);
  }
  void ShowStatus(const std::string& text) override {
    desk_.tap_->ShowHint(app::HintText{std::string(), text});
  }
  std::vector<CoverageRow> Coverage(const std::string& format, const GeoRect& area) override {
    std::vector<CoverageRow> rows, out;
    if (!desk_.catalog_ || !desk_.catalog_->SelectByGeoRect(area, &rows).ok()) return out;
    for (CoverageRow& r : rows)
      if (r.format == format) out.push_back(std::move(r));
    return out;
  }
  bool OwnshipFix(PositionFix* out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ownship_.has_position) return false;
    *out = ownship_;
    return true;
  }
  void RaiseAlarm(const std::string& text) override {
    std::lock_guard<std::mutex> lock(mutex_);
    alarms_.push_back(text);
  }

  /// Copies the Desk's ownship for overlays reading it off the UI thread.
  void PublishOwnship(const OwnshipState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    ownship_ = state.has_position() ? state.fix : PositionFix();
  }
  /// The alarms raised since the last call, oldest first.
  std::vector<std::string> TakeAlarms() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(alarms_, {});
  }

 private:
  Desk& desk_;
  std::atomic<bool> redraw_{false};
  std::mutex mutex_;  ///< guards ownship_ and alarms_
  PositionFix ownship_;
  std::vector<std::string> alarms_;
};

// MARK: Stack hook

/// Re-lists the open file overlays when the stack or a file name changes.
class Desk::StackHook : public StackObserver {
 public:
  explicit StackHook(Desk& desk) : desk_(desk) {}
  void OverlayAdded(Overlay& o) override {
    desk_.NoteRecent(o);
    desk_.AttachMovingMap(o);
    // An elevation overlay with no source draws nothing, which reads as broken.
    const Status bound = desk_.BindOverlay(o);
    if (!bound.ok()) {
      const app::OverlayTypeDesc* d = desk_.types_.Find(o.type_id());
      const std::string what = d != nullptr ? d->display_name : o.Name();
      desk_.Report(Status::Error(bound.code, what + " " + bound.message));
    }
    Changed();
  }
  void OverlayRemoved(Overlay& o) override {
    if (app::HostAware* h = o.AsHostAware()) h->SetHost(nullptr);
    desk_.DetachMovingMap(o);
    desk_.DropFeatureViews(o);
    desk_.undo_.RemoveOwner(&o);
    Changed();
  }
  void OverlayFileSpecChanged(Overlay&) override { Changed(); }
  void OverlayOrderChanged() override {
    ++desk_.layers_generation_;
    desk_.SyncOwnship();
  }
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
  host_ = std::make_unique<OverlayHostImpl>(*this);
  search_ = std::make_unique<DeskSearch>();
  overlays_.SetTypeRegistry(&types_);
  session_ = std::make_unique<app::OverlaySession>(types_, overlays_, *tap_, settings_);
  SetReplayRate(settings_.GetDouble(kPlaybackRateKey, 1.0));
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
  // In the desktop a moving map is a document per feed, not fvkit's static toggle.
  if (s.ok()) s = types_.Replace(MovingMapFileType());
  if (s.ok()) s = RegisterMovingMapEditor(types_, *this);
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
  overlay_options_ = {RoutingOptions([this] { ResolveRoutePlanner(); }),
                      MovingMapOptions([this] { MovingMapOptionsChanged(); })};
  menus_.SetOnChange([this] { shell_.MenusChanged(); });
  RegisterStaticCommands();
  Refresh();
}

Desk::~Desk() {
  moving_maps_.clear();
  // An overlay a script still holds must not keep a pointer to this Desk.
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (app::HostAware* h = o->AsHostAware()) h->SetHost(nullptr);
  overlays_.RemoveObserver(stack_hook_.get());
  menus_.SetOnChange(nullptr);
}

void Desk::Refresh() {
  RegisterGroupCommands();
  RegisterOverlayTypeCommands();
  RegisterToolCommands();
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

void Desk::LoadPackages(const PackageDirs& dirs, const std::string& host_version) {
  packages_ = std::make_unique<PackageManager>(dirs, types_, tools_, host_version);
  packages_->Scan(&warnings_);
  packages_->LoadAll();
  for (const PackageInfo& p : packages_->packages())
    if (p.state == PackageState::kFailed || p.state == PackageState::kUnavailable)
      Warn(p.id + ": " + p.reason);
  Refresh();
}

Status Desk::LoadToolConfig(const std::string& path) {
  ToolConfig config;
  Status s = ReadToolConfig(path, &config);
  if (!s.ok()) {
    Warn(s.message);
    return s;
  }
  for (const ToolManifestEntry& e : config.tools) {
    const ToolRun* run = FindBuiltinTool(e.implementation_name);
    if (run == nullptr) {
      s = Status::Error(kNotFound, path + ": tool '" + e.desc.id + "': no builtin tool named '" +
                                       e.implementation_name + "'");
      Warn(s.message);
      continue;
    }
    if (tools_.Find(e.desc.id) != nullptr) continue;  // a second call
    ToolDesc d = e.desc;
    d.run = *run;
    const Status r = tools_.Register(std::move(d));
    if (!r.ok()) {
      s = r;
      Warn(r.message);
    }
  }
  tool_config_ = std::move(config);
  Refresh();
  return s;
}

Status Desk::InstallPackage(const std::string& path, InstallResult* result) {
  if (packages_ == nullptr) return Status::Error(kUnsupported, "packages are not loaded");
  const Status s = packages_->Install(path, result);
  if (s.ok() && result->live) Refresh();
  return s;
}

Status Desk::LinkPackage(const std::string& dir, InstallResult* result) {
  if (packages_ == nullptr) return Status::Error(kUnsupported, "packages are not loaded");
  const Status s = packages_->Link(dir, result);
  if (s.ok() && result->live) Refresh();
  return s;
}

Status Desk::SetPackageEnabled(const std::string& id, bool enabled) {
  if (packages_ == nullptr) return Status::Error(kUnsupported, "packages are not loaded");
  const Status s = packages_->SetEnabled(id, enabled);
  if (s.ok()) Refresh();
  return s;
}

Status Desk::RemovePackage(const std::string& id) {
  if (packages_ == nullptr) return Status::Error(kUnsupported, "packages are not loaded");
  return packages_->Remove(id);
}

void Desk::RegisterToolCommands() {
  commands_.RemovePrefix("tools.run.");
  for (const ToolDesc* t : tools_.All()) {
    if (tool_config_.Excludes(t->id)) continue;
    Command c;
    c.id = "tools.run." + t->id;
    c.label = t->title;
    c.icon = t->icon;
    c.submenu = t->submenu;
    const std::string id = t->id;
    c.action = [this, id] {
      const ToolDesc* tool = tools_.Find(id);
      if (tool == nullptr) return;
      ToolContext ctx(*this);
      const Status s = tool->run(ctx);
      if (!s.ok() && s.code != kInterrupted) Report(s);
    };
    const Status s = commands_.Register(std::move(c));
    if (!s.ok()) Warn(s.message);
  }
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
  add("tools.overlay_manager", "Overlay Manager…", "", [this] { shell_.ShowOverlayManager(); },
      [this] { return shell_.ProvidesOverlayManager(); });
  add("file.save_workspace", "Save Workspace…", "", [this] {
    const auto spec = shell_.ChooseSaveSpec(WorkspaceFileType(),
                                            std::string("Untitled.") + kWorkspaceExtension);
    if (!spec.first.empty()) SaveWorkspace(spec.first);
  });
  // An empty workspace stack falls back to the edited overlay's history, so
  // routes, points and plugin overlays undo from the Edit menu too.
  add("edit.undo", "Undo", "Primary+Z", [this] {
    if (!undo_.CanUndo()) {
      app::EditTarget* t = EditedHistory();
      if (t == nullptr || !t->CanUndo()) return;
      t->Undo();
      shell_.RequestInvalidate();
      return;
    }
    const Status st = undo_.Undo();
    if (!st.ok()) Report(st);
  }, [this] {
    if (undo_.CanUndo()) return true;
    const app::EditTarget* t = EditedHistory();
    return t != nullptr && t->CanUndo();
  }, nullptr, "undo");
  add("edit.redo", "Redo", "Primary+Shift+Z", [this] {
    if (!undo_.CanRedo()) {
      app::EditTarget* t = EditedHistory();
      if (t == nullptr || !t->CanRedo()) return;
      t->Redo();
      shell_.RequestInvalidate();
      return;
    }
    const Status st = undo_.Redo();
    if (!st.ok()) Report(st);
  }, [this] {
    if (undo_.CanRedo()) return true;
    const app::EditTarget* t = EditedHistory();
    return t != nullptr && t->CanRedo();
  }, nullptr, "redo");
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
  add("view.symbols_larger", "Larger Chart Symbols", "Primary+]",
      [this] { SetSymbolScale(symbol_scale_ * kSymbolScaleStep); },
      [this] { return symbol_scale_ < kMaxSymbolScale; });
  add("view.symbols_smaller", "Smaller Chart Symbols", "Primary+[",
      [this] { SetSymbolScale(symbol_scale_ / kSymbolScaleStep); },
      [this] { return symbol_scale_ > kMinSymbolScale; });
  add("view.ownship", "Ownship\xE2\x80\xA6", "Primary+Shift+M", [this] { shell_.ShowOwnship(); });
  add("view.symbols_actual", "Actual Symbol Size", "Primary+0", [this] { SetSymbolScale(1.0); },
      [this] { return symbol_scale_ != 1.0; });
  add("map.rotate.left", "Rotate Left", "Alt+Left", [this] { RotateChart(-kRotateStepDeg); });
  add("map.rotate.right", "Rotate Right", "Alt+Right", [this] { RotateChart(kRotateStepDeg); });
  add("map.rotate.north", "North Up", "Primary+Shift+Up",
      [this] { RotateChart(-view_->View().Rotation()); },
      [this] { return view_->View().Rotation() != 0.0; });

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
  add("overlay.add_web_layer", "Add Web Layer\xE2\x80\xA6", "", [this] { shell_.ShowAddWebLayer(); },
      [] { return AnyFeatureSourceLists(); });
  add("layers.refresh", "Refresh", "", [this] {
    const Status st = RefreshWebLayer();
    if (!st.ok()) Report(st);
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->remote_layer() != nullptr;
  });
  add("layers.reset_service_style", "Reset to Service Style", "", [this] {
    const Status st = ResetToServiceStyle();
    if (!st.ok()) Report(st);
  }, [this] {
    const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
    return f != nullptr && f->from_source();
  });
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

  // The ENC options page's buttons; not in any menu.
  auto can_import = [this] { return !Building() && IsCatalogFile(catalog_path_) && catalog_ != nullptr; };
  add("enc.import_zip", "Import ENC Zip…", "", [this] {
    app::FileTypeDesc zips;
    zips.default_extension = "zip";
    zips.open_filters = {{"ENC Zip Files (*.zip)", "*.zip"}};
    const std::vector<std::string> files = shell_.ChooseFilesToOpen(zips);
    if (files.empty()) return;
    const Status s = ImportEncCharts(files);
    if (!s.ok()) Report(s);
  }, can_import);
  add("enc.import_folder", "Import ENC Folder…", "", [this] {
    const std::string dir = shell_.ChooseDirectory("Choose an ENC_ROOT folder");
    if (dir.empty()) return;
    const Status s = ImportEncCharts({dir});
    if (!s.ok()) Report(s);
  }, can_import);
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
  const std::string scale = user->Get(kSymbolScaleKey);
  if (!scale.empty()) {
    char* end = nullptr;
    const double s = std::strtod(scale.c_str(), &end);
    if (end != scale.c_str() && std::isfinite(s) && s > 0)
      symbol_scale_ = std::clamp(s, kMinSymbolScale, kMaxSymbolScale);
  }
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
            c.icon = ResolveIconFile(n.icon, desc != nullptr ? desc->resource_dir : std::string());
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
    if (ScanReaches(r, root))
      return Status::Error(kInvalidArg, root + " is already scanned as part of " + r);
  }
  roots.erase(std::remove_if(roots.begin(), roots.end(),
                             [&](const std::string& r) { return ScanReaches(root, r); }),
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
  if (enc_import_) enc_import_->Cancel();
}

bool Desk::JobFinished() const {
  if (enc_import_) return enc_import_->Progress().finished;
  return build_ && build_->Progress().finished;
}

std::string Desk::EncChartsDir() const {
  const std::string dir = settings_.GetString(kEncChartsDirKey, "");
  if (!dir.empty()) return AbsolutePath(dir);
  const std::string def = DefaultChartsDir();
  return def.empty() ? def : AbsolutePath(def);
}

Status Desk::ImportEncCharts(const std::vector<std::string>& sources) {
  if (Building()) return Status::Error(kUnsupported, "a catalog build or import is already running");
  if (sources.empty()) return Status::Error(kInvalidArg, "nothing to import");
  if (!catalog_ || !IsCatalogFile(catalog_path_))
    return Status::Error(kInvalidArg, "open or create a map catalog first (Map ▸ Map Data Sources…)");
  const std::string charts = EncChartsDir();
  if (charts.empty()) return Status::Error(kInvalidArg, "set the imported charts directory first");
  // Cells install into <charts>/enc; a scan root that already reads that
  // folder would catalogue them twice.
  const std::string installs = (std::filesystem::path(charts) / "enc").string();
  bool listed = false;
  for (const std::string& r : ScanRoots()) {
    if (r == charts) listed = true;
    else if (ScanReaches(r, installs))
      return Status::Error(kInvalidArg, "the charts directory " + charts +
                                            " is inside a folder Generate Coverage reads from " +
                                            r + "; choose " + r + " or a directory outside it");
  }
  std::error_code ec;
  std::filesystem::create_directories(charts, ec);
  if (ec) return Status::Error(kIoError, "cannot create " + charts + ": " + ec.message());
  if (!listed) {
    const Status s = AddScanRoot(charts);
    if (!s.ok()) return s;
  }
  build_had_data_ = !available_.empty();
  enc_import_ = std::make_unique<EncImportJob>(sources, charts,
                                               settings_.GetString("enc.data_dir", ""));
  return Status::Ok();
}

bool Desk::PollCatalogBuild() {
  if (enc_import_) {
    if (!enc_import_->Progress().finished) return false;
    std::string summary = enc_import_->Summary();
    const bool installed = enc_import_->AnyInstalled();
    enc_import_.reset();
    std::vector<ScanStep> steps;
    if (installed)
      for (ScanStep& step : PlanScan(EncChartsDir()))
        if (step.format == "enc") steps.push_back(std::move(step));
    if (steps.empty()) {
      FV_LOG_INFO(summary);
      shell_.ShowNotice(summary);
      return true;
    }
    // The rescan replaces only the enc source's coverage; nothing else is touched.
    steps.front().keep_if_empty = true;
    import_summary_ = std::move(summary);
    build_ = std::make_unique<CatalogBuild>(catalog_path_, std::move(steps), false);
    return true;
  }
  if (!build_ || !build_->Progress().finished) return false;
  std::string summary = build_->Summary();
  if (!import_summary_.empty()) {
    summary = import_summary_ + "\n" + summary;
    import_summary_.clear();
  }
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
std::unique_ptr<IElevationSource> OpenElevationStack(const std::string& dted_dir,
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

std::shared_ptr<IElevationSource> Desk::OverlayHostImpl::OpenElevation() {
  return OpenElevationStack(desk_.elevation_dted_dir_, desk_.elevation_lidar_dirs_);
}

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
  elevation_ = OpenElevationStack(dted_dir, lidar_dirs);
  pointer_elevation_ = OpenElevationStack(dted_dir, lidar_dirs);
  elevation_dted_dir_ = dted_dir;
  elevation_lidar_dirs_ = lidar_dirs;
  RebindHosts();
}

Status Desk::BindOverlay(Overlay& overlay) {
  app::HostAware* h = overlay.AsHostAware();
  if (h == nullptr) return Status::Ok();
  h->SetHost(host_.get());
  // The road graph and routing settings are window state with no host service.
  if (auto* r = dynamic_cast<RouteOverlay*>(&overlay)) {
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
  }
  return h->HostStatus();
}

void Desk::RebindHosts() {
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (app::HostAware* h = o->AsHostAware()) h->SetHost(host_.get());
}

bool Desk::FlushOverlayRedraw() {
  for (const std::string& alarm : host_->TakeAlarms()) shell_.ShowNotice(alarm);
  if (!host_->TakeRedraw()) return false;
  shell_.RequestInvalidate();
  return true;
}

void Desk::DisplayScaleChanged() {
  RebindHosts();
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

std::vector<std::string> Desk::RecentWebUrls() const {
  return user_settings_ != nullptr ? desk::RecentWebUrls(*user_settings_) : std::vector<std::string>();
}

Status Desk::AddWebLayers() {
  const std::vector<std::string> specs = web_.ChosenSpecs();
  if (specs.empty()) return Status::Error(kInvalidArg, "tick the layers to add");
  NoteRecentWebUrl(user_settings_, web_.url());
  for (const std::string& spec : specs) {
    const app::FlowResult r = session_->OpenFile(FeatureOverlay::kTypeId, spec);
    if (r == app::FlowResult::kDone) continue;
    const Status& e = session_->last_error();
    return e.ok() ? Status::Error(kInternal, "opening " + spec + " was cancelled") : e;
  }
  shell_.RequestInvalidate();
  return Status::Ok();
}

Status Desk::RefreshWebLayer() {
  const auto* f = dynamic_cast<const FeatureOverlay*>(overlays_.current());
  web::RemoteFeatureLayer* remote = f != nullptr ? f->remote_layer() : nullptr;
  if (remote == nullptr) return Status::Error(kNotFound, "the current layer is not a web layer");
  const Status s = remote->Refresh();
  shell_.RequestInvalidate();
  return s;
}

Status Desk::ResetToServiceStyle() {
  const auto f = std::dynamic_pointer_cast<FeatureOverlay>(overlays_.FindShared(overlays_.current()));
  if (!f || !f->from_source()) return Status::Error(kNotFound, "the current layer did not come from a service");
  FeatureStyle style;
  if (!f->source_style_json().empty()) {
    const Status s = ReadFeatureStyleJson(f->source_style_json(), &style);
    if (!s.ok()) return s;
  }
  style.overrides = f->style().overrides;
  return rules_.Replace(f, style, "Reset to Service Style");
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
  moving_map_roads_.reset();
  moving_map_graph_ = nullptr;
  for (const auto& d : moving_maps_)
    d->SetRoadNetwork(d->overlay()->feed_config().snap_to_road ? MovingMapRoads() : nullptr);
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

void Desk::SetSymbolScale(double scale) {
  if (!std::isfinite(scale) || scale <= 0) return;
  scale = std::clamp(scale, kMinSymbolScale, kMaxSymbolScale);
  // Steps of 1.25 from 1 drift off it in floating point; land back on it.
  if (std::fabs(scale - 1.0) < 1e-9) scale = 1.0;
  if (scale == symbol_scale_) return;
  symbol_scale_ = scale;
  if (user_settings_ != nullptr) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.17g", scale);
    user_settings_->Set(kSymbolScaleKey, text);
  }
}

void Desk::RotateChart(double degrees) {
  const view::Viewport& v = view_->View();
  view_->SetViewport(v.RotatedBy(degrees, v.SurfaceCenter()));
}

app::EditTarget* Desk::EditedHistory() const {
  if (!Editing()) return nullptr;
  Overlay* o = editors_->edited();
  return o != nullptr ? o->AsEditTarget() : nullptr;
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

// MARK: Moving map

void Desk::AttachMovingMap(Overlay& overlay) {
  if (dynamic_cast<DeskMovingMap*>(&overlay) == nullptr) return;
  std::shared_ptr<DeskMovingMap> mm;
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (o.get() == &overlay) mm = std::static_pointer_cast<DeskMovingMap>(o);
  if (!mm) return;
  mm->SetSymbolDpiScale(view_->View().DisplayScale());
  auto driver = std::make_unique<MovingMapDriver>(settings_, feed_receivers_, mm);
  if (mm->feed_config().snap_to_road) driver->SetRoadNetwork(MovingMapRoads());
  const view::Viewport& v = view_->View();
  const Status s = driver->Start(v.Center(), v.Rotation(), MovingMapDemoPath());
  moving_maps_.push_back(std::move(driver));
  if (mm->replay()) ++clock_generation_;  // the range widened
  // An opened track is shown whole: the clock stops at its last fix.
  if (mm->replay()) {
    replay_clock_.playing = false;
    SeekReplay(mm->end_s());
  }
  SyncOwnship();
  if (!s.ok()) Report(s);
}

void Desk::DetachMovingMap(const Overlay& overlay) {
  const auto it = std::find_if(moving_maps_.begin(), moving_maps_.end(),
                               [&overlay](const auto& d) { return d->overlay() == &overlay; });
  if (it == moving_maps_.end()) return;
  const bool replay = (*it)->overlay()->replay();
  moving_maps_.erase(it);
  double start = 0.0;
  double end = 0.0;
  if (!ReplayRange(&start, &end)) replay_clock_.playing = false;
  if (replay) ++clock_generation_;
  SyncOwnship();
}

bool Desk::ReplayRange(double* start_s, double* end_s) const {
  bool any = false;
  for (const auto& d : moving_maps_) {
    const DeskMovingMap* mm = d->overlay();
    if (!mm->replay()) continue;
    *start_s = any ? std::min(*start_s, mm->start_s()) : mm->start_s();
    *end_s = any ? std::max(*end_s, mm->end_s()) : mm->end_s();
    any = true;
  }
  return any;
}

void Desk::SeekReplay(double t_s) {
  double start = 0.0;
  double end = 0.0;
  if (!ReplayRange(&start, &end)) return;
  replay_clock_.time_s = std::clamp(t_s, start, end);
  ++clock_generation_;
  for (const auto& d : moving_maps_) d->SetReplayTime(replay_clock_.time_s, true);
  shell_.RequestInvalidate();
}

void Desk::SetReplayPlaying(bool playing) {
  double start = 0.0;
  double end = 0.0;
  if (!ReplayRange(&start, &end)) playing = false;
  if (playing && replay_clock_.time_s >= end) SeekReplay(start);
  if (replay_clock_.playing == playing) return;
  replay_clock_.playing = playing;
  ++clock_generation_;
}

void Desk::SetReplayRate(double rate) {
  if (!(rate > 0.0) || rate == replay_clock_.rate) return;
  replay_clock_.rate = rate;
  ++clock_generation_;
  char text[32];
  std::snprintf(text, sizeof(text), "%.17g", rate);
  settings_.Set(kPlaybackRateKey, text);
  if (user_settings_ != nullptr) user_settings_->Set(kPlaybackRateKey, text);
}

OwnshipPanel Desk::ownship_panel() const {
  double start = 0.0;
  double end = 0.0;
  const bool has_range = ReplayRange(&start, &end);
  const std::string units = settings_.GetString("ownship.units", "nautical");
  OwnshipPanel p = BuildOwnshipPanel(
      ownship_, replay_clock_, has_range, start, end,
      units == "metric" ? OwnshipUnits::kMetric : OwnshipUnits::kNautical);
  p.generation = OwnshipPanelGeneration();
  return p;
}

MovingMapDriver* Desk::OwnshipDriver() const {
  // The topmost moving map is the ownship (FirstOfType is top-down).
  const std::shared_ptr<Overlay> top = overlays_.FirstOfType(MovingMapOverlay::kTypeId);
  for (const auto& d : moving_maps_)
    if (d->overlay() == top.get()) return d.get();
  return nullptr;
}

void Desk::SyncOwnship() {
  MovingMapDriver* owner = OwnshipDriver();
  const Overlay* current = ownship_.source.lock().get();
  if (owner != nullptr && owner->overlay() == current) return;
  if (owner == nullptr && ownship_.mode == OwnshipMode::kNone) return;
  const uint64_t generation = ownship_.generation + 1;
  ownship_ = OwnshipState();
  ownship_.generation = generation;
  host_->PublishOwnship(ownship_);
  if (owner == nullptr) return;
  DeskMovingMap* mm = owner->overlay();
  ownship_.mode = mm->replay() ? OwnshipMode::kReplay : OwnshipMode::kLive;
  ownship_.source = overlays_.FirstOfType(MovingMapOverlay::kTypeId);
  ownship_.source_name = mm->Name();
  // The new ownship's camera starts from the view as it is, not where it last left it.
  const view::Viewport& v = view_->View();
  owner->MapMoved(v.Center(), v.Rotation());
  // A paused replay sends no fix, so the view would not come to it until play.
  mm->RecheckCamera();
  if (mm->has_fix()) {
    MovingMapStep step;
    step.new_fix = true;
    step.fix = mm->last_fix();
    step.heading = owner->heading();
    UpdateOwnship(step);
  }
}

void Desk::UpdateOwnship(const MovingMapStep& step) {
  if (!step.new_fix) return;
  ownship_.fix = step.fix;
  ownship_.time_from_fix = step.fix.has_time;
  if (!step.fix.has_time) {
    ownship_.fix.time_s = UtcNowSeconds();
    ownship_.fix.has_time = true;
  }
  ownship_.heading = step.heading;
  ++ownship_.generation;
  host_->PublishOwnship(ownship_);
}

std::vector<GeoPoint> Desk::MovingMapDemoPath() const {
  std::vector<GeoPoint> path;
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays()) {
    const auto* r = dynamic_cast<const RouteOverlay*>(o.get());
    if (r == nullptr) continue;
    if (r->has_plan()) {
      for (const std::vector<GeoPoint>& leg : r->plan().legs) path.insert(path.end(), leg.begin(), leg.end());
    } else {
      for (const auto& w : r->waypoints()) path.push_back(w.position);
    }
    if (path.size() >= 2) break;
    path.clear();
  }
  return path;
}

std::shared_ptr<const IRoadNetwork> Desk::MovingMapRoads() {
  if (planner_ == nullptr) return nullptr;
  // Snapping is a refinement: a graph that will not load leaves the raw fixes.
  const Status s = planner_->EnsureGraph();
  if (!s.ok()) {
    Warn("moving map: " + s.message);
    return nullptr;
  }
  std::shared_ptr<const routing::RoadGraph> graph = planner_->graph();
  if (graph == nullptr) return nullptr;
  if (graph.get() != moving_map_graph_) {
    moving_map_roads_ = std::make_shared<routing::RoadGraphNetwork>(graph);
    moving_map_graph_ = graph.get();
  }
  return moving_map_roads_;
}

void Desk::MovingMapOptionsChanged() {
  SetReplayRate(settings_.GetDouble(kPlaybackRateKey, 1.0));
  for (const auto& d : moving_maps_) d->ApplySettings();
  shell_.RequestInvalidate();
}

bool Desk::SetMovingMapFeed(const Overlay& overlay, const FeedConfig& config) {
  const auto it = std::find_if(moving_maps_.begin(), moving_maps_.end(),
                               [&overlay](const auto& d) { return d->overlay() == &overlay; });
  if (it == moving_maps_.end()) return false;
  (*it)->overlay()->SetFeedConfig(config);
  if ((*it)->feed_stale()) RestartMovingMap(**it);
  return true;
}

void Desk::RestartMovingMap(MovingMapDriver& d) {
  d.SetRoadNetwork(d.overlay()->feed_config().snap_to_road ? MovingMapRoads() : nullptr);
  const view::Viewport& v = view_->View();
  const Status s = d.Start(v.Center(), v.Rotation(), MovingMapDemoPath());
  if (!s.ok()) Report(s);
  if (d.overlay()->replay()) d.SetReplayTime(replay_clock_.time_s, true);
  // A restarted ownship feed has no fix yet; the ownship waits for its first one.
  if (&d == OwnshipDriver()) {
    ownship_.source.reset();
    SyncOwnship();
  }
  shell_.RequestInvalidate();
}

DeskMovingMap* Desk::EditedMovingMap() const {
  if (auto* mm = dynamic_cast<DeskMovingMap*>(editors_->edited())) return mm;
  const MovingMapDriver* d = OwnshipDriver();
  return d != nullptr ? d->overlay() : nullptr;
}

void Desk::SetMovingMapFlag(const std::string& key, bool on) {
  const char* text = on ? "true" : "false";
  settings_.Set(key, text);
  if (user_settings_ != nullptr) user_settings_->Set(key, text);
  MovingMapOptionsChanged();
}

bool Desk::ToggleMovingMapListening() {
  DeskMovingMap* mm = EditedMovingMap();
  if (mm == nullptr || mm->replay()) return false;
  FeedConfig config = mm->feed_config();
  config.listening = !config.listening;
  return SetMovingMapFeed(*mm, config);
}

bool Desk::ToggleMovingMapSnap() {
  DeskMovingMap* mm = EditedMovingMap();
  if (mm == nullptr) return false;
  FeedConfig config = mm->feed_config();
  config.snap_to_road = !config.snap_to_road;
  return SetMovingMapFeed(*mm, config);
}

bool Desk::OpenMovingMapFeed() {
  DeskMovingMap* mm = EditedMovingMap();
  // A replay's track is its feed; there is nothing to choose.
  if (mm == nullptr || mm->replay()) return false;
  std::weak_ptr<Overlay> target;
  for (const std::shared_ptr<Overlay>& o : overlays_.Overlays())
    if (o.get() == mm) target = o;
  auto props = std::make_shared<FeedConfigProperties>(false, true);
  props->set_config(mm->feed_config());
  auto page = OptionsPage::ForObject(
      MovingMapOverlay::kTypeId, "Feed for " + mm->Name(), "movingmap", props,
      [this, target](const app::Properties& p) {
        const std::shared_ptr<Overlay> o = target.lock();
        const auto* feed = dynamic_cast<const FeedConfigProperties*>(&p);
        // The map may have closed while the dialog was open.
        if (o != nullptr && feed != nullptr) SetMovingMapFeed(*o, feed->config());
      });
  if (page == nullptr) return false;
  shell_.ShowFeedOptions(std::make_shared<OptionsModel>(std::move(page)));
  return true;
}

const MovingMapDriver* Desk::ownship_map() const { return OwnshipDriver(); }

bool Desk::TickMovingMap(double dt_s) {
  if (moving_maps_.empty()) return false;
  const view::Viewport& v = view_->View();
  if (!v.HasSurface()) return false;
  feed_receivers_.Poll(UtcNowSeconds());
  // A feed changed through its properties (a workspace, the Feed dialog) restarts here.
  for (const auto& d : moving_maps_)
    if (d->feed_stale()) RestartMovingMap(*d);
  if (replay_clock_.playing) {
    double start = 0.0;
    double end = 0.0;
    ReplayRange(&start, &end);
    replay_clock_.time_s = std::min(end, std::max(start, replay_clock_.time_s) +
                                             dt_s * replay_clock_.rate);
    if (replay_clock_.time_s >= end) replay_clock_.playing = false;
    ++clock_generation_;
    for (const auto& d : moving_maps_) d->SetReplayTime(replay_clock_.time_s, false);
  }
  SyncOwnship();
  MovingMapDriver* owner = OwnshipDriver();
  const bool user_moving = view_->InGesture() || view_->Coasting();
  const MapProjection& proj = v.Projection();
  bool redraw = false;
  MovingMapStep owner_step;
  for (const auto& d : moving_maps_) {
    const MovingMapStep step = d->Tick(proj, dt_s, d.get() == owner && !user_moving);
    redraw |= step.redraw;
    if (d.get() == owner) owner_step = step;
  }
  if (owner != nullptr) {
    UpdateOwnship(owner_step);
    if (owner_step.move) {
      view_->FollowTo(v.WithCamera(owner_step.center, v.ScaleDenom(), owner_step.rotation_deg));
      const view::Viewport& now = view_->View();
      owner->NoteApplied(now.Center(), now.Rotation());
    }
  }
  if (redraw) shell_.RequestInvalidate();
  return redraw;
}

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
  const MovingMapDriver* owner = OwnshipDriver();
  const std::string mm = owner != nullptr ? owner->StatusText() : std::string();
  if (!mm.empty()) bar.message = bar.message.empty() ? mm : bar.message + "   " + mm;
  const std::string web = WebLayersStatus(overlays_);
  if (!web.empty()) bar.message = bar.message.empty() ? web : bar.message + "   " + web;
  return bar;
}

}  // namespace desk
}  // namespace fv
