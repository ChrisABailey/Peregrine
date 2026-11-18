// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "ais_traffic.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "fvkit/canvas/canvas.h"

namespace peregrine_ais {

namespace {

using fv::app::PropertySpec;
using fv::app::PropertyType;
using fv::app::PropertyValue;

constexpr double kMetresPerNm = 1852.0;
constexpr double kStepS = 0.25;
/// A target leaves the alarm only once it is this far past either threshold,
/// so one sitting on the edge does not alarm on every report.
constexpr double kAlarmHysteresis = 1.2;

double EpochNow() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

PropertySpec Spec(const char* key, const char* label, const char* group, PropertyValue def,
                  double lo = 0.0, double hi = 0.0, const char* help = "") {
  PropertySpec s;
  s.key = key;
  s.label = label;
  s.group = group;
  s.type = def.type;
  s.default_value = std::move(def);
  s.min = lo;
  s.max = hi;
  s.help = help;
  return s;
}

/// The package's own properties, listed after `fv::AisOverlay`'s.
std::vector<PropertySpec> FeedSpecs() {
  std::vector<PropertySpec> v;
  v.push_back(Spec("open_waters", "Open Waters network", "Open Waters", PropertyValue::Bool(true),
                   0, 0,
                   "Live traffic from openwaters.io for the area in view, over the internet. "
                   "Each source's credit line is shown under the map."));
  v.push_back(Spec("receiver", "Listen to a receiver", "Local Receiver",
                   PropertyValue::Bool(false), 0, 0,
                   "An AIS receiver or boat network sending NMEA 0183 (!AIVDM) over TCP or UDP."));
  PropertySpec proto = Spec("receiver_protocol", "Protocol", "Local Receiver",
                            PropertyValue::Choice(0));
  proto.choices = {"TCP", "UDP"};
  v.push_back(proto);
  v.push_back(Spec("receiver_host", "Address", "Local Receiver",
                   PropertyValue::String("192.168.1.1"), 0, 0,
                   "The receiver's address for TCP. UDP listens on every interface."));
  v.push_back(Spec("receiver_port", "Port", "Local Receiver", PropertyValue::Int(10110), 1, 65535,
                   "10110 is the NMEA 0183 port; many Wi-Fi receivers use 2000."));
  v.push_back(Spec("cpa_alarm", "Collision alarm", "Collision Alarm", PropertyValue::Bool(true), 0,
                   0, "Warns when a target will pass within the distance below, within the time "
                      "below, of the ownship."));
  v.push_back(Spec("cpa_nm", "Closest approach under (nm)", "Collision Alarm",
                   PropertyValue::Double(0.5), 0.05, 10.0));
  v.push_back(Spec("tcpa_minutes", "Within (minutes)", "Collision Alarm",
                   PropertyValue::Double(10.0), 1.0, 60.0));
  return v;
}

bool IsFeedKey(const std::string& key) {
  for (const PropertySpec& s : FeedSpecs())
    if (s.key == key) return true;
  return false;
}

/// Moving kinds only: base stations and aids to navigation do not close.
bool CanCollide(const fv::ais::Vessel& v) {
  return !v.own_ship && v.kind != fv::ais::TargetKind::kBaseStation &&
         v.kind != fv::ais::TargetKind::kAidToNavigation;
}

std::string AlarmText(const fv::ais::Vessel& v, const fv::ais::CpaResult& r) {
  const std::string who =
      v.name.empty() ? "MMSI " + std::to_string(v.mmsi)
                     : v.name + " (MMSI " + std::to_string(v.mmsi) + ")";
  char buf[200];
  std::snprintf(buf, sizeof buf,
                " passes %.2f nm from the ownship in %.0f min; now %.1f nm, bearing %03.0f\xC2\xB0.",
                r.cpa_m / kMetresPerNm, std::max(0.0, r.tcpa_s) / 60.0, r.range_m / kMetresPerNm,
                r.bearing_deg);
  return "AIS collision warning: " + who + buf;
}

std::string ReceiverStateText(fv::ReceiverState s) {
  switch (s) {
    case fv::ReceiverState::kOff: return "off";
    case fv::ReceiverState::kConnecting: return "connecting";
    case fv::ReceiverState::kLive: return "receiving";
    case fv::ReceiverState::kSilent: return "silent";
    case fv::ReceiverState::kRetrying: return "retrying";
  }
  return "";
}

}  // namespace

TrafficOverlay::TrafficOverlay(std::string user_agent, bool run_worker)
    : fv::AisOverlay("AIS Traffic"),
      user_agent_(std::move(user_agent)),
      run_worker_(run_worker),
      stream_([this](std::string_view p) { OnPayload(p); }) {
  specs_ = fv::AisOverlay::Describe();
  for (PropertySpec& s : FeedSpecs()) specs_.push_back(std::move(s));
  settings_.receiver_config.host = "192.168.1.1";
}

TrafficOverlay::~TrafficOverlay() {
  StopWorker();
  stream_.Close();
}

void TrafficOverlay::SetHost(fv::app::OverlayHost* host) {
  fv::app::HostAware::SetHost(host);
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    label_font_ = host != nullptr ? host->LabelFont() : std::string();
    display_scale_ = host != nullptr ? host->DisplayScale() : 1.0;
    SetSymbolDpiScale(display_scale_);
  }
  if (!run_worker_) return;
  if (host != nullptr)
    StartWorker();
  else
    StopWorker();
}

void TrafficOverlay::StartWorker() {
  if (worker_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    stop_ = false;
  }
  worker_ = std::thread(&TrafficOverlay::WorkerLoop, this);
}

void TrafficOverlay::StopWorker() {
  if (!worker_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  worker_.join();
  stream_.Close();
  stream_wanted_ = false;
  receiver_.reset();
}

void TrafficOverlay::WorkerLoop() {
  std::unique_lock<std::mutex> lock(wake_mutex_);
  while (!stop_) {
    lock.unlock();
    if (Step(EpochNow())) {
      if (fv::app::OverlayHost* h = host()) h->RequestRedraw();
    }
    lock.lock();
    wake_.wait_for(lock, std::chrono::duration<double>(kStepS), [this] { return stop_; });
  }
}

bool TrafficOverlay::Step(double now_s) {
  const FeedSettings s = settings();
  StepReceiver(s, now_s);
  StepOpenWaters(s, now_s);

  fv::PositionFix own;
  fv::app::OverlayHost* h = host();
  const bool has_own = h != nullptr && h->OwnshipFix(&own);

  std::vector<std::string> alarms;
  bool changed = false;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string before = StatusLine();
    const std::set<std::uint32_t> alarmed_before = alarmed_;
    own_ = has_own ? own : fv::PositionFix();
    SetOwnship(own_);
    changed = Tick(now_s);
    alarms = CheckCpa(s, now_s);
    changed = changed || alarmed_ != alarmed_before || StatusLine() != before;
  }
  if (h != nullptr)
    for (const std::string& a : alarms) h->RaiseAlarm(a);
  return changed;
}

void TrafficOverlay::StepReceiver(const FeedSettings& s, double now_s) {
  if (!s.receiver) {
    if (receiver_) {
      receiver_.reset();
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      receiver_status_.clear();
    }
    return;
  }
  if (!receiver_ || receiver_config_ != s.receiver_config) {
    receiver_config_ = s.receiver_config;
    receiver_ = std::make_unique<fv::NmeaReceiver>(fv::NmeaReceiver::FactoryFor(receiver_config_));
    receiver_->Start(now_s);
  }
  fv::ReceiverBatch batch;
  receiver_->Poll(now_s, &batch);
  for (std::string& line : batch.ais_lines) {
    fv::AisOverlay::Input in;
    in.line = std::move(line);
    in.heard_s = now_s;
    Push(std::move(in));
  }
  // Position fixes are left to the moving map's own feed (Overlay ▸ Moving Map ▸ Feed…).
  std::string text = "receiver " + receiver_->description() + " " +
                     ReceiverStateText(receiver_->state());
  if (receiver_->state() == fv::ReceiverState::kRetrying && !receiver_->last_error().empty())
    text += " (" + receiver_->last_error() + ")";
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  receiver_status_ = std::move(text);
}

void TrafficOverlay::StepOpenWaters(const FeedSettings& s, double now_s) {
  std::optional<fv::GeoRect> view;
  fv::ais::OpenWatersLimits limits;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    view = view_;
    limits = limits_;
  }
  if (!s.open_waters) {
    if (stream_wanted_ || table_from_open_waters_) {
      stream_.Close();
      stream_wanted_ = false;
      // The table does not track sources; the receiver's targets return with their next reports.
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      table().Clear();
      table_from_open_waters_ = false;
      ow_status_.clear();
    }
    return;
  }
  if (!view) return;

  const StreamState state = stream_.state();
  std::string status;
  if (state == StreamState::kLive) {
    backoff_s_ = 2.0;
    status = "Open Waters live";
  } else if (state == StreamState::kConnecting) {
    status = "Open Waters connecting";
  } else if (state == StreamState::kFailed && stream_wanted_) {
    // A failed stream waits out the backoff before the reconnect below.
    next_connect_s_ = std::max(next_connect_s_, now_s + backoff_s_);
    backoff_s_ = std::min(backoff_s_ * 2.0, 60.0);
    stream_.Close();
    stream_wanted_ = false;
  }
  if (state == StreamState::kOff || state == StreamState::kFailed) {
    const std::string err = stream_.last_error();
    status = err.empty() ? "Open Waters connecting" : "Open Waters retrying (" + err + ")";
  }

  const bool serves = stream_wanted_ && fv::ais::OpenWatersBoxServes(box_, *view, limits);
  if (!serves && now_s >= next_connect_s_) {
    box_ = fv::ais::OpenWatersBox(*view, limits);
    std::vector<std::string> headers = {"User-Agent: " + user_agent_};
    stream_.Open(fv::ais::OpenWatersStreamUrl(box_, true), std::move(headers));
    stream_wanted_ = true;
    table_from_open_waters_ = true;
    next_connect_s_ = now_s + 60.0 / std::max(1, limits.connects_per_min);
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ow_status_ = std::move(status);
}

void TrafficOverlay::OnPayload(std::string_view payload) {
  fv::ais::OpenWatersFrame frame;
  if (!fv::ais::ParseOpenWatersFrame(payload, &frame)) return;
  if (frame.kind == fv::ais::OpenWatersFrameKind::kWelcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    limits_ = frame.limits;
    return;
  }
  PushFrame(frame);
}

std::vector<std::string> TrafficOverlay::CheckCpa(const FeedSettings& s, double now_s) {
  std::vector<std::string> fresh;
  const fv::PositionFix& own = own_;
  if (!s.cpa_alarm || !own.has_position) {
    alarmed_.clear();
    return fresh;
  }
  std::set<std::uint32_t> next;
  table().ForEach([&](const fv::ais::Vessel& v) {
    if (!CanCollide(v) || !v.has_position || !Shows(v) || table().IsStale(v, now_s)) return;
    const fv::ais::CpaResult r = fv::ais::ComputeCpa(own, v, now_s);
    if (!r.valid || r.tcpa_s < 0.0) return;
    const bool was = alarmed_.count(v.mmsi) > 0;
    const double k = was ? kAlarmHysteresis : 1.0;
    if (r.cpa_m > s.cpa_m * k || r.tcpa_s > s.tcpa_s * k) return;
    next.insert(v.mmsi);
    if (!was) fresh.push_back(AlarmText(v, r));
  });
  alarmed_ = std::move(next);
  return fresh;
}

std::set<std::uint32_t> TrafficOverlay::Alarmed() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return alarmed_;
}

std::string TrafficOverlay::StatusLine() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::string line = "AIS";
  std::string sources;
  for (const std::string* part : {&ow_status_, &receiver_status_}) {
    if (part->empty()) continue;
    sources += sources.empty() ? ": " : " \xC2\xB7 ";
    sources += *part;
  }
  if (sources.empty()) sources = ": no source on";
  std::size_t shown = 0;
  table().ForEach([&](const fv::ais::Vessel& v) {
    if (!v.own_ship && v.has_position && Shows(v)) ++shown;
  });
  return line + sources + " \xC2\xB7 " + std::to_string(shown) +
         (shown == 1 ? " target" : " targets");
}

std::vector<std::string> TrafficOverlay::Credits() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<const fv::ais::Vessel*> heard;
  table().ForEach([&](const fv::ais::Vessel& v) {
    if (!v.source_text.empty()) heard.push_back(&v);
  });
  std::sort(heard.begin(), heard.end(), [](const fv::ais::Vessel* a, const fv::ais::Vessel* b) {
    return a->first_heard_s < b->first_heard_s ||
           (a->first_heard_s == b->first_heard_s && a->mmsi < b->mmsi);
  });
  std::vector<std::string> out;
  for (const fv::ais::Vessel* v : heard)
    if (std::find(out.begin(), out.end(), v->source_text) == out.end())
      out.push_back(v->source_text);
  return out;
}

FeedSettings TrafficOverlay::settings() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return settings_;
}

fv::Status TrafficOverlay::OnDraw(const fv::MapProjection& proj, fv::ICanvas& canvas) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (proj.Ready()) view_ = proj.VmapBounds();
  fv::Status s = fv::AisOverlay::OnDraw(proj, canvas);
  if (!s.ok()) return s;

  fv::Pen ring;
  ring.color = fv::FvColor{230, 0, 0, 255};
  ring.width = std::max(2, static_cast<int>(std::lround(2.0 * display_scale_)));
  const double r = 14.0 * display_scale_;
  for (std::uint32_t mmsi : alarmed_) {
    const fv::ais::Vessel* v = table().Find(mmsi);
    if (v == nullptr || !v->has_position) continue;
    double x = 0, y = 0;
    if (!proj.GeoToSurface(fv::GeoPoint{v->lat_deg, v->lon_deg}, &x, &y).ok()) continue;
    const fv::PixelRect box{static_cast<int>(std::lround(x - r)),
                            static_cast<int>(std::lround(y - r)),
                            static_cast<int>(std::lround(2 * r)),
                            static_cast<int>(std::lround(2 * r))};
    canvas.DrawEllipse(box, nullptr, &ring);
  }
  return DrawStatus(canvas);
}

fv::Status TrafficOverlay::DrawStatus(fv::ICanvas& canvas) {
  std::vector<std::string> lines = Credits();
  lines.insert(lines.begin(), StatusLine());
  fv::TextStyle style;
  style.font_path = label_font_;
  style.size = 11.0 * display_scale_;
  style.color = fv::FvColor{20, 20, 20, 255};
  const int pad = static_cast<int>(std::lround(4.0 * display_scale_));
  const int line_h = static_cast<int>(std::lround(style.size * 1.35));
  int width = 0;
  for (const std::string& l : lines) {
    fv::PixelSize ext{0, 0};
    if (canvas.GetTextExtent(l, style, &ext).ok()) width = std::max(width, ext.width);
  }
  const fv::PixelSize size = canvas.Size();
  const int height = line_h * static_cast<int>(lines.size()) + pad * 2;
  const fv::PixelRect back{0, size.height - height, width + pad * 2, height};
  const fv::Brush fill{fv::FvColor{255, 255, 255, 190}};
  canvas.DrawRectangle(back, &fill, nullptr);
  int y = size.height - height + pad;
  for (const std::string& l : lines) {
    y += line_h;
    canvas.DrawTextString(l, pad, y - line_h / 4, style);
  }
  return fv::Status::Ok();
}

void TrafficOverlay::HitTestPoint(const fv::MapProjection& proj, fv::PixelPoint p,
                                  double tolerance_px, std::vector<fv::app::HitItem>& out) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  fv::AisOverlay::HitTestPoint(proj, p, tolerance_px, out);
}

std::optional<fv::app::ItemInfoSheet> TrafficOverlay::InfoAt(const fv::MapProjection& proj,
                                                             fv::PixelPoint p,
                                                             double tolerance_px) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return fv::AisOverlay::InfoAt(proj, p, tolerance_px);
}

void TrafficOverlay::Search(const fv::app::SearchQuery& q, const std::atomic<bool>& cancel,
                            std::vector<fv::app::SearchResult>& out) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  fv::AisOverlay::Search(q, cancel, out);
}

const std::vector<PropertySpec>& TrafficOverlay::Describe() const { return specs_; }

fv::Status TrafficOverlay::GetProperty(const std::string& key, PropertyValue* out) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const FeedSettings& s = settings_;
  if (key == "open_waters")
    *out = PropertyValue::Bool(s.open_waters);
  else if (key == "receiver")
    *out = PropertyValue::Bool(s.receiver);
  else if (key == "receiver_protocol")
    *out = PropertyValue::Choice(s.receiver_config.protocol == fv::ReceiverProtocol::kUdp ? 1 : 0);
  else if (key == "receiver_host")
    *out = PropertyValue::String(s.receiver_config.host);
  else if (key == "receiver_port")
    *out = PropertyValue::Int(s.receiver_config.port);
  else if (key == "cpa_alarm")
    *out = PropertyValue::Bool(s.cpa_alarm);
  else if (key == "cpa_nm")
    *out = PropertyValue::Double(s.cpa_m / kMetresPerNm);
  else if (key == "tcpa_minutes")
    *out = PropertyValue::Double(s.tcpa_s / 60.0);
  else
    return fv::AisOverlay::GetProperty(key, out);
  return fv::Status::Ok();
}

fv::Status TrafficOverlay::SetProperty(const std::string& key, const PropertyValue& v) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!IsFeedKey(key)) return fv::AisOverlay::SetProperty(key, v);
  FeedSettings& s = settings_;
  const auto bad = [&] { return fv::Status::Error(fv::kInvalidArg, "bad value for " + key); };
  if (key == "open_waters" || key == "receiver" || key == "cpa_alarm") {
    if (v.type != PropertyType::kBool) return bad();
    (key == "open_waters" ? s.open_waters : key == "receiver" ? s.receiver : s.cpa_alarm) = v.b;
  } else if (key == "receiver_protocol") {
    if (v.type != PropertyType::kChoice || v.i < 0 || v.i > 1) return bad();
    s.receiver_config.protocol = v.i == 1 ? fv::ReceiverProtocol::kUdp : fv::ReceiverProtocol::kTcp;
  } else if (key == "receiver_host") {
    if (v.type != PropertyType::kString) return bad();
    s.receiver_config.host = v.s;
  } else if (key == "receiver_port") {
    if (v.type != PropertyType::kInt || v.i < 1 || v.i > 65535) return bad();
    s.receiver_config.port = static_cast<uint16_t>(v.i);
  } else if (key == "cpa_nm") {
    if (v.type != PropertyType::kDouble || !(v.d > 0.0)) return bad();
    s.cpa_m = std::clamp(v.d, 0.05, 10.0) * kMetresPerNm;
  } else if (key == "tcpa_minutes") {
    if (v.type != PropertyType::kDouble || !(v.d > 0.0)) return bad();
    s.tcpa_s = std::clamp(v.d, 1.0, 60.0) * 60.0;
  }
  wake_.notify_all();
  return fv::Status::Ok();
}

}  // namespace peregrine_ais
