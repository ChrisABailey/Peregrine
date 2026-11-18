// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_moving_map.h"

#include "fv_desk_feeds.h"
#include "fv_desk_ownship.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <random>
#include <utility>

#include "fvkit/nav/gpx.h"
#include "fvkit/nav/heading.h"
#include "fvkit/nav/nmea_receiver.h"
#include "fvkit/nav/road_snap.h"
#include "fvkit/nav/scripted_source.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/proj.h"
#include "fvkit/settings.h"
#include "fvkit/symbol/builtin.h"

namespace fv {
namespace desk {
namespace {

constexpr double kMetresPerDegree = 6371008.8 * 3.14159265358979323846 / 180.0;
constexpr uint16_t kDefaultNmeaPort = 10110;
int g_new_count = 0;  ///< numbers "Moving Map N"

/// A square of side 0.04 degrees around `c`, closed, for the demo with no route.
std::vector<GeoPoint> CircleAround(const GeoPoint& c) {
  const double d = 0.02;
  return {GeoPoint{c.lat - d, c.lon - d}, GeoPoint{c.lat + d, c.lon - d},
          GeoPoint{c.lat + d, c.lon + d}, GeoPoint{c.lat - d, c.lon + d},
          GeoPoint{c.lat - d, c.lon - d}};
}

/// Moves each point by up to `metres` east and north, seeded so a demo repeats.
void Scatter(std::vector<GeoPoint>* path, double metres) {
  std::mt19937 rng(20260817u);
  std::uniform_real_distribution<double> u(-1.0, 1.0);
  for (GeoPoint& p : *path) {
    const double east = metres * u(rng);
    const double north = metres * u(rng);
    p.lat += north / kMetresPerDegree;
    p.lon += east / (kMetresPerDegree * std::cos(p.lat * 3.14159265358979323846 / 180.0));
  }
}

uint16_t NmeaPort(const Settings& s) {
  const int port = s.GetInt("movingmap.nmea_port", kDefaultNmeaPort);
  return port > 0 && port < 65536 ? static_cast<uint16_t>(port) : kDefaultNmeaPort;
}

ReceiverConfig ReceiverFor(const FeedConfig& c) {
  ReceiverConfig r;
  r.protocol = c.kind == MovingMapFeed::kNmeaUdp ? ReceiverProtocol::kUdp : ReceiverProtocol::kTcp;
  r.host = c.host;
  r.port = c.port;
  return r;
}

}  // namespace

const std::vector<std::string>& MovingMapFeedNames() {
  static const std::vector<std::string> names = {"demo", "nmea_tcp", "nmea_udp"};
  return names;
}

MovingMapFeed MovingMapFeedFromSettings(const Settings& settings) {
  const std::string name = settings.GetString("movingmap.feed", "");
  const std::vector<std::string>& names = MovingMapFeedNames();
  for (size_t i = 0; i < names.size(); ++i)
    if (name == names[i]) return static_cast<MovingMapFeed>(i);
  if (!settings.GetString("movingmap.nmea_host", "").empty()) return MovingMapFeed::kNmeaTcp;
  return MovingMapFeed::kDemo;
}

bool FeedConfig::operator==(const FeedConfig& o) const {
  return kind == o.kind && host == o.host && port == o.port &&
         demo_speed_mps == o.demo_speed_mps && demo_scatter_m == o.demo_scatter_m &&
         snap_to_road == o.snap_to_road && listening == o.listening;
}

FeedConfig DefaultFeedConfig(const Settings& settings) {
  FeedConfig c;
  c.kind = MovingMapFeedFromSettings(settings);
  c.host = settings.GetString("movingmap.nmea_host", "");
  c.port = NmeaPort(settings);
  c.demo_speed_mps = std::max(0.1, settings.GetDouble("movingmap.speed_mps", 15.0));
  c.demo_scatter_m = std::max(0.0, settings.GetDouble("movingmap.noise_m", 0.0));
  c.snap_to_road = settings.GetBool("movingmap.snap_to_road", true);
  return c;
}

// MARK: FeedConfigProperties

FeedConfigProperties::FeedConfigProperties(bool replay, bool for_dialog)
    : replay_(replay), for_dialog_(for_dialog) {}

const std::vector<app::PropertySpec>& FeedConfigProperties::Describe() const {
  auto spec = [](const char* key, const char* label, const char* group, app::PropertyType type,
                 app::PropertyValue def, const char* help) {
    app::PropertySpec s;
    s.key = key;
    s.label = label;
    s.group = group;
    s.type = type;
    s.default_value = std::move(def);
    s.help = help;
    return s;
  };
  using T = app::PropertyType;
  using V = app::PropertyValue;
  static const std::vector<app::PropertySpec> live = [&] {
    std::vector<app::PropertySpec> v;
    app::PropertySpec feed = spec("feed", "Feed", "Feed", T::kChoice, V::Choice(0),
                                  "demo replays the open route (or circles the map); nmea_tcp "
                                  "connects to a receiver serving NMEA 0183; nmea_udp listens "
                                  "for NMEA broadcasts.");
    feed.choices = MovingMapFeedNames();
    v.push_back(feed);
    v.push_back(spec("nmea_host", "NMEA host", "Feed", T::kString, V::String(std::string()),
                     "The receiver's address for nmea_tcp."));
    app::PropertySpec port = spec("nmea_port", "NMEA port", "Feed", T::kInt, V::Int(kDefaultNmeaPort),
                                  "The TCP port to connect to, or the UDP port to listen on.");
    port.min = 1;
    port.max = 65535;
    v.push_back(port);
    v.push_back(spec("listening", "Listen", "Feed", T::kBool, V::Bool(true),
                     "Off stops the feed; the ship and trail stay where they are."));
    v.push_back(spec("speed_mps", "Demo speed (m/s)", "Demo", T::kDouble, V::Double(15.0),
                     "Ground speed of the demo replay."));
    v.push_back(spec("noise_m", "Demo scatter (m)", "Demo", T::kDouble, V::Double(0.0),
                     "Scatters the demo fixes by up to this distance."));
    v.push_back(spec("snap_to_road", "Snap to road", "Map", T::kBool, V::Bool(true),
                     "Put the ship on the road it is most likely on."));
    return v;
  }();
  static const std::vector<app::PropertySpec> replay = {live.back()};
  static const std::vector<app::PropertySpec> dialog(live.begin(), live.end() - 1);
  if (for_dialog_) return dialog;
  return replay_ ? replay : live;
}

Status FeedConfigProperties::GetProperty(const std::string& key, app::PropertyValue* out) const {
  using V = app::PropertyValue;
  const FeedConfig& c = config_;
  if (key == "snap_to_road") *out = V::Bool(c.snap_to_road);
  else if (replay_) return Status::Error(kNotFound, "no feed property '" + key + "'");
  else if (key == "feed") *out = V::Choice(static_cast<int>(c.kind));
  else if (key == "nmea_host") *out = V::String(c.host);
  else if (key == "nmea_port") *out = V::Int(c.port);
  else if (key == "listening") *out = V::Bool(c.listening);
  else if (key == "speed_mps") *out = V::Double(c.demo_speed_mps);
  else if (key == "noise_m") *out = V::Double(c.demo_scatter_m);
  else return Status::Error(kNotFound, "no feed property '" + key + "'");
  return Status::Ok();
}

Status FeedConfigProperties::SetProperty(const std::string& key, const app::PropertyValue& v) {
  using T = app::PropertyType;
  FeedConfig c = config_;
  auto bad = [&key] { return Status::Error(kInvalidArg, "bad value for feed property '" + key + "'"); };
  if (key == "snap_to_road") {
    if (v.type != T::kBool) return bad();
    c.snap_to_road = v.b;
  } else if (replay_) {
    return Status::Error(kNotFound, "no feed property '" + key + "'");
  } else if (key == "feed") {
    if (v.type != T::kChoice || v.i < 0 || v.i >= static_cast<long long>(MovingMapFeedNames().size()))
      return bad();
    c.kind = static_cast<MovingMapFeed>(v.i);
  } else if (key == "nmea_host") {
    if (v.type != T::kString) return bad();
    c.host = v.s;
  } else if (key == "nmea_port") {
    if (v.type != T::kInt || v.i < 1 || v.i > 65535) return bad();
    c.port = static_cast<uint16_t>(v.i);
  } else if (key == "listening") {
    if (v.type != T::kBool) return bad();
    c.listening = v.b;
  } else if (key == "speed_mps") {
    if (v.type != T::kDouble || !(v.d > 0.0)) return bad();
    c.demo_speed_mps = v.d;
  } else if (key == "noise_m") {
    if (v.type != T::kDouble || !(v.d >= 0.0)) return bad();
    c.demo_scatter_m = v.d;
  } else {
    return Status::Error(kNotFound, "no feed property '" + key + "'");
  }
  if (c == config_) return Status::Ok();
  config_ = c;
  if (on_change_) on_change_();
  return Status::Ok();
}

// MARK: DeskMovingMap

DeskMovingMap::DeskMovingMap() : MovingMapOverlay("Moving Map") {
  feed_props_.set_on_change([this] {
    has_feed_config_ = true;
    ++feed_generation_;
  });
}

std::string DeskMovingMap::ConfigurationSpec() const {
  return replay() ? file_spec() : std::string();
}

Status DeskMovingMap::FileNew() {
  track_.clear();
  recording_.clear();
  recording_full_ = false;
  SetName("Moving Map " + std::to_string(++g_new_count));
  set_file_spec(Name() + ".gpx");
  set_dirty(false);
  return Status::Ok();
}

Status DeskMovingMap::FileOpen(const std::string& spec) {
  std::vector<PositionFix> fixes;
  const Status s = ReadTrackFixes(spec, &fixes);
  if (!s.ok()) return s;
  fixes = ReplayableFixes(std::move(fixes));
  if (fixes.size() < 2)
    return Status::Error(kInvalidArg, "No track to replay in " + spec + " (" +
                                          std::to_string(fixes.size()) + " positions).");
  track_ = std::move(fixes);
  recording_.clear();
  recording_full_ = false;
  replay_time_s_ = track_.back().time_s;
  // A replay's only feed setting is snapping.
  const FeedConfig config = feed_config();
  feed_props_ = FeedConfigProperties(true);
  feed_props_.set_config(config);
  feed_props_.set_on_change([this] {
    has_feed_config_ = true;
    ++feed_generation_;
  });
  SetName(std::filesystem::path(spec).stem().string());
  set_file_spec(spec);
  set_has_been_saved(true);
  set_dirty(false);
  return Status::Ok();
}

Status DeskMovingMap::FileSaveAs(const std::string& spec, int /*format_index*/) {
  const std::vector<PositionFix>& fixes = replay() ? track_ : recording_;
  const Status s = WriteGpxFile(spec, BuildGpxTrack(fixes, Name(), std::string()));
  if (!s.ok()) return s;
  set_file_spec(spec);
  set_has_been_saved(true);
  set_dirty(false);
  return Status::Ok();
}

void DeskMovingMap::SetPhase(ReplayPhase phase) {
  if (phase == phase_) return;
  phase_ = phase;
  SetDrawOpacity(phase == ReplayPhase::kBefore ? 96 : 255);
}

void DeskMovingMap::AddTrailFix(const PositionFix& fix) {
  if (!fix.has_position) return;
  if (recording_.size() >= trail_style_.record_max) {
    recording_full_ = true;
    return;
  }
  recording_.push_back(fix);
  // A demo ride is made up; closing it loses nothing.
  if (feed_config().kind != MovingMapFeed::kDemo) set_dirty(true);
}

FixSpan DeskMovingMap::trail() const {
  if (replay()) return FixSpan{track_.data(), track_.size()};
  const size_t n = std::min(recording_.size(), trail_style_.trail_max);
  return FixSpan{recording_.data() + (recording_.size() - n), n};
}

Status DeskMovingMap::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (proj.Ready() && (trail_style_.dots || trail_style_.line)) DrawTrail(proj, canvas);
  return MovingMapOverlay::OnDraw(proj, canvas);
}

void DeskMovingMap::DrawTrail(const MapProjection& proj, ICanvas& canvas) const {
  const FixSpan fixes = trail();
  if (fixes.size == 0) return;
  const double dpi = symbol_dpi_scale();
  const uint8_t opacity = draw_opacity();
  auto scaled = [opacity](int a) { return static_cast<unsigned char>(a * opacity / 255); };
  FvColor past = color();
  past.a = scaled(past.a);
  FvColor future = color();
  future.a = scaled(64);
  // On a live map every fix is past; on a replay the clock splits the track.
  const bool split = replay();

  struct Projected {
    bool ok = false;
    bool past = true;
    double x = 0.0, y = 0.0;
  };
  std::vector<Projected> pts(fixes.size);
  for (size_t i = 0; i < fixes.size; ++i) {
    Projected& p = pts[i];
    p.ok = proj.GeoToSurface(fixes.data[i].position(), &p.x, &p.y).ok();
    p.past = !split || fixes.data[i].time_s <= replay_time_s_;
  }

  if (trail_style_.line) {
    Pen pen;
    pen.width = std::max(1, static_cast<int>(std::lround(2.0 * dpi)));
    std::vector<PixelPoint> run;
    bool run_past = true;
    auto flush = [&] {
      pen.color = run_past ? past : future;
      if (run.size() >= 2) (void)canvas.DrawLines(run, pen);
      run.clear();
    };
    for (const Projected& p : pts) {
      if (!p.ok) {
        flush();
        continue;
      }
      const PixelPoint px{static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
      if (!run.empty() && p.past != run_past) {
        // The future run starts where the past one ended, so the line is unbroken.
        const PixelPoint joint = run.back();
        flush();
        run.push_back(joint);
      }
      run_past = p.past;
      run.push_back(px);
    }
    flush();
  }

  if (trail_style_.dots) {
    const PixelSize surf = proj.SurfaceSize();
    // The tolerance keeps fixes exactly 3 px apart, give or take rounding, as separate dots.
    const double min_gap = 3.0 * dpi - 1e-6;
    const int d = std::max(2, static_cast<int>(std::lround(6.0 * dpi)));
    const Brush past_fill{past};
    const Brush future_fill{future};
    Pen edge;
    edge.color = FvColor{0, 0, 0, scaled(160)};
    bool have_last = false;
    double last_x = 0.0, last_y = 0.0;
    for (const Projected& p : pts) {
      if (!p.ok) continue;
      // Dots closer than 3 px to the last one drawn would overlap it.
      if (have_last && std::hypot(p.x - last_x, p.y - last_y) < min_gap) continue;
      have_last = true;
      last_x = p.x;
      last_y = p.y;
      if (p.x < -d || p.y < -d || p.x > surf.width + d || p.y > surf.height + d) continue;
      const PixelRect box{static_cast<int>(std::lround(p.x - d / 2.0)),
                          static_cast<int>(std::lround(p.y - d / 2.0)), d, d};
      (void)canvas.DrawEllipse(box, p.past ? &past_fill : &future_fill,
                               p.past && show_edge() ? &edge : nullptr);
    }
  }
}

void DeskMovingMap::SetFeedConfig(const FeedConfig& config) {
  has_feed_config_ = true;
  if (config == feed_config()) return;
  feed_props_.set_config(config);
  ++feed_generation_;
}

app::OverlayTypeDesc MovingMapFileType() {
  app::OverlayTypeDesc desc;
  desc.id = MovingMapOverlay::kTypeId;
  desc.display_name = "Moving Map";
  desc.icon = "movingmap";
  desc.default_display_order = 980;
  app::FileTypeDesc file;
  file.default_extension = "gpx";
  file.extra_extensions = {"nmea"};
  file.open_filters = {{"Recorded Tracks (*.gpx, *.nmea)", "*.gpx;*.nmea"},
                       {"GPX Tracks (*.gpx)", "*.gpx"},
                       {"NMEA Logs (*.nmea)", "*.nmea"}};
  file.save_filters = {{"GPX Tracks (*.gpx)", "*.gpx"}};
  desc.file = std::move(file);
  desc.factory = [] { return std::make_shared<DeskMovingMap>(); };
  return desc;
}

// MARK: MovingMapDriver

MovingMapDriver::MovingMapDriver(Settings& settings, FeedReceivers& receivers,
                                 std::shared_ptr<DeskMovingMap> overlay)
    : settings_(settings), receivers_(receivers), overlay_(std::move(overlay)) {
  overlay_->SetRotationSupported(true);
  if (!overlay_->has_feed_config()) overlay_->SetFeedConfig(DefaultFeedConfig(settings_));
}

MovingMapDriver::~MovingMapDriver() { Stop(); }

void MovingMapDriver::Stop() {
  overlay_->Stop();
  overlay_->SetSource(nullptr);
  demo_.reset();
  subscription_.reset();
}

void MovingMapDriver::SetRoadNetwork(std::shared_ptr<const IRoadNetwork> network) {
  network_ = std::move(network);
  overlay_->SetRoadNetwork(overlay_->feed_config().snap_to_road ? network_ : nullptr);
}

void MovingMapDriver::ApplySettings() {
  // Peregrine follows the ship only while centring on every fix: off, the map
  // stays where the user put it, and course-up has nothing to turn.
  CameraModes modes;
  modes.continuous = settings_.GetBool("movingmap.continuous", false);
  modes.auto_center = modes.continuous;
  modes.auto_rotate = modes.continuous && settings_.GetBool("movingmap.auto_rotate", false);
  SlewSettings slew;
  slew.duration_s = std::max(0.0, settings_.GetDouble("movingmap.slew_seconds", 0.35));
  // Continuous recentring moves every fix; easing in and out of each would pulse.
  slew.easing = modes.continuous ? SlewEasing::kLinear : SlewEasing::kEaseInOut;
  overlay_->SetSlewSettings(slew);
  overlay_->SetModes(modes);
  // Pippin's chevron, which reaches 1.3 of the size box.
  overlay_->SetSymbolId(settings_.GetString("movingmap.symbol", builtin_symbol::kNorthArrow));
  overlay_->SetSizePx(settings_.GetDouble("movingmap.symbol_px", 9.0));
  overlay_->SetShowApron(settings_.GetBool("movingmap.show_apron", false));
  TrailStyle trail;
  trail.dots = settings_.GetBool("movingmap.show_trail_dots", true);
  trail.line = settings_.GetBool("movingmap.show_trail_line", false);
  trail.trail_max = static_cast<size_t>(std::max(0, settings_.GetInt("movingmap.trail_max", 5000)));
  trail.record_max =
      static_cast<size_t>(std::max(1, settings_.GetInt("movingmap.record_max", 500000)));
  overlay_->SetTrailStyle(trail);
  overlay_->SetRoadNetwork(overlay_->feed_config().snap_to_road ? network_ : nullptr);
}

Status MovingMapDriver::Start(const GeoPoint& center, double rotation_deg,
                              std::vector<GeoPoint> demo_path) {
  // Stopping drops the overlay's fix; a live map keeps its ship where it was (OS-D8).
  const bool hold = !overlay_->replay() && overlay_->has_fix();
  const PositionFix last = overlay_->last_fix();
  const Status s = StartFeed(center, rotation_deg, std::move(demo_path));
  if (hold) {
    overlay_->PushFix(last);
    held_ = last;
    held_pending_ = true;
  }
  return s;
}

Status MovingMapDriver::StartFeed(const GeoPoint& center, double rotation_deg,
                                  std::vector<GeoPoint> demo_path) {
  Stop();
  started_generation_ = overlay_->feed_generation();
  ApplySettings();
  overlay_->ResetMap(center, rotation_deg);
  true_heading_.Reset();
  heading_ = ResolvedHeading();
  has_applied_ = false;
  feed_error_.clear();
  if (overlay_->replay()) {
    feed_ = MovingMapFeed::kReplay;
    return Status::Ok();
  }
  const FeedConfig& config = overlay_->feed_config();
  feed_ = config.kind;
  if (!config.listening) return Status::Ok();
  if (feed_ == MovingMapFeed::kNmeaTcp || feed_ == MovingMapFeed::kNmeaUdp) {
    subscription_ = receivers_.Subscribe(ReceiverFor(config), UtcNowSeconds());
    return Status::Ok();
  }
  if (demo_path.size() < 2) demo_path = CircleAround(center);
  if (config.demo_scatter_m > 0.0) Scatter(&demo_path, config.demo_scatter_m);
  demo_ = std::make_shared<ScriptedSource>(
      BuildScriptedTrack(demo_path, std::max(0.1, config.demo_speed_mps)));
  demo_->SetLooping(true);
  overlay_->SetSource(demo_);
  const Status started = overlay_->Start();
  if (!started.ok()) feed_error_ = started.message;
  return started;
}

void MovingMapDriver::SetReplayTime(double t_s, bool seek) {
  if (!overlay_->replay()) return;
  if (!seek && overlay_->has_fix() && !replay_pending_ && t_s == replay_time_) return;
  replay_time_ = t_s;
  replay_pending_ = true;
  if (seek) {
    replay_seek_ = true;
    overlay_->heading_resolver().Reset();
    overlay_->snapper().Reset();
    overlay_->queue().Clear();
    // Tick re-bases the slew on the view before it places the ship.
    has_applied_ = false;
  }
}

void MovingMapDriver::MapMoved(const GeoPoint& center, double rotation_deg) {
  overlay_->ResetMap(center, rotation_deg);
  applied_center_ = center;
  applied_rotation_ = rotation_deg;
}

void MovingMapDriver::NoteApplied(const GeoPoint& center, double rotation_deg) {
  applied_center_ = center;
  applied_rotation_ = rotation_deg;
  has_applied_ = true;
}

MovingMapStep MovingMapDriver::Tick(const MapProjection& proj, double dt_s, bool follow) {
  MovingMapStep step;
  if (demo_) demo_->Poll();
  if (subscription_)
    for (const PositionFix& fix : subscription_->TakeFixes()) overlay_->PushFix(fix);
  if (replay_pending_) {
    const TrackState state = TrackStateAt(overlay_->track(), replay_time_);
    overlay_->SetPhase(state.phase);
    overlay_->SetReplayTime(replay_time_);
    // After a jump the drawn heading has no history; one segment back along
    // the track gives it the segment's screen bearing at once.
    if (replay_seek_ && state.heading.known && !state.heading.reported)
      overlay_->heading_resolver().Prime(
          GeoPoint{state.fix.lat - (state.segment_to.lat - state.segment_from.lat),
                   state.fix.lon - (state.segment_to.lon - state.segment_from.lon)});
    overlay_->PushFix(state.fix);
    replay_heading_ = state.heading;
    replay_pending_ = false;
    replay_seek_ = false;
  }

  const GeoPoint center = proj.Center();
  const double rotation = proj.Rotation();
  // The view no longer shows what the last tick put there: someone else moved it.
  if (!follow || !has_applied_ || center.lat != applied_center_.lat ||
      center.lon != applied_center_.lon || rotation != applied_rotation_)
    MapMoved(center, rotation);
  has_applied_ = true;

  const MovingMapTick tick = overlay_->Tick(proj, dt_s);
  step.redraw = tick.new_fix;
  step.new_fix = tick.new_fix;
  if (tick.new_fix) {
    step.fix = tick.fix;
    // A snapped fix carries the road's bearing; otherwise a replay takes the
    // track's heading at the clock time and a live map the bearing of movement.
    if (overlay_->replay() && !tick.fix.has_true_heading)
      heading_ = replay_heading_;
    else if (!tick.stopped || !heading_.known)
      heading_ = true_heading_.Update(tick.fix);
    step.heading = heading_;
    // A live map records what the receiver said, not where the snapper put it.
    const PositionFix& raw = tick.snap_applied ? tick.snap.raw : tick.fix;
    // The fix handed back after a restart is already in the trail.
    const bool held = held_pending_ && raw.lat == held_.lat && raw.lon == held_.lon &&
                      raw.time_s == held_.time_s;
    held_pending_ = false;
    if (!overlay_->replay() && !held) overlay_->AddTrailFix(raw);
  }
  if (tick.slew.changed && follow) {
    step.move = true;
    step.center = tick.slew.center;
    step.rotation_deg = tick.slew.rotation_deg;
    step.redraw = true;
  }
  return step;
}

std::string MovingMapDriver::FeedDescription() const {
  const FeedConfig& config = overlay_->feed_config();
  switch (feed_) {
    case MovingMapFeed::kReplay:
      return std::filesystem::path(overlay_->file_spec()).filename().string();
    case MovingMapFeed::kNmeaTcp:
      return config.host + ":" + std::to_string(config.port);
    case MovingMapFeed::kNmeaUdp:
      return "udp:" + std::to_string(config.port);
    case MovingMapFeed::kDemo:
      break;
  }
  return "demo";
}

std::string MovingMapDriver::StatusText() const {
  std::string text;
  if (overlay_->snapping()) {
    const SnappedFix& snap = overlay_->last_snap();
    if (snap.snapped)
      text = "On " + (snap.road_name.empty() ? std::string("an unnamed road") : snap.road_name) +
             (snap.held ? " (held)" : "") + "  ";
    else if (overlay_->has_fix())
      text = "Off road  ";
  }
  text += "Feed " + FeedDescription();
  if (!overlay_->replay() && !overlay_->feed_config().listening) {
    text += ": paused";
  } else if (subscription_) {
    const NmeaReceiver& r = subscription_->receiver();
    text += std::string(": ") + ReceiverStateName(r.state()) + ", " +
            std::to_string(r.fixes_emitted()) + " fixes";
    // Lines arriving with nothing parsed would otherwise look like silence.
    if (r.lines_received() != 0 && r.fixes_emitted() == 0 && r.ais_lines() == 0 &&
        r.lines_ignored() == r.lines_received())
      text += ", " + std::to_string(r.lines_received()) + " lines and no NMEA parsed";
    if (!r.last_error().empty() && r.state() == ReceiverState::kRetrying)
      text += ", " + r.last_error();
  }
  if (overlay_->recording_full())
    text += ", recording full at " + std::to_string(overlay_->recording().size()) + " fixes";
  if (!feed_error_.empty()) text += " (" + feed_error_ + ")";
  return text;
}

}  // namespace desk
}  // namespace fv
