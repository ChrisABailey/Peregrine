// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// `fv.ais` overlay. See fvkit/overlay/ais_overlay.h.

#include "fvkit/overlay/ais_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "fvkit/ais/presentation.h"
#include "fvkit/canvas/geo_draw.h"

namespace fv {
namespace {

using app::PropertySpec;
using app::PropertyType;
using app::PropertyValue;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kEarthMeanRadiusM = 6371008.8;
constexpr double kKnot = 1852.0 / 3600.0;
constexpr double kClassBScale = 0.75;
constexpr std::size_t kTransportLinesPerTick = 5000;

// Property indices, in Specs() order.
enum Prop {
  kShowTrails,
  kTrailMinutes,
  kVectorMinutes,
  kShowLabels,
  kLabelMaxScale,
  kSymbolSizePt,
  kShowAtoN,
  kShowBaseStations,
  kStaleMinutes,
  kDropMinutes,
  kSpeedUnit,
  kPropCount
};

enum SpeedUnit { kKnots = 0, kKmh = 1, kMps = 2 };

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

const std::vector<PropertySpec>& Specs() {
  static const std::vector<PropertySpec> specs = [] {
    std::vector<PropertySpec> v;
    v.push_back(Spec("show_trails", "Show trails", "Display", PropertyValue::Bool(true)));
    v.push_back(Spec("trail_minutes", "Trail length (minutes)", "Display",
                     PropertyValue::Double(30.0), 1.0, 240.0));
    v.push_back(Spec("vector_minutes", "Course vector (minutes)", "Display",
                     PropertyValue::Double(6.0), 0.0, 60.0,
                     "Length of the course-over-ground line as distance run in this time; 0 hides it."));
    v.push_back(Spec("show_labels", "Show names", "Labels", PropertyValue::Bool(true)));
    v.push_back(Spec("label_max_scale", "Names at 1:N or larger", "Labels",
                     PropertyValue::Double(100000.0), 0.0, 1e8, "0 shows names at every scale."));
    v.push_back(Spec("symbol_size_pt", "Ship size (points)", "Display",
                     PropertyValue::Double(24.0), 8.0, 64.0,
                     "Drawn hull length of a class A ship; class B is three quarters of it."));
    v.push_back(Spec("show_aton", "Show aids to navigation", "Targets", PropertyValue::Bool(true)));
    v.push_back(Spec("show_base_stations", "Show base stations", "Targets",
                     PropertyValue::Bool(false)));
    v.push_back(Spec("stale_minutes", "Fade after (minutes)", "Ageing",
                     PropertyValue::Double(6.0), 1.0, 60.0));
    v.push_back(Spec("drop_minutes", "Remove after (minutes)", "Ageing",
                     PropertyValue::Double(30.0), 1.0, 240.0));
    PropertySpec unit = Spec("speed_unit", "Speed unit", "Units", PropertyValue::Choice(kKnots));
    unit.choices = {"knots", "km/h", "m/s"};
    v.push_back(unit);
    return v;
  }();
  return specs;
}

int IndexOf(const std::string& key) {
  const std::vector<PropertySpec>& specs = Specs();
  for (std::size_t i = 0; i < specs.size(); ++i)
    if (specs[i].key == key) return static_cast<int>(i);
  return -1;
}

/// Position reached after `distance_m` along `bearing_deg`, flat earth.
GeoPoint Advance(double lat, double lon, double bearing_deg, double distance_m) {
  const double b = bearing_deg * kDegToRad;
  GeoPoint p;
  p.lat = lat + distance_m * std::cos(b) / kEarthMeanRadiusM / kDegToRad;
  p.lon = lon + distance_m * std::sin(b) / (kEarthMeanRadiusM * std::cos(lat * kDegToRad)) / kDegToRad;
  return p;
}

/// Bearing the hull points along: heading, else COG, else north.
double HullBearing(const ais::Vessel& v) {
  if (v.has_heading) return v.heading_deg;
  if (v.has_cog) return v.cog_deg;
  return 0.0;
}

std::string Format(const char* fmt, double value) {
  char buf[64];
  std::snprintf(buf, sizeof buf, fmt, value);
  return buf;
}

std::string FormatAge(double s) {
  if (s < 0.0) s = 0.0;
  if (s < 90.0) return Format("%.0f s ago", s);
  if (s < 5400.0) return Format("%.0f min ago", s / 60.0);
  return Format("%.1f h ago", s / 3600.0);
}

}  // namespace

const char AisOverlay::kTypeId[] = "fv.ais";

AisOverlay::AisOverlay(std::string name) : Overlay(std::move(name)) {
  set_type_id(kTypeId);
  values_.reserve(Specs().size());
  for (const PropertySpec& s : Specs()) values_.push_back(s.default_value);
  ApplyTableOptions();
}

void AisOverlay::PushLine(std::string line) {
  Input input;
  input.line = std::move(line);
  Push(std::move(input));
}

void AisOverlay::Push(Input input) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  queue_.push_back(std::move(input));
}

void AisOverlay::PushFrame(const ais::OpenWatersFrame& frame) {
  if (frame.kind != ais::OpenWatersFrameKind::kEvent) return;
  Input input;
  input.heard_s = frame.time_s;
  input.station = frame.station;
  input.attribution = frame.attribution;
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (frame.nmea.empty()) {
    if (!frame.has_fallback) return;
    input.has_message = true;
    input.message = frame.fallback;
    queue_.push_back(std::move(input));
    return;
  }
  for (const std::string& sentence : frame.nmea) {
    input.line = sentence;
    queue_.push_back(input);
  }
}

void AisOverlay::SetTransport(std::shared_ptr<ILineTransport> transport) {
  transport_ = std::move(transport);
}

bool AisOverlay::Tick(double now_s) {
  const std::uint64_t revision_before = table_.revision();
  std::size_t stale_before = 0;
  table_.ForEach([&](const ais::Vessel& v) { stale_before += table_.IsStale(v, now_s_); });

  std::deque<Input> inputs;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    inputs.swap(queue_);
  }
  for (const Input& input : inputs) Decode(input, now_s);
  if (transport_ && transport_->is_open()) {
    std::string line;
    for (std::size_t n = 0; n < kTransportLinesPerTick; ++n) {
      if (transport_->ReadLine(&line) != LineResult::kLine) break;
      Decode(line, now_s);
    }
  }
  now_s_ = now_s;
  table_.Expire(now_s);

  std::size_t stale_after = 0;
  table_.ForEach([&](const ais::Vessel& v) { stale_after += table_.IsStale(v, now_s); });
  return table_.revision() != revision_before || stale_after != stale_before;
}

void AisOverlay::FeedLine(std::string_view line, double now_s) {
  Decode(line, now_s);
  now_s_ = now_s;
}

void AisOverlay::Decode(std::string_view line, double now_s) {
  ais::AisMessage m;
  if (decoder_.Feed(line, now_s, &m) == ais::FeedResult::kMessage) table_.Update(m, now_s);
}

void AisOverlay::Decode(const Input& input, double now_s) {
  ais::AisMessage m;
  if (input.has_message) {
    m = input.message;
  } else if (decoder_.Feed(input.line, now_s, &m) != ais::FeedResult::kMessage) {
    return;
  }
  if (!m.tag.has_source && !input.station.empty()) {
    m.tag.has_source = true;
    m.tag.source = input.station;
  }
  if (!m.tag.has_text && !input.attribution.empty()) {
    m.tag.has_text = true;
    m.tag.text = input.attribution;
  }
  // A snapshot replays reports hours old; they age from when they were heard.
  // A reception time ahead of the local clock is taken as now.
  const double heard = input.heard_s > 0.0 && input.heard_s < now_s ? input.heard_s : now_s;
  table_.Update(m, heard);
}

void AisOverlay::ApplyTableOptions() {
  ais::VesselTableOptions o = table_.options();
  o.stale_after_s = values_[kStaleMinutes].d * 60.0;
  o.drop_after_s = values_[kDropMinutes].d * 60.0;
  o.trail_max_age_s = values_[kTrailMinutes].d * 60.0;
  o.trail_max_points = 1000;
  table_.SetOptions(o);
}

bool AisOverlay::Shows(const ais::Vessel& v) const {
  if (v.own_ship || !v.has_position) return false;
  if (v.kind == ais::TargetKind::kAidToNavigation) return values_[kShowAtoN].b;
  if (v.kind == ais::TargetKind::kBaseStation) return values_[kShowBaseStations].b;
  return true;
}

double AisOverlay::HullLengthPx(const ais::Vessel& v) const {
  double len = values_[kSymbolSizePt].d * dpi_scale_;
  if (v.vessel_class == ais::VesselClass::kB) len *= kClassBScale;
  return len;
}

std::string AisOverlay::FormatSpeed(double mps) const {
  switch (values_[kSpeedUnit].i) {
    case kKmh: return Format("%.1f km/h", mps * 3.6);
    case kMps: return Format("%.1f m/s", mps);
    default: return Format("%.1f kn", mps / kKnot);
  }
}

Status AisOverlay::OnDraw(const MapProjection& proj, ICanvas& canvas) {
  if (!proj.Ready()) return Status::Error(kInvalidArg, "projection not ready");

  std::vector<const ais::Vessel*> shown;
  table_.ForEach([&](const ais::Vessel& v) {
    if (Shows(v)) shown.push_back(&v);
  });
  // Stable draw order, so overlapping symbols stack the same way every frame.
  std::sort(shown.begin(), shown.end(),
            [](const ais::Vessel* a, const ais::Vessel* b) { return a->mmsi < b->mmsi; });

  GeoDraw draw(proj, &canvas);
  draw.SetSymbolDpiScale(dpi_scale_);
  std::shared_ptr<PngSymbolLibrary> sheet = ais::AisSymbolSheet();
  draw.SetSymbols(sheet.get());
  const int line_px = std::max(1, static_cast<int>(std::lround(dpi_scale_)));

  // Trails are dots in the hull's colour. Newest first, and a dot closer than
  // `spacing` to the last one drawn (or to the hull) is skipped, so a slow or
  // zoomed-out trail does not merge back into a line.
  if (values_[kShowTrails].b) {
    const int r = std::max(3, static_cast<int>(std::lround(2.0 * dpi_scale_)));
    const double spacing = 3.0 * r;
    Pen rim;
    rim.color = FvColor{40, 40, 40, 150};
    rim.width = std::max(1, line_px / 2);
    for (const ais::Vessel* v : shown) {
      if (v->trail.empty()) continue;
      double lx = 0.0, ly = 0.0;
      if (!proj.GeoToSurface(GeoPoint{v->lat_deg, v->lon_deg}, &lx, &ly).ok()) continue;
      // Only hulls and the SART family move; the latter is drawn as a red shape.
      const bool stale = table_.IsStale(*v, now_s_);
      FvColor color = ais::HullColor(*v, stale);
      if (ais::HullSymbolId(*v, stale).empty())
        color = stale ? FvColor{128, 128, 128, 255} : FvColor{230, 0, 0, 255};
      const Brush fill{color};
      for (auto t = v->trail.rbegin(); t != v->trail.rend(); ++t) {
        double sx = 0.0, sy = 0.0;
        if (!proj.GeoToSurface(GeoPoint{t->lat_deg, t->lon_deg}, &sx, &sy).ok()) continue;
        if (std::hypot(sx - lx, sy - ly) < spacing) continue;
        lx = sx;
        ly = sy;
        const int x = static_cast<int>(std::lround(sx));
        const int y = static_cast<int>(std::lround(sy));
        canvas.DrawEllipse(PixelRect{x - r, y - r, 2 * r + 1, 2 * r + 1}, &fill, &rim);
      }
    }
  }

  const double vector_s = values_[kVectorMinutes].d * 60.0;
  if (vector_s > 0.0) {
    const GeoLineStyle style = SolidGeoLine(FvColor{0, 0, 0, 255}, line_px);
    for (const ais::Vessel* v : shown) {
      if (!v->has_sog || !v->has_cog || v->sog_mps < 0.1 * kKnot) continue;
      const GeoPoint from{v->lat_deg, v->lon_deg};
      draw.DrawGeoLine(from, Advance(v->lat_deg, v->lon_deg, v->cog_deg, v->sog_mps * vector_s),
                       LineKind::kSimple, style);
    }
  }

  for (const ais::Vessel* v : shown) {
    const bool stale = table_.IsStale(*v, now_s_);
    const GeoPoint at{v->lat_deg, v->lon_deg};
    const std::string id = ais::HullSymbolId(*v, stale);
    if (!id.empty()) {
      // A bearing is clockwise; PointSymbolStyle turns counter-clockwise.
      const double scale = HullLengthPx(*v) / dpi_scale_ / ais::HullNominalLengthPx();
      draw.DrawSymbol(at, id, PointSymbolStyle{true, id, -HullBearing(*v), scale});
      continue;
    }
    double sx = 0.0, sy = 0.0;
    if (!proj.GeoToSurface(at, &sx, &sy).ok()) continue;
    const int x = static_cast<int>(std::lround(sx));
    const int y = static_cast<int>(std::lround(sy));
    const int r = std::max(3, static_cast<int>(std::lround(values_[kSymbolSizePt].d * dpi_scale_ * 0.3)));
    Pen pen;
    pen.width = std::max(2, line_px * 2);
    const FvColor grey{128, 128, 128, 160};
    if (v->kind == ais::TargetKind::kAidToNavigation) {
      pen.color = stale ? grey : FvColor{200, 0, 200, 255};
      if (v->virtual_aid) pen.dash = {3.0 * line_px, 2.0 * line_px};
      const std::vector<std::vector<PixelPoint>> ring = {
          {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}, {x, y - r}}};
      canvas.DrawPolyPolygon(ring, nullptr, &pen);
      const Brush dot{pen.color};
      canvas.DrawEllipse(PixelRect{x - line_px, y - line_px, 2 * line_px + 1, 2 * line_px + 1},
                         &dot, nullptr);
    } else if (v->kind == ais::TargetKind::kBaseStation) {
      pen.color = stale ? grey : FvColor{40, 70, 160, 255};
      const int h = r * 3 / 4;
      canvas.DrawRectangle(PixelRect{x - h, y - h, 2 * h + 1, 2 * h + 1}, nullptr, &pen);
    } else {
      // SART, MOB and EPIRB: a red circle crossed, the ECDIS AIS-SART sign.
      pen.color = stale ? grey : FvColor{230, 0, 0, 255};
      canvas.DrawEllipse(PixelRect{x - r, y - r, 2 * r + 1, 2 * r + 1}, nullptr, &pen);
      const int d = static_cast<int>(std::lround(r * 0.7));
      canvas.DrawLines({{x - d, y - d}, {x + d, y + d}}, pen);
      canvas.DrawLines({{x - d, y + d}, {x + d, y - d}}, pen);
    }
  }

  const double label_max = values_[kLabelMaxScale].d;
  if (values_[kShowLabels].b && (label_max <= 0.0 || proj.Scale() <= label_max)) {
    for (const ais::Vessel* v : shown) {
      LabelStyle style;
      style.valid = true;
      style.style.size = 12.0 * dpi_scale_;
      style.style.color = table_.IsStale(*v, now_s_) ? FvColor{110, 110, 110, 255}
                                                     : FvColor{0, 0, 0, 255};
      style.halo_width = 2.0 * dpi_scale_;
      style.valign = LabelVAlign::kCenter;
      style.dx = static_cast<int>(std::lround(HullLengthPx(*v) * 0.5 + 4.0 * dpi_scale_));
      const std::string text = v->name.empty() ? std::to_string(v->mmsi) : v->name;
      draw.DrawLabel(GeoPoint{v->lat_deg, v->lon_deg}, text, style);
    }
  }
  return Status::Ok();
}

void AisOverlay::HitTestPoint(const MapProjection& proj, PixelPoint p, double tolerance_px,
                              std::vector<app::HitItem>& out) {
  if (!proj.Ready()) return;
  table_.ForEach([&](const ais::Vessel& v) {
    if (!Shows(v)) return;
    double sx = 0.0, sy = 0.0;
    if (!proj.GeoToSurface(GeoPoint{v.lat_deg, v.lon_deg}, &sx, &sy).ok()) return;
    const double d = std::hypot(sx - p.x, sy - p.y);
    if (d > tolerance_px + HullLengthPx(v) / 2.0) return;
    app::HitItem item;
    item.overlay = this;
    item.feature = v.mmsi;
    item.distance_px = d;
    item.hint.tool_tip = v.name.empty() ? std::to_string(v.mmsi) : v.name;
    std::string status = item.hint.tool_tip;
    if (v.kind != ais::TargetKind::kVessel) {
      status += std::string(" - ") + ais::KindText(v.kind);
    } else if (v.has_ship_type) {
      status += " - " + ais::ShipTypeText(v.ship_type);
    }
    if (v.has_sog) status += " - " + FormatSpeed(v.sog_mps);
    item.hint.status = std::move(status);
    item.cursor = app::CursorId::kHand;
    out.push_back(std::move(item));
  });
}

std::optional<app::ItemInfoSheet> AisOverlay::InfoAt(const MapProjection& proj, PixelPoint p,
                                                     double tolerance_px) {
  std::vector<app::HitItem> hits;
  HitTestPoint(proj, p, tolerance_px, hits);
  if (hits.empty()) return std::nullopt;
  const auto nearest = std::min_element(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
    return a.distance_px < b.distance_px;
  });
  return Details(static_cast<std::uint32_t>(nearest->feature));
}

std::optional<app::ItemInfoSheet> AisOverlay::Details(std::uint32_t mmsi) const {
  const ais::Vessel* found = table_.Find(mmsi);
  if (found == nullptr) return std::nullopt;
  const ais::Vessel& v = *found;

  app::ItemInfoSheet sheet;
  sheet.title = v.name.empty() ? "MMSI " + std::to_string(v.mmsi) : v.name;
  auto row = [&sheet](const char* section, const char* key, const std::string& value) {
    if (!value.empty()) sheet.rows.push_back({section, key, value});
  };
  const bool vessel = v.kind == ais::TargetKind::kVessel;

  row("Identity", "Name", v.name);
  row("Identity", "MMSI", std::to_string(v.mmsi));
  if (!vessel) row("Identity", "Kind", ais::KindText(v.kind));
  row("Identity", "Call sign", v.call_sign);
  if (v.has_imo) row("Identity", "IMO", std::to_string(v.imo));
  if (v.has_ship_type) {
    row("Identity", "Type",
        v.kind == ais::TargetKind::kAidToNavigation ? ais::AidTypeText(v.ship_type)
                                                    : ais::ShipTypeText(v.ship_type));
  }
  if (v.kind == ais::TargetKind::kAidToNavigation && v.virtual_aid) row("Identity", "Virtual", "Yes");
  if (v.kind == ais::TargetKind::kAidToNavigation && v.off_position) row("Identity", "Off position", "Yes");
  if (vessel && v.vessel_class != ais::VesselClass::kUnknown)
    row("Identity", "Class", v.vessel_class == ais::VesselClass::kA ? "A" : "B");
  if (v.has_dimensions && v.length_m() > 0)
    row("Identity", "Size", std::to_string(v.length_m()) + " m x " + std::to_string(v.beam_m()) + " m");
  if (v.has_mothership) row("Identity", "Mothership", std::to_string(v.mothership_mmsi));

  row("Voyage", "Destination", v.destination);
  if (v.has_eta && v.eta_month > 0 && v.eta_day > 0) {
    char eta[48];
    // Hour 24 and minute 60 are AIS "not available".
    if (v.eta_hour < 24 && v.eta_minute < 60) {
      std::snprintf(eta, sizeof eta, "%02d-%02d %02d:%02d UTC", v.eta_month, v.eta_day,
                    v.eta_hour, v.eta_minute);
    } else {
      std::snprintf(eta, sizeof eta, "%02d-%02d", v.eta_month, v.eta_day);
    }
    row("Voyage", "ETA", eta);
  }
  if (v.has_draught) row("Voyage", "Draught", Format("%.1f m", v.draught_m));
  if (v.has_nav_status) row("Voyage", "Status", ais::NavStatusText(v.nav_status));

  if (v.has_position) row("Motion", "Position", app::FormatInfoPosition({v.lat_deg, v.lon_deg}));
  if (v.has_sog) row("Motion", "Speed", FormatSpeed(v.sog_mps));
  if (v.has_cog) row("Motion", "Course", Format("%.1f\xC2\xB0", v.cog_deg));
  if (v.has_heading) row("Motion", "Heading", Format("%.0f\xC2\xB0", v.heading_deg));
  if (v.has_rot) row("Motion", "Rate of turn", Format("%+.0f\xC2\xB0/min", v.rot_deg_per_min));
  if (v.has_altitude) row("Motion", "Altitude", std::to_string(v.altitude_m) + " m");

  if (!v.own_ship && ownship_.has_position) {
    const ais::CpaResult cpa = ais::ComputeCpa(ownship_, v, now_s_);
    if (v.has_position) {
      row("Approach", "Range", Format("%.2f nm", cpa.range_m / 1852.0));
      row("Approach", "Bearing", Format("%.0f\xC2\xB0", cpa.bearing_deg));
    }
    if (cpa.valid && cpa.tcpa_s >= 0.0) {
      row("Approach", "CPA", Format("%.2f nm", cpa.cpa_m / 1852.0));
      row("Approach", "TCPA", Format("%.1f min", cpa.tcpa_s / 60.0));
    }
  }

  row("Source", "Last heard", FormatAge(now_s_ - v.last_heard_s));
  row("Source", "Station", v.station);
  row("Source", "Credit", v.source_text);
  row("Source", "Layer", Name());
  return sheet;
}

void AisOverlay::Search(const app::SearchQuery& q, const std::atomic<bool>& cancel,
                        std::vector<app::SearchResult>& out) {
  const std::size_t before = out.size();
  table_.ForEach([&](const ais::Vessel& v) {
    if (cancel.load()) return;
    if (q.max_results > 0 && out.size() - before >= q.max_results) return;
    if (!Shows(v)) return;
    const GeoPoint at{v.lat_deg, v.lon_deg};
    if (!app::SearchAreaAccepts(q, at)) return;
    int best = -1;
    for (const std::string& label : {v.name, std::to_string(v.mmsi), v.call_sign}) {
      int quality = 0;
      if (!label.empty() && app::SearchTextAccepts(q, label, &quality)) best = std::max(best, quality);
    }
    if (best < 0) return;
    app::SearchResult r;
    r.overlay = this;
    r.feature = v.mmsi;
    r.match_quality = best;
    r.position = at;
    r.bounds = GeoRect{at, at};
    r.title = v.name.empty() ? "MMSI " + std::to_string(v.mmsi) : v.name;
    r.detail = v.kind != ais::TargetKind::kVessel ? ais::KindText(v.kind)
               : v.has_ship_type                  ? ais::ShipTypeText(v.ship_type)
                                                  : "Vessel";
    r.detail += " \xC2\xB7 MMSI " + std::to_string(v.mmsi);
    out.push_back(std::move(r));
  });
}

const std::vector<app::PropertySpec>& AisOverlay::Describe() const { return Specs(); }

Status AisOverlay::GetProperty(const std::string& key, app::PropertyValue* out) const {
  const int i = IndexOf(key);
  if (i < 0) return Status::Error(kNotFound, "no AIS property '" + key + "'");
  if (out != nullptr) *out = values_[static_cast<std::size_t>(i)];
  return Status::Ok();
}

Status AisOverlay::SetProperty(const std::string& key, const app::PropertyValue& value) {
  const int i = IndexOf(key);
  if (i < 0) return Status::Error(kNotFound, "no AIS property '" + key + "'");
  const PropertySpec& spec = Specs()[static_cast<std::size_t>(i)];
  if (value.type != spec.type)
    return Status::Error(kInvalidArg, "wrong type for AIS property '" + key + "'");
  if (spec.min != spec.max) {
    const double v = spec.type == PropertyType::kDouble ? value.d : static_cast<double>(value.i);
    if (v < spec.min || v > spec.max)
      return Status::Error(kInvalidArg, "AIS property '" + key + "' out of range");
  }
  if (spec.type == PropertyType::kChoice &&
      (value.i < 0 || value.i >= static_cast<long long>(spec.choices.size())))
    return Status::Error(kInvalidArg, "AIS property '" + key + "' has no such choice");
  values_[static_cast<std::size_t>(i)] = value;
  if (i == kStaleMinutes || i == kDropMinutes || i == kTrailMinutes) ApplyTableOptions();
  return Status::Ok();
}

}  // namespace fv
