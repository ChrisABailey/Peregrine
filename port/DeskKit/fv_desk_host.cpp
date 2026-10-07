// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_host.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

#include "fv_desk_base_map.h"
#include "fv_desk_desk.h"
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

/// A shell for a host whose UI is not wired to the core's questions yet:
/// notifications are queued for the UI to poll, and every question is
/// answered as a cancel.
class QueuedShell : public DeskShell {
 public:
  std::deque<std::string> errors;
  bool invalidate = false;

  SaveAnswer AskSave(const std::string&) override { return SaveAnswer::kCancel; }
  std::vector<std::string> ChooseFilesToOpen(const app::FileTypeDesc&) override { return {}; }
  std::pair<std::string, int> ChooseSaveSpec(const app::FileTypeDesc&,
                                             const std::string&) override {
    return {std::string(), 0};
  }
  std::optional<int> ChooseFromList(const std::string&, const std::vector<std::string>&) override {
    return std::nullopt;
  }
  bool ConfirmRevert(const std::string&) override { return false; }
  void SetCursor(app::CursorId) override {}
  void ShowHint(const app::HintText&) override {}
  void ShowContextMenu(PixelPoint, const app::MenuNode&) override {}
  void RequestInvalidate() override { invalidate = true; }
  void OnEditorChanged(const app::TypeId&, app::OverlayEditor*) override {}
  void ReportError(const Status& s) override { errors.push_back(s.message); }
  void MenusChanged() override {}
  void Quit() override {}
  void ShowOverlayOptions(std::shared_ptr<OptionsModel>) override {}
};

/// What the worker needs besides the viewport to draw one generation.
struct Job {
  bool has_product = false;
  view::LadderProduct product;
  std::string catalog_path;
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
  QueuedShell shell;
  std::unique_ptr<Desk> desk;

  uint64_t requested_gen = 0;
  bool requested_any = false;
  std::shared_ptr<const view::Frame> shown;
  std::string status_cache;

  std::mutex jobs_mu;
  std::map<uint64_t, Job> jobs;  // by generation; pruned as frames render

  // Touched only by the render worker.
  BaseMapRenderer base;
  std::string worker_catalog_path;

  // Last, so it is destroyed (and its worker joined) before what it uses.
  std::unique_ptr<view::RenderScheduler> scheduler;

  std::shared_ptr<const view::Frame> Render(const view::Viewport& v, uint64_t gen);
  std::string StatusLine() const;
};

std::shared_ptr<const view::Frame> DeskHost::Impl::Render(const view::Viewport& v, uint64_t gen) {
  Job job;
  {
    std::lock_guard<std::mutex> lock(jobs_mu);
    auto it = jobs.find(gen);
    if (it != jobs.end()) job = it->second;
    jobs.erase(jobs.begin(), jobs.upper_bound(gen));
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
  }

  CpuCanvas canvas(v.PixelWidth(), v.PixelHeight());
  canvas.Clear(FvColor{0, 0, 0, 255});
  // A failed draw still delivers what was drawn; the next view retries.
  if (job.has_product) base.Render(v, job.product, canvas);

  auto f = std::make_shared<view::Frame>();
  f->viewport = v;
  f->generation = gen;
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

DeskHost* DeskHost::Create() { return new DeskHost(); }

DeskHost::DeskHost() : impl_(std::make_unique<Impl>()) {
  RegisterBuiltinFormats();
  impl_->desk = std::make_unique<Desk>(impl_->shell, impl_->settings);
  Impl* impl = impl_.get();
  impl_->scheduler = std::make_unique<view::RenderScheduler>(
      [impl](const view::Viewport& v, uint64_t gen) { return impl->Render(v, gen); },
      [](std::shared_ptr<const view::Frame>) {});
}

DeskHost::~DeskHost() { impl_->scheduler.reset(); }

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
  const Status s = impl_->desk->Execute(command_id);
  return s.ok() ? std::string() : s.message;
}

std::string DeskHost::TakeError() {
  if (impl_->shell.errors.empty()) return std::string();
  std::string e = std::move(impl_->shell.errors.front());
  impl_->shell.errors.pop_front();
  return e;
}

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
  const view::MapView& mv = m.desk->map_view();
  const uint64_t gen = mv.Generation();
  if (mv.View().HasSurface() && (!m.requested_any || gen != m.requested_gen)) {
    Job job;
    job.has_product = mv.HasProduct();
    if (job.has_product) job.product = mv.Product();
    job.catalog_path = m.desk->catalog_path();
    {
      std::lock_guard<std::mutex> lock(m.jobs_mu);
      m.jobs[gen] = std::move(job);
    }
    m.scheduler->Request(mv.View(), gen);
    m.requested_gen = gen;
    m.requested_any = true;
    t.redraw = true;
  }
  std::shared_ptr<const view::Frame> last = m.scheduler->LastFrame();
  if (last != m.shown) {
    m.shown = std::move(last);
    t.new_frame = true;
    t.redraw = true;
  }
  if (m.shell.invalidate) {
    m.shell.invalidate = false;
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
