// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_host.h"

#include <algorithm>
#include <utility>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include "fv_desk_base_map.h"
#include "fv_desk_desk.h"
#include "fv_desk_user_settings.h"
#include "fv_enc_format.h"
#include "fv_osm_format.h"
#include "fv_view_scheduler.h"
#include "fvkit/canvas/cpu_canvas.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/engine.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/style/expression.h"
#include "fvkit/feature/value_text.h"
#include "fvkit/formats/registry.h"
#include "fvkit/log.h"
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

/// A shell's int row or index as a size_t; a negative one becomes 0.
size_t Index(int i) { return static_cast<size_t>(std::max(i, 0)); }

static_assert(kFieldBool == static_cast<int>(app::PropertyType::kBool) &&
                  kFieldColor == static_cast<int>(app::PropertyType::kColor) &&
                  kFieldPath == static_cast<int>(app::PropertyType::kPath),
              "HostFieldType mirrors PropertyType");

static_assert(unsigned{kHostPrimary} == unsigned{kPrimary} &&
                  unsigned{kHostShift} == unsigned{kShift} &&
                  unsigned{kHostAlt} == unsigned{kAlt} &&
                  unsigned{kHostControl} == unsigned{kControl},
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
  bool sources_requested = false;
  bool profile_requested = false;
  bool layers_requested = false;
  bool layers_hide_requested = false;
  bool find_requested = false;
  bool form_requested = false;
  bool table_requested = false;
  bool style_requested = false;
  bool fields_requested = false;
  bool new_layer_requested = false;
  bool properties_requested = false;
  bool info_requested = false;
  bool properties_tabs = false;
  bool select_requested = false;
  bool add_rule_requested = false;
  bool rule_rename_requested = false;
  bool rule_type_requested = false;
  bool rule_filter_requested = false;
  bool rule_style_requested = false;

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
  void ShowDataSources() override { sources_requested = true; }
  void ShowRouteProfile() override { profile_requested = true; }
  void ShowLayers() override { layers_requested = true; }
  void HideLayers() override { layers_hide_requested = true; }
  void ShowFind() override { find_requested = true; }
  void ShowFeatureForm() override { form_requested = true; }
  void ShowAttributeTable() override { table_requested = true; }
  void ShowLayerStyle() override { style_requested = true; }
  void ShowLayerFields() override { fields_requested = true; }
  void ShowNewLayer() override { new_layer_requested = true; }
  void ShowLayerProperties() override { properties_requested = true; }
  void ShowItemInfo() override { info_requested = true; }
  bool HostsPropertiesTabs() const override { return properties_tabs; }
  void ShowSelectByAttributes() override { select_requested = true; }
  void ShowAddRule() override { add_rule_requested = true; }
  void BeginRuleRename() override { rule_rename_requested = true; }
  void ShowRuleType() override { rule_type_requested = true; }
  void ShowRuleFilter() override { rule_filter_requested = true; }
  void ShowRuleStyle() override { rule_style_requested = true; }
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
  bool pan = false;            ///< a band drawn while the map pans
};

/// A pan frame draws the overlays only when the last full overlay pass took
/// less than this; slower overlays wait for the pan to stop.
constexpr double kPanOverlayBudget = 0.04;

/// What decides the base map's pixels; a change starts a new base sequence.
struct BaseKey {
  uint64_t view_gen = 0;  ///< MapView frame generation: all but the centre, and the product
  std::string catalog_path;
  uint64_t catalog_epoch = 0;
  uint64_t map_style = 0;

  bool operator==(const BaseKey& o) const {
    return view_gen == o.view_gen && catalog_path == o.catalog_path &&
           catalog_epoch == o.catalog_epoch && map_style == o.map_style;
  }
  bool operator!=(const BaseKey& o) const { return !(*this == o); }
};

/// The viewport drawn while the map pans: `live` grown by `margin` of its
/// width and height on each side, its centre led by `lead` points the way
/// the content moves from, so the side being uncovered has the most map.
view::Viewport PanBand(const view::Viewport& live, double margin, const view::PointF& lead) {
  const double mx = std::ceil(live.WidthPt() * margin), my = std::ceil(live.HeightPt() * margin);
  const view::Viewport band = live.WithSurface(live.WidthPt() + 2 * mx, live.HeightPt() + 2 * my,
                                               live.DisplayScale(), live.MmPerPoint());
  const double lx = std::clamp(lead.x, -0.75 * mx, 0.75 * mx);
  const double ly = std::clamp(lead.y, -0.75 * my, 0.75 * my);
  return band.Panned(lx, ly);
}

/// True when the frame drawn for `drawn` still covers the screen of `live`
/// once the content has moved by `ahead` points.
bool FrameCovers(const view::Viewport& drawn, const view::Viewport& live, const view::PointF& ahead) {
  const double w = live.WidthPt(), h = live.HeightPt();
  const view::PointF corners[] = {{0, 0}, {w, 0}, {0, h}, {w, h}};
  for (const view::PointF& c : corners) {
    view::PointF q;
    if (!drawn.PointFor(live.GeoAt(view::PointF{c.x - ahead.x, c.y - ahead.y}), &q)) return false;
    if (q.x < 0 || q.y < 0 || q.x > drawn.WidthPt() || q.y > drawn.HeightPt()) return false;
  }
  return true;
}

/// "1,234 ft", rounded to the foot, thousands separated.
std::string FormatFeet(double feet) {
  const long long n = std::llround(feet);
  std::string digits = std::to_string(n < 0 ? -n : n);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(i, ",");
  return (n < 0 ? "-" : "") + digits + " ft";
}

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

namespace {

/// One entry of the map's right-click menu, with its action.
struct MapMenuEntry {
  HostMenuEntry entry;
  bool enabled = true;
  bool checked = false;
  std::function<void()> action;
};

}  // namespace

struct DeskHost::Impl {
  Settings settings;
  UserSettings user;
  std::string user_path;  // "" until LoadSettings
  uint64_t saved_user_gen = 0;  // the user settings' generation last on disk
  HostShell shell;
  std::unique_ptr<Desk> desk;

  uint64_t menus_gen = 0;  // the generation `menu_entries` was flattened from
  std::vector<HostMenuEntry> menu_entries;
  std::vector<HostMenuEntry> toolbar_entries;
  uint64_t ticked_menus_gen = 0;
  std::string ticked_catalog;
  ProfileLayout profile_layout;
  double profile_w = 0, profile_h = 0;
  std::string ticked_job;

  // UI thread: the newest request posted.
  bool requested_any = false;
  BaseKey requested_base;
  view::Viewport requested_view;  ///< the viewport of the newest request
  bool requested_pan = false;     ///< the newest request is a pan band
  uint64_t ticked_view_gen = 0;   ///< MapView generation at the last Tick
  uint64_t requested_epoch = 0;
  uint64_t seq = 0;       // request number, the scheduler's generation
  uint64_t base_seq = 0;  // bumped when the base key changes
  uint64_t catalog_epoch = 0;
  int stack_depth = 0;    // nesting of StackEdit
  std::vector<DeskSearchRow> search_rows;  // the last Find search
  std::string search_status;
  std::vector<LayerRow> layer_rows;  // the panel's snapshot of `Desk::layers()`
  std::vector<SourceRow> source_rows;  // `Desk::sources()` at the same time
  uint64_t layers_gen = UINT64_MAX;  // the generation `layer_rows` was taken at
  uint64_t ticked_features_gen = 0;  // `Desk::FeatureViewsGeneration()` at the last Tick
  int ticked_table_row = -1;         // `Desk::SelectedTableRow()` at the last Tick
  std::shared_ptr<const view::Frame> shown;
  std::string status_cache;

  // Read by the worker to abandon a render that a newer request overtook.
  std::atomic<uint64_t> latest_seq{0};
  std::atomic<uint64_t> latest_base_seq{0};
  /// Seconds the last base map took to draw; how far ahead a pan band leads.
  std::atomic<double> draw_seconds{0.1};
  /// Seconds the last overlay pass took, or ran before it was interrupted.
  std::atomic<double> overlay_seconds{0.0};
  // Bumped by the UI thread before it takes `stack_mu` to change overlays.
  std::atomic<uint64_t> overlay_epoch{0};
  // Held by the worker for the overlay pass and by the UI thread in Execute.
  std::mutex stack_mu;
  /// The pointer press in progress went to an overlay, not the pan.
  bool pointer_on_overlay = false;

  std::mutex jobs_mu;
  std::map<uint64_t, Job> jobs;  // by request number; pruned as frames render

  /// Why the base map is incomplete ("" when it drew fully); written by the
  /// worker, read by the status bar.
  mutable std::mutex base_note_mu;
  std::string base_note;

  std::shared_ptr<FileLogSink> log_file;
  std::vector<int> log_sinks;  // ids to remove with the host

  /// Held by the worker while it draws the base map, and by a hover that
  /// reads what it drew; guards `base` and `base_view`.
  std::mutex base_mu;
  view::Viewport base_view;  ///< the viewport `base` last drew at
  bool have_base_view = false;
  /// Last hover point, in points; the base hint is re-read when a frame lands.
  bool have_hover = false;
  double hover_x = 0, hover_y = 0;

  // Touched only by the render worker, except under `base_mu`.
  BaseMapRenderer base;
  std::string worker_catalog_path;
  std::set<std::string> logged_skips;  // unreadable files already logged for this catalog
  uint64_t worker_map_style = 0;
  PixelBuffer base_px;  // the base map of `base_px_seq`
  uint64_t base_px_seq = 0;
  bool base_px_valid = false;

  // Last, so it is destroyed (and its worker joined) before what it uses.
  std::unique_ptr<view::RenderScheduler> scheduler;

  class StackEdit;
  /// The map menu `OpenMapMenu` built, flattened.
  std::vector<MapMenuEntry> map_menu;
  /// Re-reads `layer_rows` when the Desk's layer generation moved; true when it did.
  bool RefreshLayers();
  /// Runs a rule edit with the stack held, then re-reads the rows; the error message or "".
  template <typename Fn>
  std::string EditRules(Fn&& fn);
  const LayerRow* Row(int row) const {
    return row >= 0 && row < static_cast<int>(layer_rows.size()) ? &layer_rows[row] : nullptr;
  }
  const SourceRow* Source(int source) const {
    return source >= 0 && source < static_cast<int>(source_rows.size()) ? &source_rows[source] : nullptr;
  }

  std::shared_ptr<const view::Frame> Render(const view::Viewport& v, uint64_t seq);
  std::string StatusLine() const;
  /// The desk's status message with the base map's note appended.
  std::string Message(const StatusBar& bar) const;
  /// The page of the open dialog `kind`, or null.
  OptionsPage* OptionsPageAt(int kind, int page) const;
  /// Re-flattens the menus and toolbar when the model changed.
  void SyncMenus();
  std::string JobLine() const;
  /// Sets the desk's map hint from what the base map drew under the last
  /// hover point.
  void HoverBaseMap();
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
  SetLogThreadName("render");
  // The worker keeps its own catalog connection; the UI thread's is not
  // shared across threads.
  std::unique_lock<std::mutex> base_lock(base_mu);
  if (job.catalog_path != worker_catalog_path) {
    worker_catalog_path = job.catalog_path;
    logged_skips.clear();
    std::shared_ptr<Catalog> c;
    if (!job.catalog_path.empty()) {
      c = std::make_shared<Catalog>();
      const Status opened = c->Open(job.catalog_path);
      if (!opened.ok()) {
        FV_LOG_ERROR("render: catalog " << job.catalog_path << ": " << opened.message);
        c.reset();
      }
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
    const auto started = std::chrono::steady_clock::now();
    base_px_valid = false;
    CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
    canvas.Clear(FvColor{0, 0, 0, 255});
    if (!HostLabelFont().empty()) canvas.SetDefaultFont(HostLabelFont());
    std::string note;
    if (job.has_product) {
      const auto stale = [this, &job] { return latest_base_seq.load() != job.base_seq; };
      std::vector<SkippedFrame> skipped;
      const Status s = base.Render(v, job.product, canvas, stale, &skipped);
      if (s.code == kInterrupted) return nullptr;
      // Each unreadable file is logged once per catalog, not once per frame.
      for (const SkippedFrame& f : skipped)
        if (logged_skips.insert(f.path).second)
          FV_LOG_WARNING("render: skipped " << f.path << ": " << f.status.message);
      // A failed render keeps what was drawn; the next view retries.
      if (!s.ok()) {
        note = "Map not fully drawn: " + s.message;
        FV_LOG_WARNING("render: " << job.product.format << " " << job.product.series_key << ": "
                                  << s.message);
      }
      else if (skipped.size() == 1)
        note = "1 map file could not be read: " + skipped[0].path;
      else if (skipped.size() > 1)
        note = std::to_string(skipped.size()) + " map files could not be read";
    }
    {
      std::lock_guard<std::mutex> lock(base_note_mu);
      base_note = std::move(note);
    }
    base_px = std::move(canvas.Buffer());
    base_px_seq = job.base_seq;
    base_px_valid = true;
    base_view = v;
    have_base_view = true;
    draw_seconds.store(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
  }
  base_lock.unlock();

  CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  if (!HostLabelFont().empty()) canvas.SetDefaultFont(HostLabelFont());
  canvas.Buffer() = base_px;
  if (!job.pan || overlay_seconds.load() < kPanOverlayBudget) {
    const auto stale = [this, &job, seq] {
      return latest_seq.load() != seq || overlay_epoch.load() != job.overlay_epoch;
    };
    std::lock_guard<std::mutex> lock(stack_mu);
    if (stale()) return nullptr;
    const auto started = std::chrono::steady_clock::now();
    // A failing overlay leaves the ones below it drawn, as the base map does.
    const bool interrupted =
        desk->overlays().DrawAll(v.Projection(), canvas, stale).code == kInterrupted;
    const double took =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!interrupted || took > overlay_seconds.load()) overlay_seconds.store(took);
    if (interrupted) return nullptr;
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
  return bar.scale + '\n' + bar.product + '\n' + Message(bar) + '\n';
}

std::string DeskHost::Impl::Message(const StatusBar& bar) const {
  std::lock_guard<std::mutex> lock(base_note_mu);
  if (bar.message.empty()) return base_note;
  if (base_note.empty()) return bar.message;
  return bar.message + " · " + base_note;
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

template <typename Fn>
std::string DeskHost::Impl::EditRules(Fn&& fn) {
  Status s;
  {
    StackEdit edit(*this);
    s = fn(*desk);
  }
  RefreshLayers();
  return s.message;
}

bool DeskHost::Impl::RefreshLayers() {
  if (desk->LayersGeneration() == layers_gen) return false;
  StackEdit edit(*this);
  layers_gen = desk->LayersGeneration();
  layer_rows = desk->layers().Rows();
  source_rows = desk->sources().Rows();
  return true;
}

DeskHost* DeskHost::Create() { return new DeskHost(); }

DeskHost::DeskHost() : impl_(std::make_unique<Impl>()) {
  RegisterBuiltinFormats();
  // ENC and OSM register outside fvkit, which they link.
  RegisterEncFormat();
  RegisterOsmFormat();
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
  for (int id : impl_->log_sinks) RemoveLogSink(id);
}

// MARK: Catalog and commands

std::string DeskHost::OpenCatalog(const std::string& path) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->OpenCatalog(path);
  }
  if (!s.ok()) {
    FV_LOG_ERROR("catalog " << path << ": " << s.message);
    return s.message;
  }
  FV_LOG_INFO("catalog opened: " << path);
  if (impl_->desk->CurrentGroup() != nullptr) return Execute("map.recenter");
  return std::string();
}

std::string DeskHost::CatalogPath() const { return impl_->desk->catalog_path(); }

std::string DeskHost::DocumentName() const { return impl_->desk->DocumentName(); }

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

bool DeskHost::OpensAsOverlay(const std::string& path) const {
  const std::string ext = std::filesystem::path(path).extension().string();
  return impl_->desk->types().FindByExtension(ext) != nullptr;
}

std::string DeskHost::OpenOverlayFile(const std::string& path) {
  const size_t errors = impl_->shell.errors.size();
  {
    Impl::StackEdit edit(*impl_);
    impl_->desk->session().OpenFile(app::TypeId(), path);
  }
  return impl_->shell.errors.size() > errors ? impl_->shell.errors.back() : std::string();
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

int DeskHost::OptionsInitialPage(int kind) const {
  return OptionsOpen(kind) ? impl_->shell.options_shown[kind]->initial_page() : -1;
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
  if (s.ok() && !m.user_path.empty() && m.user.dirty()) {
    s = m.user.Save(m.user_path);
    m.saved_user_gen = m.user.Generation();
  }
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
  m.saved_user_gen = m.user.Generation();
  m.desk->SettingsLoaded();
  return error;
}

// MARK: Application log

std::string DeskHost::StartLog(const std::string& directory, const std::string& app_version) {
  Impl& m = *impl_;
  if (m.log_file) return std::string();
#ifdef NDEBUG
  LogLevel level = LogLevel::kInfo;
#else
  LogLevel level = LogLevel::kDebug;
#endif
  const std::string configured = m.settings.GetString("log.level", "");
  const bool level_ok = configured.empty() || ParseLogLevel(configured, &level);
  const std::string dir = directory.empty() ? DefaultLogDirectory("Peregrine") : directory;
  if (dir.empty()) return "no log directory: HOME is not set";
  const int max_mb = std::max(1, m.settings.GetInt("log.max_mb", 5));
  const int keep = std::max(1, m.settings.GetInt("log.keep_files", 5));
  Status s;
  m.log_file = FileLogSink::Open(dir, "Peregrine", &s, static_cast<uint64_t>(max_mb) << 20, keep);
  if (!m.log_file) return s.message;
  m.log_sinks.push_back(AddLogSink(m.log_file, level));
#ifndef NDEBUG
  m.log_sinks.push_back(AddLogSink(std::make_shared<StderrLogSink>(), level));
#endif
  const std::string& catalog = m.desk->catalog_path();
  std::error_code ec;
  const std::string ini =
      m.settings.path().empty() ? "(none)" : std::filesystem::absolute(m.settings.path(), ec).string();
  LogBanner(__FILE__, __LINE__,
            "Peregrine " + app_version + " started on " + OperatingSystemDescription() +
                "; settings " + ini +
                "; user settings " + (m.user_path.empty() ? "(none)" : m.user_path) +
                "; catalog " + (catalog.empty() ? "(none)" : catalog) + "; log level " +
                LogLevelName(level));
  if (!level_ok)
    FV_LOG_WARNING("log.level = " << configured << " is not error, warning, info or debug");
  return std::string();
}

std::string DeskHost::LogFilePath() const {
  return impl_->log_file ? impl_->log_file->Path() : std::string();
}

// MARK: Route elevation profile

bool DeskHost::TakeRouteProfileRequest() {
  const bool asked = impl_->shell.profile_requested;
  impl_->shell.profile_requested = false;
  return asked;
}

bool DeskHost::RouteProfileOpen() const { return impl_->desk->RouteProfileOpen(); }

void DeskHost::CloseRouteProfile() {
  impl_->desk->CloseRouteProfile();
  impl_->profile_layout = ProfileLayout();
}

std::string DeskHost::RouteProfileTitle() const { return impl_->desk->route_profile_title(); }

std::string DeskHost::RouteProfileStats() const { return impl_->desk->route_profile().Stats(); }

void DeskHost::LayoutRouteProfile(double width_pt, double height_pt) {
  impl_->profile_w = width_pt;
  impl_->profile_h = height_pt;
  impl_->profile_layout = impl_->desk->route_profile().Layout(width_pt, height_pt);
}

HostChartBox DeskHost::RouteProfileBox() const {
  const ProfileLayout& L = impl_->profile_layout;
  HostChartBox b;
  b.x0 = L.x0;
  b.y0 = L.y0;
  b.x1 = L.x1;
  b.y1 = L.y1;
  b.valid = L.has_plot;
  return b;
}

std::string DeskHost::RouteProfileMessage() const { return impl_->profile_layout.message; }

namespace {
const std::vector<ChartTick>* TicksOf(const ProfileLayout& L, int axis) {
  return axis == 0 ? &L.x_ticks : axis == 1 ? &L.y_ticks : nullptr;
}
}  // namespace

int DeskHost::RouteProfileTickCount(int axis) const {
  const std::vector<ChartTick>* t = TicksOf(impl_->profile_layout, axis);
  return t ? static_cast<int>(t->size()) : 0;
}

double DeskHost::RouteProfileTickPos(int axis, int index) const {
  const std::vector<ChartTick>* t = TicksOf(impl_->profile_layout, axis);
  return t && index >= 0 && index < static_cast<int>(t->size()) ? (*t)[index].pos : 0.0;
}

std::string DeskHost::RouteProfileTickLabel(int axis, int index) const {
  const std::vector<ChartTick>* t = TicksOf(impl_->profile_layout, axis);
  return t && index >= 0 && index < static_cast<int>(t->size()) ? (*t)[index].label
                                                                 : std::string();
}

int DeskHost::RouteProfileRunCount() const {
  return static_cast<int>(impl_->profile_layout.runs.size());
}

int DeskHost::RouteProfileRunLength(int run) const {
  const auto& runs = impl_->profile_layout.runs;
  return run >= 0 && run < static_cast<int>(runs.size()) ? static_cast<int>(runs[run].size()) : 0;
}

double DeskHost::RouteProfileRunX(int run, int index) const {
  return index < RouteProfileRunLength(run) && index >= 0
             ? impl_->profile_layout.runs[run][index].x
             : 0.0;
}

double DeskHost::RouteProfileRunY(int run, int index) const {
  return index < RouteProfileRunLength(run) && index >= 0
             ? impl_->profile_layout.runs[run][index].y
             : 0.0;
}

int DeskHost::RouteProfileVertexCount() const {
  return static_cast<int>(impl_->profile_layout.vertex_x.size());
}

double DeskHost::RouteProfileVertexX(int index) const {
  const auto& v = impl_->profile_layout.vertex_x;
  return index >= 0 && index < static_cast<int>(v.size()) ? v[index] : 0.0;
}

std::string DeskHost::RouteProfileReadout(double x_pt) const {
  return impl_->desk->route_profile().ReadoutAt(x_pt, impl_->profile_w, impl_->profile_h);
}

// MARK: Feature form

namespace {

const FormField* FormAt(const Desk& desk, int i) {
  const auto& form = desk.feature_form();
  if (!form || i < 0 || i >= static_cast<int>(form->fields().size())) return nullptr;
  return &form->fields()[i];
}

}  // namespace

bool DeskHost::TakeFeatureFormRequest() {
  const bool asked = impl_->shell.form_requested;
  impl_->shell.form_requested = false;
  return asked && impl_->desk->feature_form() != nullptr;
}

bool DeskHost::FeatureFormOpen() const { return impl_->desk->feature_form() != nullptr; }

std::string DeskHost::FeatureFormTitle() const {
  const auto& form = impl_->desk->feature_form();
  return form ? form->title() : std::string();
}

int DeskHost::FormFieldCount() const {
  const auto& form = impl_->desk->feature_form();
  return form ? static_cast<int>(form->fields().size()) : 0;
}

int DeskHost::FormFieldControl(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? static_cast<int>(f->control) : static_cast<int>(FormControl::kReadOnly);
}

std::string DeskHost::FormFieldTitle(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? f->title : std::string();
}

std::string DeskHost::FormFieldDescription(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? f->description : std::string();
}

std::string DeskHost::FormFieldUnit(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? f->unit : std::string();
}

bool DeskHost::FormFieldRequired(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f && f->required;
}

int DeskHost::FormFieldChoiceCount(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? static_cast<int>(f->choices.size()) : 0;
}

std::string DeskHost::FormFieldChoice(int i, int choice) const {
  const FormField* f = FormAt(*impl_->desk, i);
  if (!f || choice < 0 || choice >= static_cast<int>(f->choices.size())) return std::string();
  return f->choices[choice];
}

std::string DeskHost::FormFieldText(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? f->text : std::string();
}

std::string DeskHost::FormFieldEndText(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? f->end_text : std::string();
}

std::string DeskHost::FormFieldError(int i) const {
  const FormField* f = FormAt(*impl_->desk, i);
  return f ? f->error : std::string();
}

std::string DeskHost::FormFieldActionUrl(int i) const {
  const auto& form = impl_->desk->feature_form();
  return form && i >= 0 ? form->ActionUrl(static_cast<size_t>(i)) : std::string();
}

std::string DeskHost::SetFormFieldText(int i, const std::string& text) {
  const auto& form = impl_->desk->feature_form();
  if (!form) return "no feature form is open";
  return form->SetText(Index(i), text).message;
}

std::string DeskHost::SetFormFieldEndText(int i, const std::string& text) {
  const auto& form = impl_->desk->feature_form();
  if (!form) return "no feature form is open";
  return form->SetEndText(Index(i), text).message;
}

bool DeskHost::FeatureFormDirty() const {
  const auto& form = impl_->desk->feature_form();
  return form && form->dirty();
}

std::string DeskHost::ValidateFeatureForm() {
  const auto& form = impl_->desk->feature_form();
  if (!form) return "no feature form is open";
  return form->Validate().message;
}

std::string DeskHost::ApplyFeatureForm() {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->ApplyFeatureForm().message;
}

std::string DeskHost::RevertFeatureForm() {
  const auto& form = impl_->desk->feature_form();
  if (!form) return "no feature form is open";
  Impl::StackEdit edit(*impl_);
  return form->Revert().message;
}

void DeskHost::CloseFeatureForm() { impl_->desk->CloseFeatureForm(); }

// MARK: Attribute table

bool DeskHost::TakeAttributeTableRequest() {
  const bool asked = impl_->shell.table_requested;
  impl_->shell.table_requested = false;
  return asked && impl_->desk->attribute_table() != nullptr;
}

bool DeskHost::AttributeTableOpen() const { return impl_->desk->attribute_table() != nullptr; }

std::string DeskHost::AttributeTableTitle() const {
  const auto& t = impl_->desk->attribute_table();
  return t ? t->title() : std::string();
}

int DeskHost::TableColumnCount() const {
  const auto& t = impl_->desk->attribute_table();
  return t ? static_cast<int>(t->columns().size()) : 0;
}

std::string DeskHost::TableColumnTitle(int column) const {
  const auto& t = impl_->desk->attribute_table();
  if (!t || column < 0 || column >= static_cast<int>(t->columns().size())) return std::string();
  return t->columns()[column].title;
}

bool DeskHost::TableColumnNumeric(int column) const {
  const auto& t = impl_->desk->attribute_table();
  return t && column >= 0 && column < static_cast<int>(t->columns().size()) && t->columns()[column].numeric;
}

int DeskHost::TableRowCount() const {
  const auto& t = impl_->desk->attribute_table();
  return t ? static_cast<int>(t->RowCount()) : 0;
}

std::string DeskHost::TableCell(int row, int column) const {
  const auto& t = impl_->desk->attribute_table();
  if (!t || row < 0 || column < 0) return std::string();
  return t->Cell(static_cast<size_t>(row), static_cast<size_t>(column));
}

void DeskHost::SortTable(int column, bool ascending) {
  if (const auto& t = impl_->desk->attribute_table()) t->Sort(column, ascending);
}

int DeskHost::TableSortColumn() const {
  const auto& t = impl_->desk->attribute_table();
  return t ? t->sort_column() : -1;
}

bool DeskHost::TableSortAscending() const {
  const auto& t = impl_->desk->attribute_table();
  return !t || t->ascending();
}

void DeskHost::SetTableFilter(const std::string& text) {
  if (const auto& t = impl_->desk->attribute_table()) t->SetFilter(text);
}

std::string DeskHost::ReloadAttributeTable() {
  const auto& t = impl_->desk->attribute_table();
  if (!t) return "no attribute table is open";
  Impl::StackEdit edit(*impl_);
  return t->Reload().message;
}

std::string DeskHost::SelectTableRow(int row) {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->SelectTableRow(Index(row)).message;
}

int DeskHost::SelectedTableRow() const { return impl_->desk->SelectedTableRow(); }

std::string DeskHost::EditTableRow(int row) {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->EditTableRow(Index(row)).message;
}

void DeskHost::CloseAttributeTable() { impl_->desk->CloseAttributeTable(); }

// MARK: Layer style

namespace {

/// Property `k` of the open style dialog's geometry; false out of range.
bool StylePropAt(const Desk& desk, int k, SymbolPropertyInfo* out) {
  const auto& style = desk.layer_style();
  if (!style || k < 0) return false;
  const std::vector<SymbolPropertyInfo> props = SymbolPropertiesFor(style->geometry());
  if (k >= static_cast<int>(props.size())) return false;
  *out = props[k];
  return true;
}

/// Fields a label can show.
std::vector<StyleFieldChoice> LabelFields(const Desk& desk) {
  const auto& style = desk.layer_style();
  return style ? style->LabelFields() : std::vector<StyleFieldChoice>{};
}

constexpr const char* kNoStyle = "no style dialog is open";

/// Applies `edit` to a copy of entry `i`'s symbol and stores it.
template <typename Edit>
std::string EditStyleSymbol(Desk& desk, int i, int k, Edit edit) {
  const auto& st = desk.layer_style();
  if (!st) return kNoStyle;
  SymbolPropertyInfo p;
  if (!StylePropAt(desk, k, &p)) return "no style property " + std::to_string(k);
  if (i < 0 || i >= static_cast<int>(st->EntryCount())) return "no style entry " + std::to_string(i);
  FeatureSymbol sym = st->EntrySymbol(static_cast<size_t>(i));
  Status s = edit(&sym, p.id);
  if (s.ok()) s = st->SetEntrySymbol(static_cast<size_t>(i), sym);
  return s.message;
}

}  // namespace

bool DeskHost::TakeLayerStyleRequest() {
  const bool asked = impl_->shell.style_requested;
  impl_->shell.style_requested = false;
  return asked && impl_->desk->layer_style() != nullptr;
}

bool DeskHost::LayerStyleOpen() const { return impl_->desk->layer_style() != nullptr; }

std::string DeskHost::LayerStyleTitle() const {
  const auto& st = impl_->desk->layer_style();
  return st ? st->title() : std::string();
}

bool DeskHost::LayerStyleIsFeature() const {
  const auto& st = impl_->desk->layer_style();
  return st && st->feature() != 0;
}

bool DeskHost::LayerStyleDirty() const {
  const auto& st = impl_->desk->layer_style();
  return st && st->dirty();
}

int DeskHost::StyleRenderer() const {
  const auto& st = impl_->desk->layer_style();
  return st ? static_cast<int>(st->renderer()) : 0;
}

std::string DeskHost::SetStyleRenderer(int renderer) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  if (renderer < 0 || renderer > 3) return "no renderer " + std::to_string(renderer);
  return st->SetRenderer(static_cast<FeatureRenderer>(renderer)).message;
}

std::string DeskHost::CategoriseStyleIntoRules() {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->CategoriseIntoRules().message;
}

int DeskHost::StyleFieldCount() const {
  const auto& st = impl_->desk->layer_style();
  return st ? static_cast<int>(st->CandidateFields().size()) : 0;
}

std::string DeskHost::StyleFieldName(int i) const {
  const auto& st = impl_->desk->layer_style();
  if (!st) return std::string();
  const auto fields = st->CandidateFields();
  return i >= 0 && i < static_cast<int>(fields.size()) ? fields[i].name : std::string();
}

std::string DeskHost::StyleFieldTitle(int i) const {
  const auto& st = impl_->desk->layer_style();
  if (!st) return std::string();
  const auto fields = st->CandidateFields();
  return i >= 0 && i < static_cast<int>(fields.size()) ? fields[i].title : std::string();
}

std::string DeskHost::StyleField() const {
  const auto& st = impl_->desk->layer_style();
  return st ? st->field() : std::string();
}

std::string DeskHost::SetStyleField(const std::string& name) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->SetField(name).message;
}

int DeskHost::StyleClassCount() const {
  const auto& st = impl_->desk->layer_style();
  return st ? st->graduated().classes : 0;
}

int DeskHost::StyleClassMode() const {
  const auto& st = impl_->desk->layer_style();
  return st ? static_cast<int>(st->graduated().mode) : 0;
}

int DeskHost::StyleClassVary() const {
  const auto& st = impl_->desk->layer_style();
  return st ? static_cast<int>(st->graduated().vary) : 0;
}

std::string DeskHost::ClassifyStyle(int classes, int mode, int vary) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  if (classes < 1 || classes > 32) return "choose from 1 to 32 classes";
  GraduatedOptions opt = st->graduated();
  opt.classes = classes;
  opt.mode = mode == 1 ? GraduatedMode::kQuantile : GraduatedMode::kEqualInterval;
  opt.vary = vary == 1 ? GraduatedVary::kSize : GraduatedVary::kColor;
  st->SetGraduated(opt);
  return st->Classify().message;
}

int DeskHost::StyleEntryCount() const {
  const auto& st = impl_->desk->layer_style();
  return st ? static_cast<int>(st->EntryCount()) : 0;
}

bool DeskHost::StyleEntryIsOther(int i) const {
  const auto& st = impl_->desk->layer_style();
  return st && i >= 0 && st->EntryIsOther(static_cast<size_t>(i));
}

std::string DeskHost::StyleEntryLabel(int i) const {
  const auto& st = impl_->desk->layer_style();
  return st && i >= 0 ? st->EntryLabel(static_cast<size_t>(i)) : std::string();
}

std::string DeskHost::StyleEntryValue(int i) const {
  const auto& st = impl_->desk->layer_style();
  return st && i >= 0 ? st->EntryValue(static_cast<size_t>(i)) : std::string();
}

std::string DeskHost::SetStyleEntryLabel(int i, const std::string& label) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->SetEntryLabel(Index(i), label).message;
}

std::string DeskHost::SetStyleEntryValue(int i, const std::string& value) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->SetEntryValue(Index(i), value).message;
}

std::string DeskHost::AddStyleEntry() {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->AddEntry().message;
}

std::string DeskHost::RemoveStyleEntry(int i) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->RemoveEntry(Index(i)).message;
}

bool DeskHost::CopyStyleSwatch(int i, int width, int height, double scale, uint8_t* out,
                               size_t capacity) const {
  const auto& st = impl_->desk->layer_style();
  if (!st || out == nullptr || i < 0) return false;
  PixelBuffer buf;
  if (!st->RenderSwatch(static_cast<size_t>(i), width, height, scale, &buf).ok()) return false;
  const size_t row = static_cast<size_t>(buf.Width()) * 4;
  if (capacity < row * buf.Height()) return false;
  for (int y = 0; y < buf.Height(); ++y) std::memcpy(out + row * y, buf.Row(y), row);
  return true;
}

int DeskHost::StylePropertyCount() const {
  const auto& st = impl_->desk->layer_style();
  return st ? static_cast<int>(SymbolPropertiesFor(st->geometry()).size()) : 0;
}

int DeskHost::StylePropertyKind(int k) const {
  SymbolPropertyInfo p;
  return StylePropAt(*impl_->desk, k, &p) ? static_cast<int>(p.kind) : 0;
}

std::string DeskHost::StylePropertyTitle(int k) const {
  SymbolPropertyInfo p;
  return StylePropAt(*impl_->desk, k, &p) ? p.title : std::string();
}

std::string DeskHost::StylePropertyGroup(int k) const {
  SymbolPropertyInfo p;
  return StylePropAt(*impl_->desk, k, &p) ? p.group : std::string();
}

int DeskHost::StylePropertyChoiceCount(int k) const {
  SymbolPropertyInfo p;
  return StylePropAt(*impl_->desk, k, &p) ? static_cast<int>(p.choices.size()) : 0;
}

std::string DeskHost::StylePropertyChoice(int k, int choice) const {
  SymbolPropertyInfo p;
  if (!StylePropAt(*impl_->desk, k, &p) || choice < 0 || choice >= static_cast<int>(p.choices.size()))
    return std::string();
  return p.choices[choice];
}

uint32_t DeskHost::StyleColor(int i, int k) const {
  SymbolPropertyInfo p;
  if (!StylePropAt(*impl_->desk, k, &p) || i < 0) return 0;
  const FvColor c = GetSymbolColor(impl_->desk->layer_style()->EntrySymbol(static_cast<size_t>(i)), p.id);
  return (uint32_t{c.r} << 24) | (uint32_t{c.g} << 16) | (uint32_t{c.b} << 8) | c.a;
}

double DeskHost::StyleNumber(int i, int k) const {
  SymbolPropertyInfo p;
  if (!StylePropAt(*impl_->desk, k, &p) || i < 0) return 0;
  return GetSymbolNumber(impl_->desk->layer_style()->EntrySymbol(static_cast<size_t>(i)), p.id);
}

int DeskHost::StyleChoice(int i, int k) const {
  SymbolPropertyInfo p;
  if (!StylePropAt(*impl_->desk, k, &p) || i < 0) return 0;
  return GetSymbolChoice(impl_->desk->layer_style()->EntrySymbol(static_cast<size_t>(i)), p.id);
}

std::string DeskHost::SetStyleColor(int i, int k, uint32_t rgba) {
  const FvColor c{static_cast<unsigned char>(rgba >> 24), static_cast<unsigned char>(rgba >> 16),
                  static_cast<unsigned char>(rgba >> 8), static_cast<unsigned char>(rgba)};
  return EditStyleSymbol(*impl_->desk, i, k,
                         [&](FeatureSymbol* s, SymbolProperty id) { return SetSymbolColor(s, id, c); });
}

std::string DeskHost::SetStyleNumber(int i, int k, double value) {
  return EditStyleSymbol(*impl_->desk, i, k,
                         [&](FeatureSymbol* s, SymbolProperty id) { return SetSymbolNumber(s, id, value); });
}

std::string DeskHost::SetStyleChoice(int i, int k, int choice) {
  return EditStyleSymbol(*impl_->desk, i, k,
                         [&](FeatureSymbol* s, SymbolProperty id) { return SetSymbolChoice(s, id, choice); });
}

bool DeskHost::StyleHasOverride() const {
  const auto& st = impl_->desk->layer_style();
  return st && st->has_override();
}

std::string DeskHost::ClearStyleOverride() {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->ClearOverride().message;
}

int DeskHost::StyleLabelFieldCount() const { return static_cast<int>(LabelFields(*impl_->desk).size()); }

std::string DeskHost::StyleLabelFieldName(int i) const {
  const auto fields = LabelFields(*impl_->desk);
  return i >= 0 && i < static_cast<int>(fields.size()) ? fields[i].name : std::string();
}

std::string DeskHost::StyleLabelFieldTitle(int i) const {
  const auto fields = LabelFields(*impl_->desk);
  return i >= 0 && i < static_cast<int>(fields.size()) ? fields[i].title : std::string();
}

std::string DeskHost::StyleLabelField() const {
  const auto& st = impl_->desk->layer_style();
  return st ? st->label().field : std::string();
}

double DeskHost::StyleLabelSize() const {
  const auto& st = impl_->desk->layer_style();
  return st ? st->label().size_px : 0.0;
}

std::string DeskHost::SetStyleLabel(const std::string& field, double size_px) {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  FeatureLabel label = st->label();
  label.field = field;
  label.size_px = size_px;
  return st->SetLabel(label).message;
}

std::string DeskHost::ApplyLayerStyle() {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->ApplyLayerStyle().message;
}

std::string DeskHost::RevertLayerStyle() {
  const auto& st = impl_->desk->layer_style();
  if (!st) return kNoStyle;
  return st->Revert().message;
}

void DeskHost::CloseLayerStyle() { impl_->desk->CloseLayerStyle(); }

// MARK: Layer fields

namespace {

const char kNoFields[] = "no Layer Fields dialog is open";
const char kNoNewLayer[] = "no New Layer dialog is open";

/// A number as typed bounds show it; "" for none.
std::string BoundText(const std::optional<double>& v) {
  if (!v) return std::string();
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.15g", *v);
  return buf;
}

/// Typed bounds: blank is none; kInvalidArg for text that is not a number.
Status ParseBound(const std::string& text, const char* which, std::optional<double>* out) {
  size_t b = text.find_first_not_of(" \t");
  if (b == std::string::npos) {
    out->reset();
    return Status::Ok();
  }
  char* end = nullptr;
  const double v = std::strtod(text.c_str() + b, &end);
  while (end != nullptr && (*end == ' ' || *end == '\t')) ++end;
  if (end == text.c_str() + b || (end != nullptr && *end != '\0') || !std::isfinite(v))
    return Status::Error(kInvalidArg, std::string("the ") + which + " needs a number");
  *out = v;
  return Status::Ok();
}

}  // namespace

bool DeskHost::TakeLayerFieldsRequest() {
  const bool asked = impl_->shell.fields_requested;
  impl_->shell.fields_requested = false;
  return asked && impl_->desk->layer_fields() != nullptr;
}

bool DeskHost::LayerFieldsOpen() const { return impl_->desk->layer_fields() != nullptr; }

std::string DeskHost::LayerFieldsTitle() const {
  const auto& m = impl_->desk->layer_fields();
  return m ? m->title() : std::string();
}

bool DeskHost::LayerFieldsDirty() const {
  const auto& m = impl_->desk->layer_fields();
  return m && m->dirty();
}

int DeskHost::FieldTypeCount() const { return static_cast<int>(EditableFieldTypes().size()); }

std::string DeskHost::FieldTypeTitle(int k) const {
  const auto& types = EditableFieldTypes();
  return k >= 0 && k < static_cast<int>(types.size()) ? desk::FieldTypeTitle(types[k]) : std::string();
}

int DeskHost::FieldRoleCount() const { return static_cast<int>(EditableFieldRoles().size()); }

std::string DeskHost::FieldRoleTitle(int k) const {
  const auto& roles = EditableFieldRoles();
  return k >= 0 && k < static_cast<int>(roles.size()) ? desk::FieldRoleTitle(roles[k]) : std::string();
}

int DeskHost::LayerFieldCount() const {
  const auto& m = impl_->desk->layer_fields();
  return m ? static_cast<int>(m->FieldCount()) : 0;
}

namespace {

/// Field `i` of the open Layer Fields dialog, or a default one.
const FieldDef& FieldAt(const std::shared_ptr<LayerFieldsModel>& m, int i) {
  static const FieldDef kNone;
  return m && i >= 0 ? m->Field(static_cast<size_t>(i)) : kNone;
}

}  // namespace

std::string DeskHost::LayerFieldName(int i) const { return FieldAt(impl_->desk->layer_fields(), i).name; }

std::string DeskHost::LayerFieldOriginalName(int i) const {
  const auto& m = impl_->desk->layer_fields();
  return m && i >= 0 ? m->OriginalName(static_cast<size_t>(i)) : std::string();
}

int DeskHost::LayerFieldType(int i) const {
  const auto& types = EditableFieldTypes();
  const auto it = std::find(types.begin(), types.end(), FieldAt(impl_->desk->layer_fields(), i).type);
  return it == types.end() ? -1 : static_cast<int>(it - types.begin());
}

int DeskHost::LayerFieldRole(int i) const {
  const auto& roles = EditableFieldRoles();
  const auto it = std::find(roles.begin(), roles.end(), FieldAt(impl_->desk->layer_fields(), i).role);
  return it == roles.end() ? 0 : static_cast<int>(it - roles.begin());
}

std::string DeskHost::LayerFieldTitle(int i) const { return FieldAt(impl_->desk->layer_fields(), i).title; }

std::string DeskHost::LayerFieldDescription(int i) const {
  return FieldAt(impl_->desk->layer_fields(), i).description;
}

std::string DeskHost::LayerFieldUnit(int i) const { return FieldAt(impl_->desk->layer_fields(), i).unit; }

bool DeskHost::LayerFieldRequired(int i) const { return FieldAt(impl_->desk->layer_fields(), i).required; }

std::string DeskHost::LayerFieldDefault(int i) const {
  FieldDef f = FieldAt(impl_->desk->layer_fields(), i);
  f.role = FieldRole::kNone;  // the value, not a choice's label
  return FormatFieldValue(f, f.default_value);
}

std::string DeskHost::LayerFieldMin(int i) const { return BoundText(FieldAt(impl_->desk->layer_fields(), i).min); }

std::string DeskHost::LayerFieldMax(int i) const { return BoundText(FieldAt(impl_->desk->layer_fields(), i).max); }

std::string DeskHost::LayerFieldChoices(int i) const {
  return FormatChoices(FieldAt(impl_->desk->layer_fields(), i).choices);
}

int DeskHost::AddLayerField() {
  const auto& m = impl_->desk->layer_fields();
  return m ? static_cast<int>(m->AddField()) : -1;
}

std::string DeskHost::RemoveLayerField(int i) {
  const auto& m = impl_->desk->layer_fields();
  if (!m) return kNoFields;
  if (i < 0) return "no field " + std::to_string(i);
  return m->RemoveField(static_cast<size_t>(i)).message;
}

std::string DeskHost::MoveLayerField(int from, int to) {
  const auto& m = impl_->desk->layer_fields();
  if (!m) return kNoFields;
  if (from < 0 || to < 0) return "no field to move there";
  return m->MoveField(static_cast<size_t>(from), static_cast<size_t>(to)).message;
}

std::string DeskHost::SetLayerField(int i, const std::string& name, int type, int role, const std::string& title,
                                    const std::string& description, const std::string& unit, bool required,
                                    const std::string& default_text, const std::string& min_text,
                                    const std::string& max_text, const std::string& choices) {
  const auto& m = impl_->desk->layer_fields();
  if (!m) return kNoFields;
  if (i < 0 || i >= static_cast<int>(m->FieldCount())) return "no field " + std::to_string(i);
  const auto& types = EditableFieldTypes();
  const auto& roles = EditableFieldRoles();
  FieldDef f = m->Field(static_cast<size_t>(i));
  // A binary field keeps its type: the editor does not offer it.
  if (type >= 0 && type < static_cast<int>(types.size())) f.type = types[type];
  else if (f.type != FieldType::kBlob) return "no field type " + std::to_string(type);
  if (role < 0 || role >= static_cast<int>(roles.size())) return "no field role " + std::to_string(role);
  f.name = name;
  f.role = roles[role];
  f.title = title;
  f.description = description;
  f.unit = unit;
  f.required = required;
  Status s = ParseChoices(choices, &f.choices);
  if (s.ok()) s = ParseBound(min_text, "minimum", &f.min);
  if (s.ok()) s = ParseBound(max_text, "maximum", &f.max);
  if (!s.ok()) return s.message;
  FieldDef plain = f;
  plain.role = FieldRole::kNone;
  plain.title = "default";
  s = ParseFieldValue(plain, default_text, &f.default_value);
  if (!s.ok()) return s.message;
  return m->SetField(static_cast<size_t>(i), f).message;
}

std::string DeskHost::LayerFieldsLoss() const { return impl_->desk->LayerFieldsLoss(); }

std::string DeskHost::ApplyLayerFields() {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->ApplyLayerFields().message;
}

std::string DeskHost::RevertLayerFields() {
  const auto& m = impl_->desk->layer_fields();
  if (!m) return kNoFields;
  return m->Revert().message;
}

void DeskHost::CloseLayerFields() { impl_->desk->CloseLayerFields(); }

// MARK: New layer

bool DeskHost::TakeNewLayerRequest() {
  const bool asked = impl_->shell.new_layer_requested;
  impl_->shell.new_layer_requested = false;
  return asked && impl_->desk->new_layer() != nullptr;
}

bool DeskHost::NewLayerOpen() const { return impl_->desk->new_layer() != nullptr; }

int DeskHost::NewLayerTemplateCount() const {
  const auto& m = impl_->desk->new_layer();
  return m ? static_cast<int>(m->TemplateCount()) : 0;
}

std::string DeskHost::NewLayerTemplateTitle(int i) const {
  const auto& m = impl_->desk->new_layer();
  return m && i >= 0 ? m->TemplateTitle(static_cast<size_t>(i)) : std::string();
}

int DeskHost::NewLayerTemplate() const {
  const auto& m = impl_->desk->new_layer();
  return m ? static_cast<int>(m->template_index()) : 0;
}

std::string DeskHost::SetNewLayerTemplate(int i) {
  const auto& m = impl_->desk->new_layer();
  if (!m) return kNoNewLayer;
  if (i < 0) return "no template " + std::to_string(i);
  return m->SetTemplate(static_cast<size_t>(i)).message;
}

int DeskHost::NewLayerGeometryCount() const { return static_cast<int>(NewLayerGeometries().size()); }

std::string DeskHost::NewLayerGeometryTitle(int k) const {
  const auto& types = NewLayerGeometries();
  return k >= 0 && k < static_cast<int>(types.size()) ? GeometryTitle(types[k]) : std::string();
}

int DeskHost::NewLayerGeometry() const {
  const auto& m = impl_->desk->new_layer();
  if (!m) return 0;
  const auto& types = NewLayerGeometries();
  const auto it = std::find(types.begin(), types.end(), m->geometry());
  return it == types.end() ? 0 : static_cast<int>(it - types.begin());
}

std::string DeskHost::SetNewLayerGeometry(int k) {
  const auto& m = impl_->desk->new_layer();
  if (!m) return kNoNewLayer;
  const auto& types = NewLayerGeometries();
  if (k < 0 || k >= static_cast<int>(types.size())) return "no geometry " + std::to_string(k);
  return m->SetGeometry(types[k]).message;
}

std::string DeskHost::NewLayerName() const {
  const auto& m = impl_->desk->new_layer();
  return m ? m->name() : std::string();
}

void DeskHost::SetNewLayerName(const std::string& name) {
  if (const auto& m = impl_->desk->new_layer()) m->SetName(name);
}

int DeskHost::NewLayerTarget() const {
  const auto& m = impl_->desk->new_layer();
  return m ? static_cast<int>(m->target()) : 0;
}

std::string DeskHost::SetNewLayerTarget(int target) {
  const auto& m = impl_->desk->new_layer();
  if (!m) return kNoNewLayer;
  if (target < 0 || target > 2) return "no target " + std::to_string(target);
  m->SetTarget(static_cast<fv::desk::NewLayerTarget>(target));
  return std::string();
}

int DeskHost::NewLayerFormat() const {
  const auto& m = impl_->desk->new_layer();
  return m ? m->format() : 0;
}

std::string DeskHost::SetNewLayerFormat(int format) {
  const auto& m = impl_->desk->new_layer();
  if (!m) return kNoNewLayer;
  return m->SetFormat(format).message;
}

std::string DeskHost::NewLayerPath() const {
  const auto& m = impl_->desk->new_layer();
  return m ? m->path() : std::string();
}

void DeskHost::SetNewLayerPath(const std::string& path) {
  if (const auto& m = impl_->desk->new_layer()) m->SetPath(path);
}

std::string DeskHost::NewLayerFieldSummary() const {
  const auto& m = impl_->desk->new_layer();
  if (!m) return std::string();
  std::string text;
  for (const FieldDef& f : m->schema().fields) {
    if (!text.empty()) text += "\n";
    text += f.DisplayTitle() + " \xe2\x80\x94 " + desk::FieldTypeTitle(f.type);
    if (f.role != FieldRole::kNone) text += ", " + desk::FieldRoleTitle(f.role);
    if (!f.unit.empty()) text += " (" + f.unit + ")";
  }
  return text;
}

std::string DeskHost::CreateNewLayer() {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->CreateNewLayer().message;
}

void DeskHost::CloseNewLayer() { impl_->desk->CloseNewLayer(); }

// MARK: Find

bool DeskHost::TakeFindRequest() {
  const bool asked = impl_->shell.find_requested;
  impl_->shell.find_requested = false;
  return asked;
}

int DeskHost::Search(const std::string& text, bool map_view_only) {
  Impl& m = *impl_;
  {
    // Providers read overlay data the worker may be drawing.
    Impl::StackEdit edit(m);
    m.search_rows = m.desk->Search(text, map_view_only);
  }
  m.search_status = SearchStatusLine(m.search_rows, m.desk->search_notes(), text, map_view_only);
  return static_cast<int>(m.search_rows.size());
}

int DeskHost::SearchRowCount() const { return static_cast<int>(impl_->search_rows.size()); }

namespace {
const DeskSearchRow* SearchRowAt(const std::vector<DeskSearchRow>& rows, int row) {
  return row >= 0 && row < static_cast<int>(rows.size()) ? &rows[static_cast<size_t>(row)] : nullptr;
}
}  // namespace

int DeskHost::SearchRowGroup(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? static_cast<int>(r->group) : -1;
}

std::string DeskHost::SearchRowGroupTitle(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? SearchGroupTitle(r->group) : std::string();
}

std::string DeskHost::SearchRowTitle(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? r->title : std::string();
}

std::string DeskHost::SearchRowDetail(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? r->detail : std::string();
}

std::string DeskHost::SearchRowSource(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? r->source : std::string();
}

double DeskHost::SearchRowDistance(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? r->distance_m : 0.0;
}

std::string DeskHost::SearchRowDistanceText(int row) const {
  return FormatSearchDistance(SearchRowDistance(row));
}

double DeskHost::SearchRowLatitude(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? r->position.lat : 0.0;
}

double DeskHost::SearchRowLongitude(int row) const {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  return r ? r->position.lon : 0.0;
}

std::string DeskHost::SearchStatus() const { return impl_->search_status; }

void DeskHost::GoToSearchRow(int row) {
  const DeskSearchRow* r = SearchRowAt(impl_->search_rows, row);
  if (r == nullptr) return;
  const DeskSearchRow copy = *r;
  impl_->desk->GoToSearchRow(copy);
}

// MARK: Layers panel

bool DeskHost::TakeLayersRequest() {
  const bool asked = impl_->shell.layers_requested;
  impl_->shell.layers_requested = false;
  if (asked) impl_->RefreshLayers();
  return asked;
}

bool DeskHost::TakeLayersHideRequest() {
  const bool asked = impl_->shell.layers_hide_requested;
  impl_->shell.layers_hide_requested = false;
  return asked;
}

void DeskHost::SetLayersShown(bool shown) { impl_->desk->SetLayersShown(shown); }

int DeskHost::LayerCount() const { return static_cast<int>(impl_->layer_rows.size()); }

std::string DeskHost::LayerName(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr ? r->name : std::string();
}

std::string DeskHost::LayerTypeName(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr ? r->type_name : std::string();
}

std::string DeskHost::LayerIcon(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr ? r->icon : std::string();
}

bool DeskHost::LayerVisible(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr && r->visible;
}

bool DeskHost::LayerHasOpacity(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr && r->has_opacity;
}

int DeskHost::LayerOpacity(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr ? r->opacity : 0;
}

bool DeskHost::LayerCurrent(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr && r->current;
}

bool DeskHost::LayerDirty(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr && r->dirty;
}

std::string DeskHost::SetLayerVisible(int row, bool visible) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->SetLayerVisible(Index(row), visible);
  }
  impl_->RefreshLayers();
  return s.message;
}

std::string DeskHost::SetLayerOpacity(int row, int percent) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->SetLayerOpacity(Index(row), percent);
  }
  impl_->RefreshLayers();
  return s.message;
}

std::string DeskHost::MoveLayer(int from, int to) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->MoveLayer(Index(from), Index(to));
  }
  impl_->RefreshLayers();
  return s.message;
}

std::string DeskHost::SelectLayer(int row) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->SelectLayer(Index(row));
  }
  impl_->RefreshLayers();
  return s.message;
}

// MARK: Layer manager

int DeskHost::SourceCount() const { return static_cast<int>(impl_->source_rows.size()); }

std::string DeskHost::SourceName(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr ? s->name : std::string();
}

std::string DeskHost::SourcePath(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr ? s->path : std::string();
}

std::string DeskHost::SourceTypeName(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr ? s->type_name : std::string();
}

std::string DeskHost::SourceIcon(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr ? s->icon : std::string();
}

int DeskHost::SourceShown(int source) const {
  const SourceRow* s = impl_->Source(source);
  if (s == nullptr) return 0;
  return s->shown == fv::desk::SourceShown::kAll ? 2 : s->shown == fv::desk::SourceShown::kSome ? 1 : 0;
}

bool DeskHost::SourceCurrent(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr && s->current;
}

bool DeskHost::SourceDirty(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr && s->dirty;
}

int DeskHost::SourceLayerCount(int source) const {
  const SourceRow* s = impl_->Source(source);
  return s != nullptr ? static_cast<int>(s->layers.size()) : 0;
}

int DeskHost::SourceLayerRow(int source, int i) const {
  const SourceRow* s = impl_->Source(source);
  if (s == nullptr || i < 0 || i >= static_cast<int>(s->layers.size())) return -1;
  return static_cast<int>(s->layers[i]);
}

std::string DeskHost::SetSourceVisible(int source, bool visible) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->SetSourceVisible(Index(source), visible);
  }
  impl_->RefreshLayers();
  return s.message;
}

std::string DeskHost::MoveSource(int from, int to) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->MoveSource(Index(from), Index(to));
  }
  impl_->RefreshLayers();
  return s.message;
}

std::string DeskHost::SelectSource(int source) {
  Status s;
  {
    Impl::StackEdit edit(*impl_);
    s = impl_->desk->SelectSource(Index(source));
  }
  impl_->RefreshLayers();
  return s.message;
}

namespace {

HostMenuEntry EntryAt(const std::vector<MenuItem>& items, int index) {
  std::vector<HostMenuEntry> flat;
  Flatten(items, 0, &flat);
  return index >= 0 && index < static_cast<int>(flat.size()) ? flat[index] : HostMenuEntry();
}

int EntryCount(const std::vector<MenuItem>& items) {
  std::vector<HostMenuEntry> flat;
  Flatten(items, 0, &flat);
  return static_cast<int>(flat.size());
}

}  // namespace

int DeskHost::LayerMenuEntryCount() const { return EntryCount(impl_->desk->LayerMenu()); }
HostMenuEntry DeskHost::LayerMenuEntryAt(int index) const { return EntryAt(impl_->desk->LayerMenu(), index); }
int DeskHost::SourceMenuEntryCount() const { return EntryCount(impl_->desk->SourceMenu()); }
HostMenuEntry DeskHost::SourceMenuEntryAt(int index) const { return EntryAt(impl_->desk->SourceMenu(), index); }

// MARK: Style rules

namespace {

const RuleRow* RuleOf(const LayerRow* r, int rule) {
  if (r == nullptr || rule < 0 || rule >= static_cast<int>(r->rules.size())) return nullptr;
  return &r->rules[static_cast<size_t>(rule)];
}

}  // namespace

int DeskHost::RuleCount(int row) const {
  const LayerRow* r = impl_->Row(row);
  return r != nullptr ? static_cast<int>(r->rules.size()) : 0;
}

std::string DeskHost::RuleName(int row, int rule) const {
  const RuleRow* r = RuleOf(impl_->Row(row), rule);
  return r != nullptr ? r->id : std::string();
}

std::string DeskHost::RuleType(int row, int rule) const {
  const RuleRow* r = RuleOf(impl_->Row(row), rule);
  return r != nullptr ? r->type : std::string();
}

bool DeskHost::RuleShown(int row, int rule) const {
  const RuleRow* r = RuleOf(impl_->Row(row), rule);
  return r != nullptr && r->shown;
}

std::string DeskHost::RuleFilter(int row, int rule) const {
  const RuleRow* r = RuleOf(impl_->Row(row), rule);
  return r != nullptr ? r->filter : std::string();
}

std::string DeskHost::RuleLegend(int row, int rule) const {
  const RuleRow* r = RuleOf(impl_->Row(row), rule);
  return r != nullptr ? r->legend : std::string();
}

int DeskHost::CurrentRule() const { return impl_->desk->CurrentRule(); }

int DeskHost::RuleTypeCount() const { return static_cast<int>(RuleTypes().size()); }

std::string DeskHost::RuleTypeName(int i) const {
  return i >= 0 && i < RuleTypeCount() ? RuleTypes()[static_cast<size_t>(i)] : std::string();
}

std::string DeskHost::SelectRule(int row, int rule) {
  return impl_->EditRules([&](Desk& d) { return d.SelectRule(Index(row), Index(rule)); });
}

std::string DeskHost::AddRule(int row, const std::string& name, const std::string& type) {
  return impl_->EditRules([&](Desk& d) { return d.AddRule(Index(row), name, type); });
}

std::string DeskHost::DuplicateRule(int row, int rule) {
  return impl_->EditRules([&](Desk& d) { return d.DuplicateRule(Index(row), Index(rule)); });
}

std::string DeskHost::RenameRule(int row, int rule, const std::string& name) {
  return impl_->EditRules([&](Desk& d) { return d.RenameRule(Index(row), Index(rule), name); });
}

std::string DeskHost::SetRuleType(int row, int rule, const std::string& type) {
  return impl_->EditRules([&](Desk& d) { return d.SetRuleType(Index(row), Index(rule), type); });
}

std::string DeskHost::SetRuleShown(int row, int rule, bool shown) {
  return impl_->EditRules([&](Desk& d) { return d.SetRuleShown(Index(row), Index(rule), shown); });
}

std::string DeskHost::MoveRule(int row, int from, int to) {
  return impl_->EditRules([&](Desk& d) { return d.MoveRule(Index(row), Index(from), Index(to)); });
}

std::string DeskHost::DeleteRule(int row, int rule) {
  return impl_->EditRules([&](Desk& d) { return d.DeleteRule(Index(row), Index(rule)); });
}

int DeskHost::RuleMenuEntryCount() const { return EntryCount(impl_->desk->RuleMenu()); }
HostMenuEntry DeskHost::RuleMenuEntryAt(int index) const { return EntryAt(impl_->desk->RuleMenu(), index); }

bool DeskHost::TakeAddRuleRequest() { return std::exchange(impl_->shell.add_rule_requested, false); }
bool DeskHost::TakeRuleRenameRequest() { return std::exchange(impl_->shell.rule_rename_requested, false); }
bool DeskHost::TakeRuleTypeRequest() { return std::exchange(impl_->shell.rule_type_requested, false); }
bool DeskHost::TakeRuleFilterRequest() { return std::exchange(impl_->shell.rule_filter_requested, false); }

std::string DeskHost::RuleFilterTitle() const {
  const auto& q = impl_->desk->rule_filter();
  if (!q) return std::string();
  const Overlay* o = q->overlay();
  return "Rule Filter \u2014 " + impl_->desk->rule_filter_rule() + " (" + (o != nullptr ? o->Name() : std::string()) +
         ")";
}

std::string DeskHost::ApplyRuleFilter() {
  return impl_->EditRules([](Desk& d) { return d.ApplyRuleFilter(); });
}

void DeskHost::CloseRuleFilter() { impl_->desk->CloseRuleFilter(); }

// MARK: Rule style pages

namespace {

const char* const kNoRuleStyle = "no rule style is open";

/// Property `k` of the open rule style, or null.
const RuleProperty* RuleProp(const Desk& d, int k) {
  const auto& m = d.rule_style();
  if (!m || k < 0 || k >= static_cast<int>(m->properties().size())) return nullptr;
  return &m->properties()[static_cast<size_t>(k)];
}

const RulePropertyValue* RuleValue(const Desk& d, int k) {
  return RuleProp(d, k) != nullptr ? &d.rule_style()->value(static_cast<size_t>(k)) : nullptr;
}

/// Edits a copy of property `k`'s value and sets it back.
template <typename Fn>
std::string EditRuleValue(Desk& d, int k, Fn&& fn) {
  if (RuleProp(d, k) == nullptr) return kNoRuleStyle;
  RulePropertyValue v = d.rule_style()->value(static_cast<size_t>(k));
  Status s = fn(&v);
  if (s.ok()) s = d.rule_style()->SetValue(static_cast<size_t>(k), v);
  return s.message;
}

}  // namespace

bool DeskHost::TakeRuleStyleRequest() { return std::exchange(impl_->shell.rule_style_requested, false); }
bool DeskHost::RuleStyleOpen() const { return impl_->desk->rule_style() != nullptr; }

std::string DeskHost::RuleStyleTitle() const {
  const auto& m = impl_->desk->rule_style();
  return m ? m->Title() : std::string();
}

std::string DeskHost::RuleStyleType() const {
  const auto& m = impl_->desk->rule_style();
  return m ? m->type() : std::string();
}

bool DeskHost::RuleStyleDirty() const {
  const auto& m = impl_->desk->rule_style();
  return m && m->dirty();
}

int DeskHost::RuleStylePropertyCount() const {
  const auto& m = impl_->desk->rule_style();
  return m ? static_cast<int>(m->properties().size()) : 0;
}

std::string DeskHost::RuleStylePropertyKey(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? p->key : std::string();
}

std::string DeskHost::RuleStylePropertyPage(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? p->page : std::string();
}

std::string DeskHost::RuleStylePropertyTitle(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? p->title : std::string();
}

int DeskHost::RuleStylePropertyKind(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? static_cast<int>(p->kind) : 0;
}

double DeskHost::RuleStylePropertyMin(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? p->min : 0;
}

double DeskHost::RuleStylePropertyMax(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? p->max : 0;
}

int DeskHost::RuleStylePropertyChoiceCount(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? static_cast<int>(p->choices.size()) : 0;
}

std::string DeskHost::RuleStylePropertyChoice(int k, int i) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  if (!p || i < 0 || i >= static_cast<int>(p->choices.size())) return std::string();
  return p->choices[static_cast<size_t>(i)];
}

bool DeskHost::RuleStylePropertyIcons(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p && p->icons;
}

int DeskHost::RuleIconCount() const { return static_cast<int>(fv::desk::RuleIconNames().size()); }

std::string DeskHost::RuleIconName(int i) const {
  const std::vector<std::string>& names = fv::desk::RuleIconNames();
  if (i < 0 || i >= static_cast<int>(names.size())) return std::string();
  return names[static_cast<size_t>(i)];
}

bool DeskHost::CopyRuleIcon(const std::string& name, int size, uint32_t rgba, uint8_t* out, size_t capacity) const {
  if (out == nullptr) return false;
  PixelBuffer buf;
  if (!fv::desk::RenderRuleIcon(name, size, rgba, &buf).ok()) return false;
  const size_t row = static_cast<size_t>(buf.Width()) * 4;
  if (capacity < row * buf.Height()) return false;
  for (int y = 0; y < buf.Height(); ++y) std::memcpy(out + row * y, buf.Row(y), row);
  return true;
}

int DeskHost::RuleStyleModeCount(int k) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  return p ? static_cast<int>(p->modes.size()) : 0;
}

int DeskHost::RuleStyleModeAt(int k, int i) const {
  const RuleProperty* p = RuleProp(*impl_->desk, k);
  if (!p || i < 0 || i >= static_cast<int>(p->modes.size())) return 0;
  return static_cast<int>(p->modes[static_cast<size_t>(i)]);
}

int DeskHost::RuleStyleMode(int k) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  return v ? static_cast<int>(v->mode) : 0;
}

std::string DeskHost::RuleStyleConstant(int k) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  return v ? v->constant : std::string();
}

std::string DeskHost::RuleStyleField(int k) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  return v ? v->field : std::string();
}

std::string DeskHost::RuleStyleExpression(int k) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  return v ? v->expression : std::string();
}

int DeskHost::RuleStyleStopCount(int k) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  return v ? static_cast<int>(v->stops.size()) : 0;
}

std::string DeskHost::RuleStyleStopKey(int k, int i) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  if (!v || i < 0 || i >= static_cast<int>(v->stops.size())) return std::string();
  return v->stops[static_cast<size_t>(i)].key;
}

std::string DeskHost::RuleStyleStopValue(int k, int i) const {
  const RulePropertyValue* v = RuleValue(*impl_->desk, k);
  if (!v || i < 0 || i >= static_cast<int>(v->stops.size())) return std::string();
  return v->stops[static_cast<size_t>(i)].value;
}

std::string DeskHost::SetRuleStyleMode(int k, int mode) {
  if (RuleProp(*impl_->desk, k) == nullptr) return kNoRuleStyle;
  if (mode < 0 || mode > static_cast<int>(RuleValueMode::kExpression)) return "no such mode";
  return impl_->desk->rule_style()->SetMode(static_cast<size_t>(k), static_cast<RuleValueMode>(mode)).message;
}

std::string DeskHost::SetRuleStyleConstant(int k, const std::string& value) {
  return EditRuleValue(*impl_->desk, k, [&](RulePropertyValue* v) {
    v->constant = value;
    return Status::Ok();
  });
}

std::string DeskHost::SetRuleStyleField(int k, const std::string& field) {
  return EditRuleValue(*impl_->desk, k, [&](RulePropertyValue* v) {
    v->field = field;
    return Status::Ok();
  });
}

std::string DeskHost::SetRuleStyleStop(int k, int i, const std::string& key, const std::string& value) {
  return EditRuleValue(*impl_->desk, k, [&](RulePropertyValue* v) {
    if (i < 0 || i >= static_cast<int>(v->stops.size())) return Status::Error(kInvalidArg, "no such row");
    v->stops[static_cast<size_t>(i)] = {key, value};
    return Status::Ok();
  });
}

std::string DeskHost::AddRuleStyleStop(int k) {
  return EditRuleValue(*impl_->desk, k, [&](RulePropertyValue* v) {
    RuleStop next;
    next.value = v->stops.empty() ? v->constant : v->stops.back().value;
    if (v->mode == RuleValueMode::kByField) {
      for (int n = static_cast<int>(v->stops.size()) + 1;; ++n) {
        next.key = "value " + std::to_string(n);
        if (std::none_of(v->stops.begin(), v->stops.end(), [&](const RuleStop& s) { return s.key == next.key; }))
          break;
      }
    } else {
      const double last = v->stops.empty() ? 0 : std::atof(v->stops.back().key.c_str());
      char buf[32];
      std::snprintf(buf, sizeof buf, "%g", v->stops.empty() ? last : last + 1);
      next.key = buf;
    }
    v->stops.push_back(std::move(next));
    return Status::Ok();
  });
}

std::string DeskHost::RemoveRuleStyleStop(int k, int i) {
  return EditRuleValue(*impl_->desk, k, [&](RulePropertyValue* v) {
    if (i < 0 || i >= static_cast<int>(v->stops.size())) return Status::Error(kInvalidArg, "no such row");
    v->stops.erase(v->stops.begin() + i);
    return Status::Ok();
  });
}

std::string DeskHost::FillRuleStyleValues(int k) {
  if (RuleProp(*impl_->desk, k) == nullptr) return kNoRuleStyle;
  return impl_->desk->rule_style()->FillFieldValues(static_cast<size_t>(k)).message;
}

std::string DeskHost::SetRuleStyleExpression(int k, const std::string& json) {
  return EditRuleValue(*impl_->desk, k, [&](RulePropertyValue* v) {
    v->expression = json;
    return Status::Ok();
  });
}

std::string DeskHost::VerifyRuleStyleExpression(int k, const std::string& json) const {
  if (RuleProp(*impl_->desk, k) == nullptr) return kNoRuleStyle;
  return impl_->desk->rule_style()->Verify(static_cast<size_t>(k), json).message;
}

int DeskHost::RuleStyleFieldCount(bool numeric) const {
  const auto& m = impl_->desk->rule_style();
  return m ? static_cast<int>(m->Fields(numeric).size()) : 0;
}

std::string DeskHost::RuleStyleFieldName(bool numeric, int i) const {
  const auto& m = impl_->desk->rule_style();
  if (!m) return std::string();
  const std::vector<std::string> fields = m->Fields(numeric);
  return i >= 0 && i < static_cast<int>(fields.size()) ? fields[static_cast<size_t>(i)] : std::string();
}

std::string DeskHost::RuleStyleLegend() const {
  const auto& m = impl_->desk->rule_style();
  return m ? m->legend() : std::string();
}

void DeskHost::SetRuleStyleLegend(const std::string& legend) {
  if (const auto& m = impl_->desk->rule_style()) m->SetLegend(legend);
}

double DeskHost::RuleStyleMinZoom() const {
  const auto& m = impl_->desk->rule_style();
  return m ? m->min_zoom() : 0;
}

double DeskHost::RuleStyleMaxZoom() const {
  const auto& m = impl_->desk->rule_style();
  return m ? m->max_zoom() : 24;
}

std::string DeskHost::SetRuleStyleZoomRange(double min_zoom, double max_zoom) {
  const auto& m = impl_->desk->rule_style();
  return m ? m->SetZoomRange(min_zoom, max_zoom).message : kNoRuleStyle;
}

std::string DeskHost::ApplyRuleStyle() {
  return impl_->EditRules([](Desk& d) { return d.ApplyRuleStyle(); });
}

std::string DeskHost::RevertRuleStyle() { return impl_->desk->RevertRuleStyle().message; }
void DeskHost::CloseRuleStyle() { impl_->desk->CloseRuleStyle(); }

uint32_t DeskHost::RuleColorRGBA(const std::string& css, uint32_t fallback) {
  FvColor c;
  if (!ParseCssColor(css, &c)) return fallback;
  return (uint32_t{c.r} << 24) | (uint32_t{c.g} << 16) | (uint32_t{c.b} << 8) | uint32_t{c.a};
}

std::string DeskHost::RuleColorText(uint32_t rgba) {
  const unsigned r = rgba >> 24, g = (rgba >> 16) & 0xff, b = (rgba >> 8) & 0xff, a = rgba & 0xff;
  char buf[48];
  if (a == 0xff) std::snprintf(buf, sizeof buf, "#%02x%02x%02x", r, g, b);
  else std::snprintf(buf, sizeof buf, "rgba(%u, %u, %u, %.3g)", r, g, b, a / 255.0);
  return buf;
}

// MARK: Properties sheet

bool DeskHost::TakeLayerPropertiesRequest() {
  const bool asked = impl_->shell.properties_requested;
  impl_->shell.properties_requested = false;
  return asked;
}

std::string DeskHost::LayerPropertiesTitle() const { return impl_->desk->properties_title(); }

int DeskHost::LayerPropertyCount() const { return static_cast<int>(impl_->desk->properties().size()); }

std::string DeskHost::LayerPropertySection(int i) const {
  const auto& rows = impl_->desk->properties();
  return i >= 0 && i < static_cast<int>(rows.size()) ? rows[i].section : std::string();
}

std::string DeskHost::LayerPropertyKey(int i) const {
  const auto& rows = impl_->desk->properties();
  return i >= 0 && i < static_cast<int>(rows.size()) ? rows[i].key : std::string();
}

std::string DeskHost::LayerPropertyValue(int i) const {
  const auto& rows = impl_->desk->properties();
  return i >= 0 && i < static_cast<int>(rows.size()) ? rows[i].value : std::string();
}

void DeskHost::SetPropertiesTabs(bool tabbed) { impl_->shell.properties_tabs = tabbed; }

int DeskHost::LayerPropertiesTabCount() const { return static_cast<int>(impl_->desk->PropertiesTabs().size()); }

int DeskHost::LayerPropertiesTabAt(int i) const {
  const std::vector<PropertiesTab> tabs = impl_->desk->PropertiesTabs();
  return i >= 0 && i < static_cast<int>(tabs.size()) ? static_cast<int>(tabs[i]) : -1;
}

std::string DeskHost::LayerPropertiesTabTitle(int i) const {
  const int tab = LayerPropertiesTabAt(i);
  return tab < 0 ? std::string() : PropertiesTabTitle(static_cast<PropertiesTab>(tab));
}

int DeskHost::LayerPropertiesTab() const { return static_cast<int>(impl_->desk->properties_tab()); }

bool DeskHost::LayerPropertiesDirty() const { return impl_->desk->LayerPropertiesDirty(); }

std::string DeskHost::ApplyLayerProperties() {
  Impl::StackEdit edit(*impl_);
  return impl_->desk->ApplyLayerProperties().message;
}

std::string DeskHost::RevertLayerProperties() { return impl_->desk->RevertLayerProperties().message; }

void DeskHost::CloseLayerProperties() { impl_->desk->CloseLayerProperties(); }

bool DeskHost::LayerGeneralOpen() const { return impl_->desk->layer_general() != nullptr; }

namespace {

LayerGeneral GeneralOf(const Desk& desk) {
  const auto& m = desk.layer_general();
  return m ? m->general() : LayerGeneral{};
}

}  // namespace

std::string DeskHost::LayerGeneralName() const { return GeneralOf(*impl_->desk).name; }
std::string DeskHost::LayerGeneralDescription() const { return GeneralOf(*impl_->desk).description; }
std::string DeskHost::LayerGeneralCredits() const { return GeneralOf(*impl_->desk).credits; }
std::string DeskHost::LayerGeneralMinScale() const { return FormatScale(GeneralOf(*impl_->desk).min_scale); }
std::string DeskHost::LayerGeneralMaxScale() const { return FormatScale(GeneralOf(*impl_->desk).max_scale); }

std::string DeskHost::SetLayerGeneral(const std::string& name, const std::string& description,
                                      const std::string& credits, const std::string& min_scale,
                                      const std::string& max_scale) {
  const auto& m = impl_->desk->layer_general();
  if (!m) return "no layer's Properties are open";
  LayerGeneral g;
  g.name = name;
  g.description = description;
  g.credits = credits;
  Status s = ParseScale(min_scale, &g.min_scale);
  if (s.ok()) s = ParseScale(max_scale, &g.max_scale);
  if (s.ok()) s = m->Set(g);
  return s.message;
}

namespace {

LayerDisplay DisplayOf(const Desk& desk) {
  const auto& m = desk.layer_display();
  return m ? m->display() : LayerDisplay{};
}

LayerSelection SelectionOf(const Desk& desk) {
  const auto& m = desk.layer_selection();
  return m ? m->selection() : LayerSelection{};
}

const FieldDef* DisplayField(const Desk& desk, int i) {
  const auto& m = desk.layer_display();
  if (!m || i < 0 || static_cast<size_t>(i) >= m->fields().size()) return nullptr;
  return &m->fields()[static_cast<size_t>(i)];
}

uint32_t PackColor(const FvColor& c) {
  return (uint32_t{c.r} << 24) | (uint32_t{c.g} << 16) | (uint32_t{c.b} << 8) | c.a;
}

QueryBuilderModel* QueryOf(const Desk& desk, int which) {
  if (which == DeskHost::kQuerySelect) return desk.select_query().get();
  if (which == DeskHost::kQueryRule) return desk.rule_filter().get();
  return desk.layer_query().get();
}

const QueryClause* ClauseOf(const Desk& desk, int which, int i) {
  const QueryBuilderModel* q = QueryOf(desk, which);
  if (q == nullptr || i < 0 || static_cast<size_t>(i) >= q->query().clauses.size()) return nullptr;
  return &q->query().clauses[static_cast<size_t>(i)];
}

}  // namespace

bool DeskHost::LayerDisplayOpen() const { return impl_->desk->layer_display() != nullptr; }
bool DeskHost::LayerDisplayHasOpacity() const {
  const auto& m = impl_->desk->layer_display();
  return m && m->has_opacity();
}
int DeskHost::LayerDisplayOpacity() const { return DisplayOf(*impl_->desk).opacity; }
int DeskHost::LayerDisplayFieldCount() const {
  const auto& m = impl_->desk->layer_display();
  return m ? static_cast<int>(m->fields().size()) : 0;
}
std::string DeskHost::LayerDisplayFieldName(int i) const {
  const FieldDef* f = DisplayField(*impl_->desk, i);
  return f ? f->name : std::string();
}
std::string DeskHost::LayerDisplayFieldTitle(int i) const {
  const FieldDef* f = DisplayField(*impl_->desk, i);
  return f ? f->DisplayTitle() : std::string();
}
std::string DeskHost::LayerDisplayMapTipField() const { return DisplayOf(*impl_->desk).map_tip_field; }
std::string DeskHost::LayerDisplayHyperlinkField() const { return DisplayOf(*impl_->desk).hyperlink_field; }

std::string DeskHost::SetLayerDisplay(int opacity, const std::string& map_tip_field,
                                      const std::string& hyperlink_field) {
  const auto& m = impl_->desk->layer_display();
  if (!m) return "no layer's Display tab is open";
  return m->Set(LayerDisplay{opacity, map_tip_field, hyperlink_field}).message;
}

bool DeskHost::LayerSelectionOpen() const { return impl_->desk->layer_selection() != nullptr; }
bool DeskHost::LayerSelectionUsesDefault() const { return SelectionOf(*impl_->desk).use_default; }
uint32_t DeskHost::LayerSelectionColor() const { return PackColor(SelectionOf(*impl_->desk).style.color); }
double DeskHost::LayerSelectionWidth() const { return SelectionOf(*impl_->desk).style.width_px; }
int DeskHost::LayerSelectionCount() const {
  const auto& m = impl_->desk->layer_selection();
  return m ? static_cast<int>(m->selected_count()) : 0;
}

std::string DeskHost::SetLayerSelection(bool use_default, uint32_t rgba, double width_px) {
  const auto& m = impl_->desk->layer_selection();
  if (!m) return "no layer's Selection tab is open";
  LayerSelection sel;
  sel.use_default = use_default;
  sel.style.color = FvColor{static_cast<unsigned char>(rgba >> 24), static_cast<unsigned char>(rgba >> 16),
                            static_cast<unsigned char>(rgba >> 8), static_cast<unsigned char>(rgba)};
  sel.style.width_px = width_px;
  return m->Set(sel).message;
}

// MARK: Query builder

bool DeskHost::QueryOpen(int which) const { return QueryOf(*impl_->desk, which) != nullptr; }

int DeskHost::QueryFieldCount(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  return q ? static_cast<int>(q->fields().size()) : 0;
}

std::string DeskHost::QueryFieldName(int which, int i) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  if (q == nullptr || i < 0 || static_cast<size_t>(i) >= q->fields().size()) return std::string();
  return q->fields()[static_cast<size_t>(i)].name;
}

std::string DeskHost::QueryFieldTitle(int which, int i) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  if (q == nullptr || i < 0 || static_cast<size_t>(i) >= q->fields().size()) return std::string();
  return q->fields()[static_cast<size_t>(i)].DisplayTitle();
}

int DeskHost::QueryOpCount() const { return kQueryOpCount; }
std::string DeskHost::QueryOpTitle(int op) const {
  return op >= 0 && op < kQueryOpCount ? desk::QueryOpTitle(static_cast<QueryOp>(op)) : std::string();
}
bool DeskHost::QueryOpTakesValue(int op) const {
  return op >= 0 && op < kQueryOpCount && desk::QueryOpTakesValue(static_cast<QueryOp>(op));
}

bool DeskHost::QueryMatchAny(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  return q != nullptr && q->query().match_any;
}

void DeskHost::SetQueryMatchAny(int which, bool any) {
  if (QueryBuilderModel* q = QueryOf(*impl_->desk, which)) q->SetMatchAny(any);
}

int DeskHost::QueryClauseCount(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  return q ? static_cast<int>(q->query().clauses.size()) : 0;
}

std::string DeskHost::QueryClauseField(int which, int i) const {
  const QueryClause* c = ClauseOf(*impl_->desk, which, i);
  return c ? c->field : std::string();
}

int DeskHost::QueryClauseOp(int which, int i) const {
  const QueryClause* c = ClauseOf(*impl_->desk, which, i);
  return c ? static_cast<int>(c->op) : 0;
}

std::string DeskHost::QueryClauseValue(int which, int i) const {
  const QueryClause* c = ClauseOf(*impl_->desk, which, i);
  return c ? c->value : std::string();
}

int DeskHost::AddQueryClause(int which) {
  QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  return q ? static_cast<int>(q->AddClause()) : -1;
}

std::string DeskHost::RemoveQueryClause(int which, int i) {
  QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  if (q == nullptr || i < 0) return "no such clause";
  return q->RemoveClause(static_cast<size_t>(i)).message;
}

std::string DeskHost::SetQueryClause(int which, int i, const std::string& field, int op, const std::string& value) {
  QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  if (q == nullptr || i < 0) return "no such clause";
  if (op < 0 || op >= kQueryOpCount) return "no such comparison";
  return q->SetClause(static_cast<size_t>(i), QueryClause{field, static_cast<QueryOp>(op), value}).message;
}

bool DeskHost::QueryIsCustom(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  return q != nullptr && q->custom();
}

std::string DeskHost::QueryExpression(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  std::string json;
  if (q == nullptr || !q->Expression(&json).ok()) return std::string();
  return json;
}

std::string DeskHost::QueryExpressionError(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  std::string json;
  return q != nullptr ? q->Expression(&json).message : std::string();
}

std::string DeskHost::SetQueryExpression(int which, const std::string& json) {
  QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  if (q == nullptr) return "no query is open";
  return q->SetExpression(json).message;
}

std::string DeskHost::QueryText(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  return q ? q->Text() : std::string();
}

std::string DeskHost::VerifyQuery(int which) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  if (q == nullptr) return "no query is open";
  std::vector<int64_t> ids;
  size_t tested = 0;
  // A definition query picks among all features; the others among those the layer shows.
  const Status s = q->Matching(which != kQueryDefinition, &ids, &tested);
  if (!s.ok()) return s.message;
  return std::to_string(ids.size()) + " of " + std::to_string(tested) + (tested == 1 ? " feature" : " features") +
         (ids.size() == 1 ? " matches" : " match");
}

std::string DeskHost::QuerySampleValues(int which, const std::string& field) const {
  const QueryBuilderModel* q = QueryOf(*impl_->desk, which);
  std::vector<std::string> values;
  if (q == nullptr || !q->SampleValues(field, 200, &values).ok()) return std::string();
  std::string out;
  for (const std::string& v : values) {
    if (!out.empty()) out += '\n';
    out += v;
  }
  return out;
}

// MARK: Select by Attributes

bool DeskHost::TakeSelectByAttributesRequest() {
  const bool asked = impl_->shell.select_requested;
  impl_->shell.select_requested = false;
  return asked;
}

std::string DeskHost::SelectByAttributesTitle() const {
  const auto& q = impl_->desk->select_query();
  if (!q) return std::string();
  const Overlay* o = q->overlay();
  return "Select by Attributes \u2014 " + (o != nullptr ? o->Name() : std::string());
}

int DeskHost::SelectionMethodCount() const { return 4; }

std::string DeskHost::SelectionMethodTitle(int method) const {
  return method >= 0 && method < 4 ? desk::SelectionMethodTitle(static_cast<SelectionMethod>(method)) : std::string();
}

std::string DeskHost::ApplySelectByAttributes(int method) {
  if (method < 0 || method >= 4) return "no such selection method";
  return impl_->desk->ApplySelectByAttributes(static_cast<SelectionMethod>(method)).message;
}

int DeskHost::SelectByAttributesSelectedCount() const {
  const auto& q = impl_->desk->select_query();
  const std::shared_ptr<Overlay> o = q ? impl_->desk->overlays().FindShared(q->overlay()) : nullptr;
  const auto* f = dynamic_cast<const FeatureOverlay*>(o.get());
  return f ? static_cast<int>(f->selection().size()) : 0;
}

void DeskHost::CloseSelectByAttributes() { impl_->desk->CloseSelectByAttributes(); }

// MARK: Map Data Sources dialog

bool DeskHost::TakeSourcesRequest() {
  const bool asked = impl_->shell.sources_requested;
  impl_->shell.sources_requested = false;
  return asked;
}

int DeskHost::ScanRootCount() const {
  return static_cast<int>(impl_->desk->ScanRoots().size());
}

std::string DeskHost::ScanRootAt(int index) const {
  const std::vector<std::string> roots = impl_->desk->ScanRoots();
  if (index < 0 || index >= static_cast<int>(roots.size())) return std::string();
  return roots[static_cast<size_t>(index)];
}

bool DeskHost::ScanRootReachable(int index) const {
  const std::string root = ScanRootAt(index);
  std::error_code ec;
  return !root.empty() && std::filesystem::is_directory(root, ec);
}

std::string DeskHost::AddScanRoot(const std::string& path) {
  return impl_->desk->AddScanRoot(path).message;
}

std::string DeskHost::RemoveScanRoot(const std::string& path) {
  return impl_->desk->RemoveScanRoot(path).message;
}

std::string DeskHost::GenerateCoverage() {
  return impl_->desk->GenerateCoverage().message;
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
  if (p.removing) return "Removing the old coverage";
  if (p.current.empty()) return "Building the map catalog";
  return "Scanning " + p.current;
}

void DeskHost::CancelJob() { impl_->desk->CancelCatalogBuild(); }

// MARK: Surface and input

void DeskHost::Resize(double width_pt, double height_pt, double display_scale,
                      double mm_per_point) {
  const double was = impl_->desk->map_view().View().DisplayScale();
  impl_->desk->map_view().Resize(width_pt, height_pt, display_scale, mm_per_point);
  if (impl_->desk->map_view().View().DisplayScale() != was) {
    Impl::StackEdit edit(*impl_);
    impl_->desk->DisplayScaleChanged();
  }
}

void DeskHost::SetClock(double (*clock)()) {
  impl_->desk->map_view().SetClock(clock);
}

void DeskHost::Hover(double x, double y) {
  Impl& m = *impl_;
  m.desk->map_view().Hover(view::PointF{x, y});
  m.have_hover = true;
  m.hover_x = x;
  m.hover_y = y;
  // Not a StackEdit: that would interrupt the frame being drawn on every mouse
  // move. While the worker holds the stack, this move's hint is skipped.
  if (m.stack_depth > 0 || !m.stack_mu.try_lock()) return;
  m.desk->OverlayHover(x, y);
  m.stack_mu.unlock();
  m.HoverBaseMap();
}

void DeskHost::HoverExit() {
  Impl& m = *impl_;
  m.have_hover = false;
  m.desk->map_view().HoverExit();
  m.desk->OverlayHoverExit();
  m.desk->SetMapHint(app::HintText());
}

void DeskHost::Impl::HoverBaseMap() {
  // Skipped while the worker draws the base map; the next move or frame retries.
  std::unique_lock<std::mutex> lock(base_mu, std::try_to_lock);
  if (!lock.owns_lock()) return;
  app::HintText hint;
  const view::Viewport& now = desk->map_view().View();
  GeoPoint g;
  double bx = 0, by = 0;
  if (have_hover && have_base_view && now.HasSurface() &&
      now.Projection().SurfaceToGeo(hover_x * now.DisplayScale(), hover_y * now.DisplayScale(), &g).ok() &&
      base_view.Projection().GeoToSurface(g, &bx, &by).ok())
    hint = base.HintAt(PixelPoint{static_cast<int>(std::lround(bx)), static_cast<int>(std::lround(by))});
  lock.unlock();
  desk->SetMapHint(hint);
}

std::string DeskHost::HoverToolTip() const { return impl_->desk->HoverToolTip(); }

void DeskHost::PointerDown(double x, double y) { PointerDown(x, y, false); }

void DeskHost::PointerDown(double x, double y, bool shift) { PointerDown(x, y, shift, 1); }

void DeskHost::PointerDown(double x, double y, bool shift, int clicks) {
  Impl& m = *impl_;
  m.pointer_on_overlay = false;
  if (m.desk->Editing()) {
    Impl::StackEdit edit(m);
    m.pointer_on_overlay = m.desk->OverlayPointerDown(x, y, shift, clicks);
  }
  if (!m.pointer_on_overlay) m.desk->map_view().PointerDown(view::PointF{x, y});
}

namespace {

/// Appends `node`'s children to `out` in preorder, submenus before their entries.
void FlattenMapMenu(const app::MenuNode& node, int depth,
                    std::vector<MapMenuEntry>* out) {
  for (const app::MenuNode& child : node.children) {
    MapMenuEntry e;
    e.entry.depth = depth;
    e.entry.label = child.label;
    e.entry.icon = child.icon;
    e.enabled = child.enabled;
    e.checked = child.checked;
    if (child.is_separator()) {
      e.entry.kind = kMenuSeparator;
    } else if (!child.children.empty()) {
      e.entry.kind = kMenuSubmenu;
    } else {
      e.entry.kind = kMenuCommand;
      e.action = child.action;
      if (!e.action) e.enabled = false;
    }
    out->push_back(std::move(e));
    if (!child.children.empty()) FlattenMapMenu(child, depth + 1, out);
  }
}

}  // namespace

int DeskHost::OpenMapMenu(double x, double y) {
  Impl& m = *impl_;
  m.map_menu.clear();
  app::MenuNode root;
  {
    Impl::StackEdit edit(m);
    root = m.desk->MapContextMenu(x, y);
  }
  FlattenMapMenu(root, 0, &m.map_menu);
  return static_cast<int>(m.map_menu.size());
}

HostMenuEntry DeskHost::MapMenuEntryAt(int index) const {
  const auto& menu = impl_->map_menu;
  return index >= 0 && index < static_cast<int>(menu.size()) ? menu[index].entry : HostMenuEntry();
}

bool DeskHost::MapMenuEntryEnabled(int index) const {
  const auto& menu = impl_->map_menu;
  return index >= 0 && index < static_cast<int>(menu.size()) && menu[index].enabled;
}

bool DeskHost::MapMenuEntryChecked(int index) const {
  const auto& menu = impl_->map_menu;
  return index >= 0 && index < static_cast<int>(menu.size()) && menu[index].checked;
}

void DeskHost::RunMapMenuEntry(int index) {
  Impl& m = *impl_;
  if (index < 0 || index >= static_cast<int>(m.map_menu.size())) return;
  const std::function<void()> action = std::move(m.map_menu[index].action);
  m.map_menu.clear();
  if (!action) return;
  Impl::StackEdit edit(m);
  action();
}

bool DeskHost::TakeItemInfoRequest() {
  const bool asked = impl_->shell.info_requested;
  impl_->shell.info_requested = false;
  return asked;
}

std::string DeskHost::ItemInfoTitle() const { return impl_->desk->item_info().title; }

int DeskHost::ItemInfoCount() const { return static_cast<int>(impl_->desk->item_info().rows.size()); }

std::string DeskHost::ItemInfoSection(int i) const {
  const auto& rows = impl_->desk->item_info().rows;
  return i >= 0 && i < static_cast<int>(rows.size()) ? rows[i].section : std::string();
}

std::string DeskHost::ItemInfoKey(int i) const {
  const auto& rows = impl_->desk->item_info().rows;
  return i >= 0 && i < static_cast<int>(rows.size()) ? rows[i].key : std::string();
}

std::string DeskHost::ItemInfoValue(int i) const {
  const auto& rows = impl_->desk->item_info().rows;
  return i >= 0 && i < static_cast<int>(rows.size()) ? rows[i].value : std::string();
}

void DeskHost::PanPointerDown(double x, double y) {
  Impl& m = *impl_;
  m.pointer_on_overlay = false;
  m.desk->map_view().PointerDown(view::PointF{x, y});
}

void DeskHost::PointerDrag(double x, double y) {
  Impl& m = *impl_;
  if (!m.pointer_on_overlay) {
    m.desk->map_view().PointerDrag(view::PointF{x, y});
    return;
  }
  Impl::StackEdit edit(m);
  m.desk->OverlayPointerDrag(x, y);
}

void DeskHost::PointerUp(double x, double y) {
  Impl& m = *impl_;
  if (!m.pointer_on_overlay) {
    m.desk->map_view().PointerUp(view::PointF{x, y});
    return;
  }
  m.pointer_on_overlay = false;
  Impl::StackEdit edit(m);
  m.desk->OverlayPointerUp(x, y);
}

bool DeskHost::KeyDown(int key, uint32_t text, bool shift, bool ctrl, bool alt, bool meta) {
  Impl& m = *impl_;
  if (!m.desk->Editing()) return false;
  KeyEvent e;
  e.key = key;
  e.text = text;
  e.shift = shift;
  e.ctrl = ctrl;
  e.alt = alt;
  e.meta = meta;
  Impl::StackEdit edit(m);
  return m.desk->OverlayKeyDown(e);
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
  const CatalogBuild* build = m.desk->catalog_build();
  if (build != nullptr && build->Progress().finished) {
    // Picking up the build re-resolves the elevation overlays' source.
    Impl::StackEdit edit(m);
    m.desk->PollCatalogBuild();
    t.job_changed = true;
    ++m.catalog_epoch;  // the catalog's rows changed under the same path
  }
  std::string job = m.JobLine();
  if (job != m.ticked_job) {
    m.ticked_job = std::move(job);
    t.job_changed = true;
  }
  // Settings changed outside an options dialog, such as the recent overlays.
  // One attempt per change: a failed write is logged, not retried each tick.
  if (!m.user_path.empty() && m.user.Generation() != m.saved_user_gen) {
    m.saved_user_gen = m.user.Generation();
    const Status saved = m.user.Save(m.user_path);
    if (!saved.ok()) FV_LOG_WARNING("user settings not saved: " << saved.message);
  }
  if (m.desk->menus().Generation() != m.ticked_menus_gen) {
    m.ticked_menus_gen = m.desk->menus().Generation();
    t.menus_changed = true;
  }
  if (m.shell.options_requested >= 0) t.options_requested = true;
  if (m.shell.sources_requested) t.sources_requested = true;
  if (m.shell.profile_requested) t.profile_requested = true;
  if (m.shell.layers_requested) t.layers_requested = true;
  if (m.shell.layers_hide_requested) t.layers_hide_requested = true;
  if (m.shell.find_requested) t.find_requested = true;
  if (m.RefreshLayers()) t.layers_changed = true;
  if (m.shell.form_requested) t.form_requested = true;
  if (m.shell.table_requested) t.table_requested = true;
  if (m.shell.style_requested) t.style_requested = true;
  if (m.shell.fields_requested) t.fields_requested = true;
  if (m.shell.new_layer_requested) t.new_layer_requested = true;
  if (m.shell.properties_requested) t.properties_requested = true;
  if (m.shell.info_requested) t.info_requested = true;
  if (m.shell.select_requested) t.select_requested = true;
  if (m.shell.add_rule_requested) t.add_rule_requested = true;
  if (m.shell.rule_rename_requested) t.rule_rename_requested = true;
  if (m.shell.rule_type_requested) t.rule_type_requested = true;
  if (m.shell.rule_filter_requested) t.rule_filter_requested = true;
  if (m.shell.rule_style_requested) t.rule_style_requested = true;
  const int table_row = m.desk->SelectedTableRow();
  if (m.desk->FeatureViewsGeneration() != m.ticked_features_gen || table_row != m.ticked_table_row) {
    m.ticked_features_gen = m.desk->FeatureViewsGeneration();
    m.ticked_table_row = table_row;
    t.features_changed = true;
  }
  if (m.desk->UpdateRouteProfile()) t.profile_changed = true;
  if (m.desk->catalog_path() != m.ticked_catalog) {
    m.ticked_catalog = m.desk->catalog_path();
    t.catalog_changed = true;
  }
  if (m.shell.invalidate.exchange(false)) {
    m.overlay_epoch.fetch_add(1);
    t.redraw = true;
  }
  view::MapView& mv = m.desk->map_view();
  mv.Advance();
  const view::Viewport& live = mv.View();
  if (mv.Generation() != m.ticked_view_gen) {
    m.ticked_view_gen = mv.Generation();
    t.redraw = true;  // the placement moved
  }
  const BaseKey key{mv.FrameGeneration(), m.desk->catalog_path(), m.catalog_epoch,
                    m.desk->MapStyleGeneration()};
  const uint64_t epoch = m.overlay_epoch.load();
  // What to draw: the live view at rest; while panning, a band larger than
  // the screen, replaced one draw ahead of the screen leaving it. A pan
  // never interrupts a draw, so the map keeps filling in under the drag.
  std::optional<view::Viewport> next;
  bool new_base = false;
  bool pan = false;
  if (live.HasSurface()) {
    if (!m.requested_any || key != m.requested_base) {
      next = live;
      new_base = true;
    } else if (mv.Panning()) {
      // A full frame in flight is abandoned for the pan; a band is not.
      if (!(m.requested_pan && m.scheduler->Busy())) {
        const view::PointF v = mv.PanVelocity();
        const double draw = std::clamp(m.draw_seconds.load(), 1.0 / 120, 0.5);
        const double horizon = 1.5 * draw + 1.0 / 60;
        if (!FrameCovers(m.requested_view, live, view::PointF{v.x * horizon, v.y * horizon})) {
          const double margin = std::clamp(m.settings.GetDouble("display.pan_band_margin", 0.25), 0.0, 1.0);
          next = PanBand(live, margin, view::PointF{v.x * draw, v.y * draw});
          new_base = true;
          pan = true;
        }
      }
    } else if (m.requested_view != live) {
      next = live;
      new_base = true;
    }
    if (!next && epoch != m.requested_epoch) {
      next = m.requested_view;
      pan = m.requested_pan;
    }
  }
  if (next) {
    if (new_base) ++m.base_seq;
    m.requested_base = key;
    m.requested_view = *next;
    m.requested_pan = pan;
    ++m.seq;
    Job job;
    job.has_product = mv.HasProduct();
    if (job.has_product) job.product = mv.Product();
    job.catalog_path = key.catalog_path;
    job.base_seq = m.base_seq;
    job.overlay_epoch = epoch;
    job.map_style = key.map_style;
    job.pan = pan;
    {
      std::lock_guard<std::mutex> lock(m.jobs_mu);
      m.jobs[m.seq] = std::move(job);
    }
    m.latest_base_seq.store(m.base_seq);
    m.latest_seq.store(m.seq);
    m.scheduler->Request(*next, m.seq);
    m.requested_epoch = epoch;
    m.requested_any = true;
    t.redraw = true;
  }
  std::shared_ptr<const view::Frame> last = m.scheduler->LastFrame();
  if (last != m.shown) {
    m.shown = std::move(last);
    t.new_frame = true;
    t.redraw = true;
    // The base map under a still pointer may have changed with the frame.
    if (m.have_hover) m.HoverBaseMap();
  }
  std::string status = m.StatusLine() + StatusPosition() + '\n' + StatusElevation() + '\n' +
                       m.desk->CurrentStatus().feedback + '\n' + m.desk->HoverToolTip();
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
std::string DeskHost::StatusMessage() const {
  return impl_->Message(impl_->desk->CurrentStatus());
}

std::string DeskHost::StatusPosition() const {
  const view::MapView& mv = impl_->desk->map_view();
  if (!mv.Hovering() || !mv.View().HasSurface()) return std::string();
  return FormatPosition(mv.View().GeoAt(mv.HoverPoint()));
}

std::string DeskHost::StatusElevation() const {
  const view::MapView& mv = impl_->desk->map_view();
  if (!mv.Hovering() || !mv.View().HasSurface()) return std::string();
  double feet = 0;
  if (!impl_->desk->ElevationFeetAt(mv.View().GeoAt(mv.HoverPoint()), &feet)) return std::string();
  return FormatFeet(feet);
}

std::string DeskHost::StatusFeedback() const {
  const StatusBar bar = impl_->desk->CurrentStatus();
  const std::string message = impl_->Message(bar);
  if (bar.feedback.empty()) return message;
  if (message.empty()) return bar.feedback;
  return bar.feedback + " \xC2\xB7 " + message;
}

}  // namespace desk
}  // namespace fv
