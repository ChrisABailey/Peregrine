// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_host.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

#include "fv_desk_base_map.h"
#include "fv_desk_desk.h"
#include "fv_desk_user_settings.h"
#include "fv_view_scheduler.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "fvkit/settings.h"

void fv_desk_host_retain(fv::desk::DeskHost* host) {
  host->refs_.fetch_add(1, std::memory_order_relaxed);
}

void fv_desk_host_release(fv::desk::DeskHost* host) {
  if (host->refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete host;
}

namespace fv {
namespace desk {
namespace {

static_assert(kFieldBool == static_cast<int>(app::PropertyType::kBool) &&
                  kFieldColor == static_cast<int>(app::PropertyType::kColor) &&
                  kFieldPath == static_cast<int>(app::PropertyType::kPath),
              "HostFieldType mirrors PropertyType");

static_assert(kHostPrimary == kPrimary && kHostShift == kShift && kHostAlt == kAlt &&
                  kHostControl == kControl,
              "HostModifier mirrors Modifier");

/// The host's DeskShell: questions go to the native shell's request handler,
/// notifications are queued for the UI to poll.
class HostShell : public DeskShell {
 public:
  std::deque<std::string> errors;
  std::deque<std::string> notices;
  std::atomic<bool> invalidate{false};
  bool quit = false;

  std::shared_ptr<OptionsModel> options_shown[2];  // by HostOptionsKind
  int options_requested = -1;

  HostRequestFn handler = nullptr;
  void* handler_context = nullptr;
  HostRequest request;
  std::vector<std::pair<std::string, std::string>> items;
  bool answered = false;
  int answer_index = 0;
  std::vector<std::string> answer_paths;

  SaveAnswer AskSave(const std::string& name) override {
    HostRequest r;
    r.kind = kRequestAskSave;
    r.title = name;
    if (!Ask(r, {})) return SaveAnswer::kCancel;
    if (answer_index == 0) return SaveAnswer::kSave;
    if (answer_index == 1) return SaveAnswer::kDiscard;
    return SaveAnswer::kCancel;
  }
  std::vector<std::string> ChooseFilesToOpen(const app::FileTypeDesc& d) override {
    HostRequest r;
    r.kind = kRequestChooseOpen;
    r.directory = d.default_directory;
    r.multiple = true;
    if (!Ask(r, d.open_filters)) return {};
    return answer_paths;
  }
  std::pair<std::string, int> ChooseSaveSpec(const app::FileTypeDesc& d,
                                             const std::string& suggested) override {
    HostRequest r;
    r.kind = kRequestChooseSave;
    r.directory = d.default_directory;
    r.suggested_name = suggested;
    if (!Ask(r, d.save_filters) || answer_paths.empty()) return {std::string(), 0};
    return {answer_paths.front(), answer_index};
  }
  std::optional<int> ChooseFromList(const std::string& title,
                                    const std::vector<std::string>& rows) override {
    HostRequest r;
    r.kind = kRequestChooseFromList;
    r.title = title;
    std::vector<std::pair<std::string, std::string>> list;
    for (const std::string& row : rows) list.emplace_back(row, std::string());
    if (!Ask(r, list) || answer_index < 0 || answer_index >= static_cast<int>(rows.size()))
      return std::nullopt;
    return answer_index;
  }
  bool ConfirmRevert(const std::string& spec) override {
    HostRequest r;
    r.kind = kRequestConfirmRevert;
    r.title = spec;
    return Ask(r, {}) && answer_index == 1;
  }
  std::string ChooseDirectory(const std::string& title) override {
    HostRequest r;
    r.kind = kRequestChooseDirectory;
    r.title = title;
    if (!Ask(r, {}) || answer_paths.empty()) return std::string();
    return answer_paths.front();
  }
  void SetCursor(app::CursorId) override {}
  void ShowHint(const app::HintText&) override {}
  void ShowContextMenu(PixelPoint, const app::MenuNode&) override {}
  void RequestInvalidate() override { invalidate = true; }
  void OnEditorChanged(const app::TypeId&, app::OverlayEditor*) override {}
  void ReportError(const Status& s) override { errors.push_back(s.message); }
  void ShowNotice(const std::string& text) override { notices.push_back(text); }
  void MenusChanged() override {}
  void Quit() override { quit = true; }
  void ShowOverlayOptions(std::shared_ptr<OptionsModel> model) override {
    options_shown[kOptionsOverlay] = std::move(model);
    options_requested = kOptionsOverlay;
  }
  void ShowMapOptions(std::shared_ptr<OptionsModel> model) override {
    options_shown[kOptionsMap] = std::move(model);
    options_requested = kOptionsMap;
  }

 private:
  /// Raises `r` with the native shell; true when it answered.
  bool Ask(HostRequest r, std::vector<std::pair<std::string, std::string>> list) {
    if (handler == nullptr) return false;
    r.item_count = static_cast<int>(list.size());
    request = std::move(r);
    items = std::move(list);
    answered = false;
    answer_index = 0;
    answer_paths.clear();
    handler(handler_context);
    request = HostRequest();
    items.clear();
    return answered;
  }
};

/// Appends `items` to `out` in preorder, submenus first then their children.
void Flatten(const std::vector<MenuItem>& items, int depth, std::vector<HostMenuEntry>* out) {
  for (const MenuItem& m : items) {
    HostMenuEntry e;
    e.depth = depth;
    e.label = m.label;
    e.icon = m.icon;
    if (!m.children.empty()) {
      e.kind = kMenuSubmenu;
      out->push_back(std::move(e));
      Flatten(m.children, depth + 1, out);
      continue;
    }
    if (m.is_separator()) {
      e.kind = kMenuSeparator;
    } else {
      e.kind = kMenuCommand;
      e.id = m.command_id;
      e.key = m.shortcut.key;
      e.modifiers = m.shortcut.modifiers;
      e.checkable = m.checkable;
    }
    out->push_back(std::move(e));
  }
}

/// What the worker needs besides the viewport to draw one request.
struct Job {
  bool has_product = false;
  view::LadderProduct product;
  std::string catalog_path;
  uint64_t base_seq = 0;       ///< requests with the same value share a base map
  uint64_t overlay_epoch = 0;  ///< `Impl::overlay_epoch` when requested
  uint64_t map_style = 0;      ///< `Desk::MapStyleGeneration()` when requested
};

/// What decides the base map's pixels; a change starts a new base sequence.
struct BaseKey {
  uint64_t view_gen = 0;  ///< MapView generation: viewport and product
  std::string catalog_path;
  uint64_t catalog_epoch = 0;
  uint64_t map_style = 0;

  bool operator==(const BaseKey& o) const {
    return view_gen == o.view_gen && catalog_path == o.catalog_path &&
           catalog_epoch == o.catalog_epoch && map_style == o.map_style;
  }
  bool operator!=(const BaseKey& o) const { return !(*this == o); }
};

/// "32°48.123' N  079°54.456' W" — degrees and decimal minutes.
std::string FormatPosition(const GeoPoint& g) {
  auto part = [](double v, int deg_width, char pos, char neg) {
    const char hemi = v < 0 ? neg : pos;
    double a = std::fabs(v);
    int deg = static_cast<int>(a);
    double min = (a - deg) * 60.0;
    // Rounding to the printed precision must carry into the degrees.
    if (min >= 59.9995) {
      min = 0.0;
      ++deg;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*d\xC2\xB0%06.3f' %c", deg_width, deg, min, hemi);
    return std::string(buf);
  };
  return part(g.lat, 2, 'N', 'S') + "  " + part(g.lon, 3, 'E', 'W');
}

}  // namespace

struct DeskHost::Impl {
  Settings settings;
  UserSettings user;
  std::string user_path;  // "" until LoadSettings
  HostShell shell;
  std::unique_ptr<Desk> desk;

  uint64_t menus_gen = 0;  // the generation `menu_entries` was flattened from
  std::vector<HostMenuEntry> menu_entries;
  std::vector<HostMenuEntry> toolbar_entries;
  uint64_t ticked_menus_gen = 0;
  std::string ticked_catalog;
  std::string ticked_job;

  // UI thread: the newest request posted.
  bool requested_any = false;
  BaseKey requested_base;
  uint64_t requested_epoch = 0;
  uint64_t seq = 0;       // request number, the scheduler's generation
  uint64_t base_seq = 0;  // bumped when the base key changes
  uint64_t catalog_epoch = 0;
  int stack_depth = 0;    // nesting of StackEdit
  std::shared_ptr<const view::Frame> shown;
  std::string status_cache;

  // Read by the worker to abandon a render that a newer request overtook.
  std::atomic<uint64_t> latest_seq{0};
  std::atomic<uint64_t> latest_base_seq{0};
  // Bumped by the UI thread before it takes `stack_mu` to change overlays.
  std::atomic<uint64_t> overlay_epoch{0};
  // Held by the worker for the overlay pass and by the UI thread in Execute.
  std::mutex stack_mu;

  std::mutex jobs_mu;
  std::map<uint64_t, Job> jobs;  // by request number; pruned as frames render

  // Touched only by the render worker.
  BaseMapRenderer base;
  std::string worker_catalog_path;
  uint64_t worker_map_style = 0;
  PixelBuffer base_px;  // the base map of `base_px_seq`
  uint64_t base_px_seq = 0;
  bool base_px_valid = false;

  // Last, so it is destroyed (and its worker joined) before what it uses.
  std::unique_ptr<view::RenderScheduler> scheduler;

  class StackEdit;

  std::shared_ptr<const view::Frame> Render(const view::Viewport& v, uint64_t seq);
  std::string StatusLine() const;
  /// The page of the open dialog `kind`, or null.
  OptionsPage* OptionsPageAt(int kind, int page) const;
  /// Re-flattens the menus and toolbar when the model changed.
  void SyncMenus();
  std::string JobLine() const;
};

void DeskHost::Impl::SyncMenus() {
  const MenuModel& model = desk->menus();
  if (model.Generation() == menus_gen) return;
  menus_gen = model.Generation();
  menu_entries.clear();
  for (const Menu& m : model.Menus()) {
    HostMenuEntry top;
    top.kind = kMenuTop;
    top.id = m.id;
    top.label = m.title;
    menu_entries.push_back(std::move(top));
    Flatten(m.items, 1, &menu_entries);
  }
  toolbar_entries.clear();
  Flatten(model.Toolbar(), 0, &toolbar_entries);
}

std::string DeskHost::Impl::JobLine() const {
  const CatalogBuild* b = desk->catalog_build();
  if (b == nullptr) return std::string();
  const BuildProgress p = b->Progress();
  return std::to_string(p.done) + "/" + std::to_string(p.total) + " " + p.current;
}

std::shared_ptr<const view::Frame> DeskHost::Impl::Render(const view::Viewport& v, uint64_t seq) {
  Job job;
  {
    std::lock_guard<std::mutex> lock(jobs_mu);
    auto it = jobs.find(seq);
    if (it != jobs.end()) job = it->second;
    jobs.erase(jobs.begin(), jobs.upper_bound(seq));
  }
  // The worker keeps its own catalog connection; the UI thread's is not
  // shared across threads.
  if (job.catalog_path != worker_catalog_path) {
    worker_catalog_path = job.catalog_path;
    std::shared_ptr<Catalog> c;
    if (!job.catalog_path.empty()) {
      c = std::make_shared<Catalog>();
      if (!c->Open(job.catalog_path).ok()) c.reset();
    }
    base.SetCatalog(std::move(c));
    worker_map_style = job.map_style;
  }
  if (job.map_style != worker_map_style) {
    // Sources read map options when opened; a new engine reopens them.
    base.SetCatalog(base.catalog());
    worker_map_style = job.map_style;
  }

  if (!base_px_valid || base_px_seq != job.base_seq) {
    base_px_valid = false;
    CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
    canvas.Clear(FvColor{0, 0, 0, 255});
    if (job.has_product) {
      const auto stale = [this, &job] { return latest_base_seq.load() != job.base_seq; };
      // Any other failure keeps what was drawn; the next view retries.
      if (base.Render(v, job.product, canvas, stale).code == kInterrupted) return nullptr;
    }
    base_px = std::move(canvas.Buffer());
    base_px_seq = job.base_seq;
    base_px_valid = true;
  }

  CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  canvas.Buffer() = base_px;
  {
    const auto stale = [this, &job, seq] {
      return latest_seq.load() != seq || overlay_epoch.load() != job.overlay_epoch;
    };
    std::lock_guard<std::mutex> lock(stack_mu);
    if (stale()) return nullptr;
    // A failing overlay leaves the ones below it drawn, as the base map does.
    if (desk->overlays().DrawAll(v.Projection(), canvas, stale).code == kInterrupted)
      return nullptr;
  }

  auto f = std::make_shared<view::Frame>();
  f->viewport = v;
  f->generation = seq;
  f->width = v.PixelWidth();
  f->height = v.PixelHeight();
  const PixelBuffer& px = canvas.Buffer();
  f->rgba.assign(px.Data(), px.Data() + static_cast<size_t>(px.StrideBytes()) * px.Height());
  return f;
}

std::string DeskHost::Impl::StatusLine() const {
  const StatusBar bar = desk->CurrentStatus();
  return bar.scale + '\n' + bar.product + '\n' + bar.message + '\n';
}

/// Scope in which the UI thread may change the overlay stack: interrupts the
/// worker's overlay pass, then waits for it to let go. Nests.
class DeskHost::Impl::StackEdit {
 public:
  explicit StackEdit(Impl& m) : m_(m) {
    if (m_.stack_depth++ > 0) return;
    m_.overlay_epoch.fetch_add(1);
    m_.stack_mu.lock();
  }
  ~StackEdit() {
    if (--m_.stack_depth == 0) m_.stack_mu.unlock();
  }
  StackEdit(const StackEdit&) = delete;
  StackEdit& operator=(const StackEdit&) = delete;

 private:
  Impl& m_;
};

DeskHost* DeskHost::Create() { return new DeskHost(); }

DeskHost::DeskHost() : impl_(std::make_unique<Impl>()) {
  RegisterBuiltinFormats();
  impl_->desk = std::make_unique<Desk>(impl_->shell, impl_->settings);
  Impl* impl = impl_.get();
  impl_->scheduler = std::make_unique<view::RenderScheduler>(
      [impl](const view::Viewport& v, uint64_t gen) { return impl->Render(v, gen); },
      [](std::shared_ptr<const view::Frame>) {});
}

DeskHost::~DeskHost() {
  // Abandons the render in progress at its next check.
  impl_->latest_seq.store(UINT64_MAX);
  impl_->latest_base_seq.store(UINT64_MAX);
  impl_->scheduler.reset();
}

// MARK: Catalog and commands

std::string DeskHost::OpenCatalog(const std::string& path) {
  const Status s = impl_->desk->OpenCatalog(path);
  if (!s.ok()) return s.message;
  if (impl_->desk->CurrentGroup() != nullptr) return Execute("map.recenter");
  return std::string();
}

std::string DeskHost::CatalogPath() const { return impl_->desk->catalog_path(); }

void DeskHost::GoTo(double lat, double lon, double scale_denom) {
  impl_->desk->GoTo(GeoPoint{lat, lon}, scale_denom);
}

double DeskHost::CenterLat() const { return impl_->desk->map_view().View().Center().lat; }
double DeskHost::CenterLon() const { return impl_->desk->map_view().View().Center().lon; }
double DeskHost::ScaleDenom() const { return impl_->desk->map_view().View().ScaleDenom(); }

std::string DeskHost::Execute(const std::string& command_id) {
  Impl::StackEdit edit(*impl_);
  const Status s = impl_->desk->Execute(command_id);
  return s.ok() ? std::string() : s.message;
}

std::string DeskHost::TakeError() {
  if (impl_->shell.errors.empty()) return std::string();
  std::string e = std::move(impl_->shell.errors.front());
  impl_->shell.errors.pop_front();
  return e;
}

std::string DeskHost::TakeNotice() {
  if (impl_->shell.notices.empty()) return std::string();
  std::string n = std::move(impl_->shell.notices.front());
  impl_->shell.notices.pop_front();
  return n;
}

bool DeskHost::QuitRequested() const { return impl_->shell.quit; }

// MARK: Menus and toolbar

uint64_t DeskHost::MenuGeneration() const { return impl_->desk->menus().Generation(); }

int DeskHost::MenuEntryCount() const {
  impl_->SyncMenus();
  return static_cast<int>(impl_->menu_entries.size());
}

HostMenuEntry DeskHost::MenuEntryAt(int index) const {
  impl_->SyncMenus();
  if (index < 0 || index >= static_cast<int>(impl_->menu_entries.size())) return HostMenuEntry();
  return impl_->menu_entries[index];
}

int DeskHost::ToolbarEntryCount() const {
  impl_->SyncMenus();
  return static_cast<int>(impl_->toolbar_entries.size());
}

HostMenuEntry DeskHost::ToolbarEntryAt(int index) const {
  impl_->SyncMenus();
  if (index < 0 || index >= static_cast<int>(impl_->toolbar_entries.size()))
    return HostMenuEntry();
  return impl_->toolbar_entries[index];
}

bool DeskHost::IsEnabled(const std::string& command_id) const {
  return impl_->desk->commands().IsEnabled(command_id);
}

bool DeskHost::IsChecked(const std::string& command_id) const {
  return impl_->desk->commands().IsChecked(command_id);
}

// MARK: Questions

void DeskHost::SetRequestHandler(HostRequestFn fn, void* context) {
  impl_->shell.handler = fn;
  impl_->shell.handler_context = context;
}

HostRequest DeskHost::PendingRequest() const { return impl_->shell.request; }

std::string DeskHost::RequestItemLabel(int index) const {
  const auto& items = impl_->shell.items;
  return index >= 0 && index < static_cast<int>(items.size()) ? items[index].first
                                                              : std::string();
}

std::string DeskHost::RequestItemPattern(int index) const {
  const auto& items = impl_->shell.items;
  return index >= 0 && index < static_cast<int>(items.size()) ? items[index].second
                                                              : std::string();
}

void DeskHost::AnswerIndex(int index) {
  impl_->shell.answered = true;
  impl_->shell.answer_index = index;
}

void DeskHost::AnswerPath(const std::string& path) {
  impl_->shell.answered = true;
  impl_->shell.answer_paths.push_back(path);
}

// MARK: Options dialogs

namespace {

/// The page's field keys in section order.
std::vector<const OptionsField*> FieldsInOrder(const OptionsPage& page) {
  std::vector<const OptionsField*> out;
  for (const OptionsSection& sec : page.sections())
    for (const std::string& key : sec.keys)
      if (const OptionsField* f = page.Field(key)) out.push_back(f);
  return out;
}

}  // namespace

OptionsPage* DeskHost::Impl::OptionsPageAt(int kind, int page) const {
  if (kind != kOptionsMap && kind != kOptionsOverlay) return nullptr;
  const std::shared_ptr<OptionsModel>& model = shell.options_shown[kind];
  if (!model || page < 0 || page >= static_cast<int>(model->pages().size())) return nullptr;
  return model->pages()[static_cast<size_t>(page)].get();
}

int DeskHost::TakeOptionsRequest() {
  const int kind = impl_->shell.options_requested;
  impl_->shell.options_requested = -1;
  return kind;
}

bool DeskHost::OptionsOpen(int kind) const {
  return (kind == kOptionsMap || kind == kOptionsOverlay) &&
         impl_->shell.options_shown[kind] != nullptr;
}

int DeskHost::OptionsPageCount(int kind) const {
  if (!OptionsOpen(kind)) return 0;
  return static_cast<int>(impl_->shell.options_shown[kind]->pages().size());
}

std::string DeskHost::OptionsPageTitle(int kind, int page) const {
  const OptionsPage* p = impl_->OptionsPageAt(kind, page);
  return p ? p->title() : std::string();
}

std::string DeskHost::OptionsPageIcon(int kind, int page) const {
  const OptionsPage* p = impl_->OptionsPageAt(kind, page);
  return p ? p->icon() : std::string();
}

int DeskHost::OptionsFieldCount(int kind, int page) const {
  const OptionsPage* p = impl_->OptionsPageAt(kind, page);
  return p ? static_cast<int>(FieldsInOrder(*p).size()) : 0;
}

HostOptionsField DeskHost::OptionsFieldAt(int kind, int page, int field) const {
  HostOptionsField out;
  const OptionsPage* p = impl_->OptionsPageAt(kind, page);
  if (p == nullptr) return out;
  const std::vector<const OptionsField*> fields = FieldsInOrder(*p);
  if (field < 0 || field >= static_cast<int>(fields.size())) return out;
  const OptionsField& f = *fields[static_cast<size_t>(field)];
  out.key = f.spec.key;
  out.label = f.spec.label;
  out.section = f.spec.group;
  out.help = f.spec.help;
  out.type = static_cast<HostFieldType>(f.spec.type);
  out.value = f.value.ToString();
  out.changed = f.changed();
  out.min = f.spec.min;
  out.max = f.spec.max;
  out.choice_count = static_cast<int>(f.spec.choices.size());
  out.directory = f.spec.path_kind == app::PathKind::kDirectory;
  for (const auto& filter : f.spec.path_filters) {
    if (!out.path_patterns.empty()) out.path_patterns += ';';
    out.path_patterns += filter.second;
  }
  return out;
}

std::string DeskHost::OptionsChoiceLabel(int kind, int page, int field, int choice) const {
  const OptionsPage* p = impl_->OptionsPageAt(kind, page);
  if (p == nullptr) return std::string();
  const std::vector<const OptionsField*> fields = FieldsInOrder(*p);
  if (field < 0 || field >= static_cast<int>(fields.size())) return std::string();
  const std::vector<std::string>& choices = fields[static_cast<size_t>(field)]->spec.choices;
  if (choice < 0 || choice >= static_cast<int>(choices.size())) return std::string();
  return choices[static_cast<size_t>(choice)];
}

std::string DeskHost::SetOptionValue(int kind, int page, const std::string& key,
                                     const std::string& value) {
  OptionsPage* p = impl_->OptionsPageAt(kind, page);
  if (p == nullptr) return "no such options page";
  const OptionsField* f = p->Field(key);
  if (f == nullptr) return "no option '" + key + "' on " + p->title();
  app::PropertyValue v = f->value;
  if (!v.FromString(value)) return "'" + value + "' is not a valid " + f->spec.label;
  const Status s = p->Set(key, v);
  return s.ok() ? std::string() : s.message;
}

std::string DeskHost::ResetOptionsPage(int kind, int page) {
  OptionsPage* p = impl_->OptionsPageAt(kind, page);
  if (p == nullptr) return "no such options page";
  const Status s = p->ResetToDefaults();
  return s.ok() ? std::string() : s.message;
}

bool DeskHost::OptionsDirty(int kind) const {
  return OptionsOpen(kind) && impl_->shell.options_shown[kind]->dirty();
}

std::string DeskHost::ApplyOptions(int kind) {
  if (!OptionsOpen(kind)) return "the options dialog is not open";
  Impl& m = *impl_;
  OptionsModel& model = *m.shell.options_shown[kind];
  Status s;
  {
    Impl::StackEdit edit(m);
    s = kind == kOptionsMap ? m.desk->ApplyMapOptions(model)
                            : m.desk->ApplyOverlayOptions(model);
  }
  if (s.ok() && !m.user_path.empty() && m.user.dirty()) s = m.user.Save(m.user_path);
  return s.ok() ? std::string() : s.message;
}

void DeskHost::RevertOptions(int kind) {
  if (OptionsOpen(kind)) impl_->shell.options_shown[kind]->Revert();
}

void DeskHost::CloseOptions(int kind) {
  if (kind == kOptionsMap || kind == kOptionsOverlay) impl_->shell.options_shown[kind].reset();
}

// MARK: Settings

std::string DeskHost::LoadSettings(const std::string& user_settings_path) {
  Impl& m = *impl_;
  std::string error;
  Status s = m.settings.LoadDefault();
  if (!s.ok()) error = s.message;
  const std::string user_path =
      user_settings_path.empty() ? DefaultUserSettingsPath() : user_settings_path;
  s = m.user.Load(user_path);
  // A file that will not read is never overwritten; options then last the session.
  if (s.ok()) m.user_path = user_path;
  else if (error.empty()) error = s.message;
  m.user.ApplyTo(m.settings);
  m.desk->SetUserSettings(&m.user);
  m.desk->SettingsLoaded();
  return error;
}

// MARK: Background jobs

bool DeskHost::JobActive() const { return impl_->desk->Building(); }

double DeskHost::JobFraction() const {
  const CatalogBuild* b = impl_->desk->catalog_build();
  if (b == nullptr) return 0.0;
  const BuildProgress p = b->Progress();
  return p.total > 0 ? static_cast<double>(p.done) / p.total : 0.0;
}

std::string DeskHost::JobText() const {
  const CatalogBuild* b = impl_->desk->catalog_build();
  if (b == nullptr) return std::string();
  const BuildProgress p = b->Progress();
  if (p.current.empty()) return "Building the map catalog";
  return "Scanning " + p.current;
}

void DeskHost::CancelJob() { impl_->desk->CancelCatalogBuild(); }

// MARK: Surface and input

void DeskHost::Resize(double width_pt, double height_pt, double display_scale,
                      double mm_per_point) {
  impl_->desk->map_view().Resize(width_pt, height_pt, display_scale, mm_per_point);
}

void DeskHost::Hover(double x, double y) {
  impl_->desk->map_view().Hover(view::PointF{x, y});
}

void DeskHost::HoverExit() {
  impl_->desk->map_view().HoverExit();
}

void DeskHost::PointerDown(double x, double y) {
  impl_->desk->map_view().PointerDown(view::PointF{x, y});
}

void DeskHost::PointerDrag(double x, double y) {
  impl_->desk->map_view().PointerDrag(view::PointF{x, y});
}

void DeskHost::PointerUp(double x, double y) {
  impl_->desk->map_view().PointerUp(view::PointF{x, y});
}

void DeskHost::Scroll(double x, double y, double dx, double dy, bool precise) {
  impl_->desk->map_view().Scroll(view::PointF{x, y}, dx, dy, precise);
}

void DeskHost::MagnifyBegin(double x, double y) {
  impl_->desk->map_view().MagnifyBegin(view::PointF{x, y});
}

void DeskHost::Magnify(double x, double y, double factor) {
  impl_->desk->map_view().Magnify(view::PointF{x, y}, factor);
}

void DeskHost::MagnifyEnd(double x, double y) {
  impl_->desk->map_view().MagnifyEnd(view::PointF{x, y});
}

void DeskHost::Step(int direction) { impl_->desk->map_view().Step(direction); }

// MARK: Frames

HostTick DeskHost::Tick() {
  HostTick t;
  Impl& m = *impl_;
  if (m.desk->PollCatalogBuild()) {
    t.job_changed = true;
    ++m.catalog_epoch;  // the catalog's rows changed under the same path
  }
  std::string job = m.JobLine();
  if (job != m.ticked_job) {
    m.ticked_job = std::move(job);
    t.job_changed = true;
  }
  if (m.desk->menus().Generation() != m.ticked_menus_gen) {
    m.ticked_menus_gen = m.desk->menus().Generation();
    t.menus_changed = true;
  }
  if (m.shell.options_requested >= 0) t.options_requested = true;
  if (m.desk->catalog_path() != m.ticked_catalog) {
    m.ticked_catalog = m.desk->catalog_path();
    t.catalog_changed = true;
  }
  if (m.shell.invalidate.exchange(false)) {
    m.overlay_epoch.fetch_add(1);
    t.redraw = true;
  }
  const view::MapView& mv = m.desk->map_view();
  const BaseKey key{mv.Generation(), m.desk->catalog_path(), m.catalog_epoch,
                    m.desk->MapStyleGeneration()};
  const uint64_t epoch = m.overlay_epoch.load();
  if (mv.View().HasSurface() &&
      (!m.requested_any || key != m.requested_base || epoch != m.requested_epoch)) {
    if (!m.requested_any || key != m.requested_base) {
      ++m.base_seq;
      m.requested_base = key;
    }
    ++m.seq;
    Job job;
    job.has_product = mv.HasProduct();
    if (job.has_product) job.product = mv.Product();
    job.catalog_path = key.catalog_path;
    job.base_seq = m.base_seq;
    job.overlay_epoch = epoch;
    job.map_style = key.map_style;
    {
      std::lock_guard<std::mutex> lock(m.jobs_mu);
      m.jobs[m.seq] = std::move(job);
    }
    m.latest_base_seq.store(m.base_seq);
    m.latest_seq.store(m.seq);
    m.scheduler->Request(mv.View(), m.seq);
    m.requested_epoch = epoch;
    m.requested_any = true;
    t.redraw = true;
  }
  std::shared_ptr<const view::Frame> last = m.scheduler->LastFrame();
  if (last != m.shown) {
    m.shown = std::move(last);
    t.new_frame = true;
    t.redraw = true;
  }
  std::string status = m.StatusLine() + StatusPosition();
  if (status != m.status_cache) {
    m.status_cache = std::move(status);
    t.status_changed = true;
  }
  return t;
}

bool DeskHost::HasFrame() const { return impl_->shown != nullptr; }
int DeskHost::FrameWidth() const { return impl_->shown ? impl_->shown->width : 0; }
int DeskHost::FrameHeight() const { return impl_->shown ? impl_->shown->height : 0; }

const uint8_t* DeskHost::FramePixels() const {
  return impl_->shown && !impl_->shown->rgba.empty() ? impl_->shown->rgba.data() : nullptr;
}

bool DeskHost::CopyFrame(uint8_t* out, size_t capacity) const {
  const uint8_t* px = FramePixels();
  if (px == nullptr || out == nullptr || capacity < impl_->shown->rgba.size()) return false;
  std::memcpy(out, px, impl_->shown->rgba.size());
  return true;
}

double DeskHost::FrameDisplayScale() const {
  return impl_->shown ? impl_->shown->viewport.DisplayScale() : 1.0;
}

FramePlacement DeskHost::Placement() const {
  FramePlacement p;
  if (!impl_->shown) return p;
  view::Affine a;
  if (!view::PreviewTransform(impl_->shown->viewport, impl_->desk->map_view().View(), &a))
    return p;
  p.a = a.a;
  p.b = a.b;
  p.c = a.c;
  p.d = a.d;
  p.tx = a.tx;
  p.ty = a.ty;
  p.valid = true;
  return p;
}

void DeskHost::WaitForRender() { impl_->scheduler->WaitIdle(); }

uint64_t DeskHost::FramesRendered() const { return impl_->scheduler->FramesRendered(); }

// MARK: Status bar

std::string DeskHost::StatusScale() const { return impl_->desk->CurrentStatus().scale; }
std::string DeskHost::StatusProduct() const { return impl_->desk->CurrentStatus().product; }
std::string DeskHost::StatusMessage() const { return impl_->desk->CurrentStatus().message; }

std::string DeskHost::StatusPosition() const {
  const view::MapView& mv = impl_->desk->map_view();
  if (!mv.Hovering() || !mv.View().HasSurface()) return std::string();
  return FormatPosition(mv.View().GeoAt(mv.HoverPoint()));
}

}  // namespace desk
}  // namespace fv
