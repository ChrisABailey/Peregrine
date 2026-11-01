// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// Open Waters AIS frames and subscription boxes. See fvkit/ais/openwaters.h.

#include "fvkit/ais/openwaters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "fvkit/nav/gpx.h"  // ParseIso8601Utc

namespace fv {
namespace ais {
namespace {

using Json = nlohmann::json;

constexpr double kKnot = 1852.0 / 3600.0;
// The box is sent at four decimals, rounded outward.
constexpr double kAreaMargin = 0.98;

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    s.remove_suffix(1);
  return s;
}

std::string String(const Json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool Number(const Json& j, const char* key, double* out) {
  const auto it = j.find(key);
  if (it == j.end() || !it->is_number()) return false;
  *out = it->get<double>();
  return true;
}

/// The tier fields of a `welcome.limits` or token `claims` object; absent
/// fields keep their defaults.
void ReadLimits(const Json& j, OpenWatersLimits* out) {
  double v = 0;
  if (Number(j, "area", &v) && v > 0) out->area_deg2 = v;
  if (Number(j, "rate", &v) && v > 0) out->rate_per_s = v;
  if (Number(j, "conns", &v) && v > 0) out->connections = static_cast<int>(v);
  if (Number(j, "connects_per_min", &v) && v > 0) out->connects_per_min = static_cast<int>(v);
}

/// Text as AIS sends it: cut at the first '@', trimmed.
std::string AisText(const Json& j, const char* key) {
  std::string s = String(j, key);
  const std::size_t at = s.find('@');
  if (at != std::string::npos) s.resize(at);
  return std::string(Trim(s));
}

void ReadDimensions(const Json& j, AisMessage* m) {
  const auto it = j.find("Dimension");
  if (it == j.end() || !it->is_object()) return;
  double a = 0, b = 0, c = 0, d = 0;
  Number(*it, "A", &a);
  Number(*it, "B", &b);
  Number(*it, "C", &c);
  Number(*it, "D", &d);
  if (a + b + c + d <= 0) return;
  m->has_dimensions = true;
  m->to_bow_m = static_cast<int>(a);
  m->to_stern_m = static_cast<int>(b);
  m->to_port_m = static_cast<int>(c);
  m->to_starboard_m = static_cast<int>(d);
}

void ReadShipType(const Json& j, const char* key, AisMessage* m) {
  double type = 0;
  if (Number(j, key, &type) && type > 0) {
    m->has_ship_type = true;
    m->ship_type = static_cast<int>(type);
  }
}

/// The decoded `message` object as an `AisMessage`, with AIS "not available"
/// values left invalid as the bit decoder leaves them. Field names are the
/// aisstream.io schema Open Waters relays.
bool FallbackMessage(const Json& msg, std::uint32_t mmsi, AisMessage* m) {
  double id = 0;
  if (!Number(msg, "MessageID", &id)) return false;
  m->type = static_cast<int>(id);
  double user = 0;
  m->mmsi = Number(msg, "UserID", &user) && user > 0 ? static_cast<std::uint32_t>(user) : mmsi;
  if (m->mmsi == 0) return false;
  switch (m->type) {
    case 4: m->kind = StationKind::kBaseStation; break;
    case 9: m->kind = StationKind::kSarAircraft; break;
    case 21: m->kind = StationKind::kAidToNavigation; break;
    default: break;
  }

  double lat = 91, lon = 181;
  if (Number(msg, "Latitude", &lat) && Number(msg, "Longitude", &lon) && std::fabs(lat) <= 90.0 &&
      std::fabs(lon) <= 180.0) {
    m->has_position = true;
    m->lat_deg = lat;
    m->lon_deg = lon;
  }
  double v = 0;
  if (Number(msg, "Sog", &v) && v >= 0.0 && v < 102.3) {
    m->has_sog = true;
    m->sog_mps = v * kKnot;
  }
  if (Number(msg, "Cog", &v) && v >= 0.0 && v < 360.0) {
    m->has_cog = true;
    m->cog_deg = v;
  }
  if (Number(msg, "TrueHeading", &v) && v >= 0.0 && v < 360.0) {
    m->has_heading = true;
    m->heading_deg = static_cast<int>(v);
  }
  if (Number(msg, "NavigationalStatus", &v) && v != 15) {
    m->has_nav_status = true;
    m->nav_status = static_cast<int>(v);
  }

  m->name = AisText(msg, "Name");
  m->call_sign = AisText(msg, "CallSign");
  m->destination = AisText(msg, "Destination");
  if (Number(msg, "ImoNumber", &v) && v > 0) {
    m->has_imo = true;
    m->imo = static_cast<std::uint32_t>(v);
  }
  ReadShipType(msg, "Type", m);
  ReadDimensions(msg, m);
  if (Number(msg, "MaximumStaticDraught", &v) && v > 0) {
    m->has_draught = true;
    m->draught_m = v;
  }
  const auto eta = msg.find("Eta");
  if (eta != msg.end() && eta->is_object()) {
    double mo = 0, d = 0, h = 24, mi = 60;
    Number(*eta, "Month", &mo);
    Number(*eta, "Day", &d);
    Number(*eta, "Hour", &h);
    Number(*eta, "Minute", &mi);
    if (mo > 0 && d > 0) {
      m->has_eta = true;
      m->eta_month = static_cast<int>(mo);
      m->eta_day = static_cast<int>(d);
      m->eta_hour = static_cast<int>(h);
      m->eta_minute = static_cast<int>(mi);
    }
  }
  if (m->type == 21) {
    const auto off = msg.find("OffPosition");
    m->off_position = off != msg.end() && off->is_boolean() && off->get<bool>();
    const auto virt = msg.find("VirtualAtoN");
    m->virtual_aid = virt != msg.end() && virt->is_boolean() && virt->get<bool>();
  }
  if (m->type == 24) {
    const auto part_b = msg.find("PartNumber");
    m->part = part_b != msg.end() && part_b->is_boolean() && part_b->get<bool>() ? 1 : 0;
    const auto report = msg.find(m->part == 0 ? "ReportA" : "ReportB");
    if (report != msg.end() && report->is_object()) {
      if (m->part == 0) {
        m->name = AisText(*report, "Name");
      } else {
        m->call_sign = AisText(*report, "CallSign");
        ReadShipType(*report, "ShipType", m);
        ReadDimensions(*report, m);
      }
    }
  }
  return true;
}

}  // namespace

std::string_view OpenWatersSsePayload(std::string_view line) {
  line = Trim(line);
  if (line.substr(0, 5) != "data:") return {};
  return Trim(line.substr(5));
}

bool ParseOpenWatersFrame(std::string_view json, OpenWatersFrame* out, std::string* error) {
  *out = OpenWatersFrame{};
  const Json j = Json::parse(json.begin(), json.end(), nullptr, /*allow_exceptions=*/false);
  if (!j.is_object()) {
    if (error) *error = "not a JSON object";
    return false;
  }
  const std::string type = String(j, "type");
  if (type == "welcome") {
    out->kind = OpenWatersFrameKind::kWelcome;
    out->role = String(j, "role");
    const auto limits = j.find("limits");
    if (limits != j.end() && limits->is_object()) ReadLimits(*limits, &out->limits);
    return true;
  }
  if (type == "error") {
    out->kind = OpenWatersFrameKind::kError;
    out->error = String(j, "message");
    if (out->error.empty()) out->error = String(j, "error");
    return true;
  }
  if (type != "event") return true;

  out->kind = OpenWatersFrameKind::kEvent;
  double v = 0;
  if (Number(j, "mmsi", &v) && v > 0) out->mmsi = static_cast<std::uint32_t>(v);
  out->msg_type = String(j, "msg_type");
  const std::string time = String(j, "time");
  if (!time.empty()) ParseIso8601Utc(time, &out->time_s);
  const auto synth = j.find("synthesized");
  out->synthesized = synth != j.end() && synth->is_boolean() && synth->get<bool>();
  out->source = String(j, "source");
  out->license = String(j, "license");
  out->attribution = String(j, "attribution");
  out->station = String(j, "station");
  const auto nmea = j.find("nmea");
  if (nmea != j.end() && nmea->is_array()) {
    for (const Json& s : *nmea)
      if (s.is_string()) out->nmea.push_back(s.get<std::string>());
  }
  if (out->nmea.empty()) {
    const auto msg = j.find("message");
    if (msg != j.end() && msg->is_object())
      out->has_fallback = FallbackMessage(*msg, out->mmsi, &out->fallback);
  }
  return true;
}

GeoRect OpenWatersBox(const GeoRect& view, const OpenWatersLimits& limits, double pad_fraction) {
  double south = std::clamp(view.ll.lat, -90.0, 90.0);
  double north = std::clamp(view.ur.lat, -90.0, 90.0);
  double west = std::clamp(view.ll.lon, -180.0, 180.0);
  double east = view.CrossesAntimeridian() ? 180.0 : std::clamp(view.ur.lon, -180.0, 180.0);
  if (north < south) std::swap(north, south);
  if (east < west) std::swap(east, west);

  const double cy = (south + north) / 2.0;
  const double cx = (west + east) / 2.0;
  const double pad = std::max(pad_fraction, 0.0);
  double h = (north - south) * (1.0 + 2.0 * pad);
  double w = (east - west) * (1.0 + 2.0 * pad);
  const double area = limits.area_deg2 * kAreaMargin;
  if (area > 0.0 && h * w > area) {
    const double k = std::sqrt(area / (h * w));
    h *= k;
    w *= k;
  }
  GeoRect box;
  box.ll = {std::max(cy - h / 2.0, -90.0), std::max(cx - w / 2.0, -180.0)};
  box.ur = {std::min(cy + h / 2.0, 90.0), std::min(cx + w / 2.0, 180.0)};
  return box;
}

bool OpenWatersBoxServes(const GeoRect& box, const GeoRect& view, const OpenWatersLimits& limits) {
  const double east = view.CrossesAntimeridian() ? 180.0 : view.ur.lon;
  if (view.ll.lat >= box.ll.lat && view.ur.lat <= box.ur.lat && view.ll.lon >= box.ll.lon &&
      east <= box.ur.lon) {
    return true;
  }
  const double view_area = (view.ur.lat - view.ll.lat) * (east - view.ll.lon);
  if (limits.area_deg2 <= 0.0 || view_area <= limits.area_deg2 * kAreaMargin) return false;
  const double cy = (view.ll.lat + view.ur.lat) / 2.0;
  const double cx = (view.ll.lon + east) / 2.0;
  const double by = (box.ll.lat + box.ur.lat) / 2.0;
  const double bx = (box.ll.lon + box.ur.lon) / 2.0;
  return std::fabs(cy - by) <= (box.ur.lat - box.ll.lat) / 4.0 &&
         std::fabs(cx - bx) <= (box.ur.lon - box.ll.lon) / 4.0;
}

std::string OpenWatersStreamUrl(const GeoRect& box, bool snapshot) {
  char buf[200];
  std::snprintf(buf, sizeof buf,
                "https://ais.openwaters.io/v1/stream?bbox=%.4f,%.4f,%.4f,%.4f%s",
                std::floor(box.ll.lat * 1e4) / 1e4, std::floor(box.ll.lon * 1e4) / 1e4,
                std::ceil(box.ur.lat * 1e4) / 1e4, std::ceil(box.ur.lon * 1e4) / 1e4,
                snapshot ? "&snapshot=1" : "");
  return buf;
}

const char kOpenWatersKeysUrl[] = "https://ais.openwaters.io/v1/keys";

std::string OpenWatersMintMessage(std::string_view pubkey, std::int64_t ts, bool bind_ip,
                                  std::string_view name, std::string_view vessel_name) {
  std::string m = "aiscast-key\n";
  m.append(pubkey);
  m += '\n';
  m += std::to_string(ts);
  m += bind_ip ? "\n1\n" : "\n0\n";
  m.append(name);
  m += '\n';
  m.append(vessel_name);
  return m;
}

std::string OpenWatersMintBody(std::string_view pubkey, std::int64_t ts, std::string_view sig) {
  Json j = Json::object();
  j["pubkey"] = std::string(pubkey);
  j["ts"] = ts;
  j["sig"] = std::string(sig);
  return j.dump();
}

bool ParseOpenWatersToken(std::string_view json, OpenWatersToken* out, std::string* error) {
  *out = OpenWatersToken{};
  const Json j = Json::parse(json.begin(), json.end(), nullptr, /*allow_exceptions=*/false);
  if (!j.is_object()) {
    if (error) *error = "not a JSON object";
    return false;
  }
  out->token = String(j, "token");
  if (out->token.empty()) {
    if (error) *error = String(j, "error").empty() ? "no token" : String(j, "error");
    return false;
  }
  const auto signed_it = j.find("signed");
  out->is_signed = signed_it != j.end() && signed_it->is_boolean() && signed_it->get<bool>();
  out->name_error = String(j, "name_error");
  const auto claims = j.find("claims");
  if (claims != j.end() && claims->is_object()) {
    out->sub = String(*claims, "sub");
    out->role = String(*claims, "role");
    Number(*claims, "exp", &out->exp_s);
    ReadLimits(*claims, &out->limits);
  }
  return true;
}

}  // namespace ais
}  // namespace fv
