// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_desk.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "fv_desk_map_options.h"
#include "fv_desk_overlay_manifest.h"
#include "fvkit/app/capabilities.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/log.h"
#include "fvkit/settings.h"

namespace fv {
namespace desk {
namespace {

constexpr char kWorkspaceConfig[] = "workspace";
constexpr char kWorkspacePrefix[] = "session.workspace.";

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
  void ShowHint(const app::HintText& h) override { Shell().ShowHint(h); }
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
  void OverlayAdded(Overlay&) override { Changed(); }
  void OverlayRemoved(Overlay&) override { Changed(); }
  void OverlayFileSpecChanged(Overlay&) override { Changed(); }

 private:
  void Changed() {
    desk_.RegisterInstanceCommands();
    desk_.RebuildMenus();
  }
  Desk& desk_;
};

// MARK: Construction

Desk::Desk(DeskShell& shell, Settings& settings, MapGroups groups)
    : shell_(shell), settings_(settings), groups_(std::move(groups)), menus_(commands_) {
  tap_ = std::make_unique<ShellTap>(*this);
  overlays_.SetTypeRegistry(&types_);
  session_ = std::make_unique<app::OverlaySession>(types_, overlays_, *tap_, settings_);
  editors_ = std::make_unique<app::EditorManager>(types_, overlays_, *tap_);
  session_->SetEditorManager(editors_.get());
  editors_->SetSession(session_.get());
  stack_hook_ = std::make_unique<StackHook>(*this);
  overlays_.AddObserver(stack_hook_.get());

  const Status s = app::RegisterBuiltinOverlayTypes(types_);
  if (!s.ok()) Warn("built-in overlay types: " + s.message);

  view_ = std::make_unique<view::MapView>(view::Viewport::Make(GeoPoint{0, 0}, 1.0e7),
                                          view::LadderKind::kUniform, nullptr);
  map_options_ = BuiltinMapOptions();
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
  add("app.quit", "Quit", "Primary+Q", [this] {
    if (session_->Exit() == app::FlowResult::kDone) shell_.Quit();
  });

  add("map.zoom_in", "Zoom In", "Primary+=", [this] { view_->Step(+1); }, nullptr, nullptr,
      "zoom_in");
  add("map.zoom_out", "Zoom Out", "Primary+-", [this] { view_->Step(-1); }, nullptr, nullptr,
      "zoom_out");
  add("map.recenter", "Recenter on Data", "", [this] { Recenter(); },
      [this] { return CurrentGroup() != nullptr; }, nullptr, "recenter");

  size_t n = 0;
  const ProjectionType* types = AllProjectionTypes(&n);
  for (size_t i = 0; i < n; ++i) {
    const ProjectionType t = types[i];
    const std::string id = std::string("map.projection.") + ProjectionKey(t);
    add(id.c_str(), ProjectionTitle(t), "",
        [this, t] { view_->SetViewport(view_->View().WithProjectionType(t)); }, nullptr,
        [this, t] { return view_->View().Type() == t; });
  }

  add("overlay.open", "Open…", "Primary+O",
      [this] { session_->OpenFileOverlays(app::TypeId()); }, [this] {
        for (const app::OverlayTypeDesc* d : types_.All())
          if (d->file && !d->display_name.empty()) return true;
        return false;
      });
  add("map.options", "Options…", "", [this] { shell_.ShowMapOptions(MapOptions()); },
      [this] { return !map_options_.empty(); });
  add("overlay.options", "Options…", "",
      [this] { shell_.ShowOverlayOptions(OverlayOptions()); });
  RegisterCatalogCommands();
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

void Desk::RegisterInstanceCommands() {
  commands_.RemovePrefix("overlay.instance.");
  const auto& stack = overlays_.Overlays();
  int k = 0;
  // Top of the stack first, as the layers panel lists them.
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    const std::shared_ptr<Overlay>& o = *it;
    if (!types_.IsFile(o->type_id())) continue;
    std::string label = o->Name();
    if (app::Persistence* p = o->AsPersistence()) {
      if (!p->file_spec().empty())
        label = std::filesystem::path(p->file_spec()).filename().string();
    }
    Command c;
    c.id = "overlay.instance." + std::to_string(k++);
    c.label = label;
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
  if (!roots.empty() && steps.empty())
    return Status::Error(kUnsupported, "no map format this app can scan is registered");
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
  available_.clear();
  if (catalog_) available_ = groups_.WithData(CatalogFormats(*catalog_));
  if (CurrentGroup() == nullptr)
    group_id_ = available_.empty() ? std::string() : available_.front()->id;
  ApplyGroupToView();
  RegisterGroupCommands();
  RebuildMenus();
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

void Desk::GoTo(const GeoPoint& center, double scale_denom) {
  const view::Viewport& v = view_->View();
  view_->SetViewport(v.WithCamera(center, scale_denom, v.Rotation()));
  // Panning alone keeps the product; a jump chooses it again.
  ApplyGroupToView();
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
  return ApplyWorkspace(w);
}

// MARK: Options

std::shared_ptr<OptionsModel> Desk::OverlayOptions() const {
  return std::make_shared<OptionsModel>(types_, settings_);
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

// MARK: Status

StatusBar Desk::CurrentStatus() const {
  StatusBar bar;
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
