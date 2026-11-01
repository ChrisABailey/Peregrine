// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// AIS vessel table and CPA/TCPA. See fvkit/ais/vessel_table.h.

#include "fvkit/ais/vessel_table.h"

#include <cmath>
#include <utility>

namespace fv {
namespace ais {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kEarthMeanRadiusM = 6371008.8;

bool IsBeacon(TargetKind k) {
  return k == TargetKind::kSart || k == TargetKind::kMob || k == TargetKind::kEpirb;
}

/// Kinds at a fixed place: no trail, and stationary for CPA.
bool IsFixed(TargetKind k) {
  return k == TargetKind::kBaseStation || k == TargetKind::kAidToNavigation;
}

TargetKind KindFromStation(StationKind k) {
  switch (k) {
    case StationKind::kBaseStation: return TargetKind::kBaseStation;
    case StationKind::kAidToNavigation: return TargetKind::kAidToNavigation;
    case StationKind::kSarAircraft: return TargetKind::kSarAircraft;
    case StationKind::kVessel: break;
  }
  return TargetKind::kVessel;
}

void CopyMotion(const AisMessage& m, Vessel* v, bool heading, bool nav_status, bool rot) {
  v->has_sog = m.has_sog;
  v->sog_mps = m.sog_mps;
  v->has_cog = m.has_cog;
  v->cog_deg = m.cog_deg;
  if (heading) {
    v->has_heading = m.has_heading;
    v->heading_deg = m.heading_deg;
  }
  if (nav_status) {
    v->has_nav_status = m.has_nav_status;
    v->nav_status = m.nav_status;
  }
  if (rot) {
    v->has_rot = m.has_rot;
    v->rot_deg_per_min = m.rot_deg_per_min;
  }
}

/// Merges static fields present in `m`. Returns true when any was present.
bool MergeStatic(const AisMessage& m, Vessel* v) {
  bool any = false;
  if (!m.name.empty()) { v->name = m.name; any = true; }
  if (!m.call_sign.empty()) { v->call_sign = m.call_sign; any = true; }
  if (m.has_imo) { v->has_imo = true; v->imo = m.imo; any = true; }
  if (m.has_ship_type) { v->has_ship_type = true; v->ship_type = m.ship_type; any = true; }
  if (m.has_dimensions) {
    v->has_dimensions = true;
    v->to_bow_m = m.to_bow_m;
    v->to_stern_m = m.to_stern_m;
    v->to_port_m = m.to_port_m;
    v->to_starboard_m = m.to_starboard_m;
    any = true;
  }
  if (m.has_mothership) { v->has_mothership = true; v->mothership_mmsi = m.mothership_mmsi; any = true; }
  if (m.type == 5) {
    // Voyage data is set by the crew and may be cleared; type 5 replaces it whole.
    v->has_eta = m.has_eta;
    v->eta_month = m.eta_month;
    v->eta_day = m.eta_day;
    v->eta_hour = m.eta_hour;
    v->eta_minute = m.eta_minute;
    v->has_draught = m.has_draught;
    v->draught_m = m.draught_m;
    v->destination = m.destination;
    any = true;
  }
  if (m.type == 21) {
    v->virtual_aid = m.virtual_aid;
    v->off_position = m.off_position;
    any = true;
  }
  return any;
}

}  // namespace

TargetKind KindFromMmsi(std::uint32_t mmsi) {
  if (mmsi >= 970000000u && mmsi <= 970999999u) return TargetKind::kSart;
  if (mmsi >= 972000000u && mmsi <= 972999999u) return TargetKind::kMob;
  if (mmsi >= 974000000u && mmsi <= 974999999u) return TargetKind::kEpirb;
  if (mmsi >= 111000000u && mmsi <= 111999999u) return TargetKind::kSarAircraft;
  if (mmsi >= 990000000u && mmsi <= 999999999u) return TargetKind::kAidToNavigation;
  if (mmsi >= 1000000u && mmsi <= 9999999u) return TargetKind::kBaseStation;  // 00MIDxxxx
  return TargetKind::kVessel;
}

CpaResult ComputeCpa(const PositionFix& ownship, const Vessel& target, double now_s) {
  CpaResult r;
  if (!ownship.has_position || !target.has_position) return r;

  bool target_known = true;
  double target_e = 0.0, target_n = 0.0;  // target velocity, m/s east and north
  if (!IsFixed(target.kind)) {
    if (!target.has_sog) {
      target_known = false;
    } else if (target.sog_mps > 0.0) {
      if (target.has_cog) {
        target_e = target.sog_mps * std::sin(target.cog_deg * kDegToRad);
        target_n = target.sog_mps * std::cos(target.cog_deg * kDegToRad);
      } else {
        target_known = false;
      }
    }
  }

  double lat = target.lat_deg;
  double lon = target.lon_deg;
  const double dt = now_s - target.position_time_s;
  if (target_known && dt > 0.0) {
    lat += target_n * dt / kEarthMeanRadiusM / kDegToRad;
    lon += target_e * dt / (kEarthMeanRadiusM * std::cos(lat * kDegToRad)) / kDegToRad;
  }

  double dlon = lon - ownship.lon;
  if (dlon > 180.0) dlon -= 360.0;
  if (dlon < -180.0) dlon += 360.0;
  const double mean_lat = 0.5 * (lat + ownship.lat) * kDegToRad;
  const double px = dlon * kDegToRad * std::cos(mean_lat) * kEarthMeanRadiusM;
  const double py = (lat - ownship.lat) * kDegToRad * kEarthMeanRadiusM;
  r.range_m = std::hypot(px, py);
  r.bearing_deg = std::atan2(px, py) / kDegToRad;
  if (r.bearing_deg < 0.0) r.bearing_deg += 360.0;
  if (!target_known) return r;

  double own_e = 0.0, own_n = 0.0;
  if (ownship.has_speed && ownship.has_true_heading) {
    own_e = ownship.speed_mps * std::sin(ownship.true_heading_deg * kDegToRad);
    own_n = ownship.speed_mps * std::cos(ownship.true_heading_deg * kDegToRad);
  }
  const double wx = target_e - own_e;
  const double wy = target_n - own_n;
  const double w2 = wx * wx + wy * wy;
  r.valid = true;
  if (w2 < 1e-12) {
    r.cpa_m = r.range_m;
    r.tcpa_s = 0.0;
    return r;
  }
  r.tcpa_s = -(px * wx + py * wy) / w2;
  r.cpa_m = std::hypot(px + wx * r.tcpa_s, py + wy * r.tcpa_s);
  return r;
}

VesselTable::VesselTable(VesselTableOptions options) : options_(options) {}

void VesselTable::SetOptions(const VesselTableOptions& options) {
  options_ = options;
  while (options_.max_vessels > 0 && vessels_.size() > options_.max_vessels) EvictOldest();
  for (auto& entry : vessels_) {
    Vessel& v = entry.second;
    const std::size_t before = v.trail.size();
    while (v.trail.size() > options_.trail_max_points) v.trail.pop_front();
    if (v.trail.size() != before) {
      v.revision = ++revision_;
      if (listener_) listener_(VesselChange::kUpdated, v);
    }
  }
}

const Vessel* VesselTable::Update(const AisMessage& m, double now_s) {
  if (m.mmsi == 0) return nullptr;

  auto it = vessels_.find(m.mmsi);
  const bool added = it == vessels_.end();
  if (added) {
    if (options_.max_vessels > 0 && vessels_.size() >= options_.max_vessels) EvictOldest();
    Vessel fresh;
    fresh.mmsi = m.mmsi;
    fresh.kind = KindFromMmsi(m.mmsi);
    fresh.first_heard_s = now_s;
    it = vessels_.emplace(m.mmsi, std::move(fresh)).first;
  }
  Vessel& v = it->second;

  // A beacon keeps its MMSI-given kind: a SART sends ordinary type 1 reports.
  if (!IsBeacon(v.kind) && m.kind != StationKind::kVessel) v.kind = KindFromStation(m.kind);
  switch (m.type) {
    case 1: case 2: case 3: case 5:
      v.vessel_class = VesselClass::kA;
      break;
    case 18: case 19: case 24:
      v.vessel_class = VesselClass::kB;
      break;
    default:
      break;
  }
  if (m.own_ship) {
    v.own_ship = true;
    own_ship_mmsi_ = m.mmsi;
  }
  v.last_heard_s = now_s;
  if (m.tag.has_source) v.station = m.tag.source;
  if (m.tag.has_text) v.source_text = m.tag.text;

  // A type 27 long-range report is coarse (1/10 minute); it does not replace a
  // recent position from a full-resolution report.
  bool take_position = m.has_position;
  if (take_position && m.type == 27 && v.has_position && v.position_type != 27) {
    const bool recent = options_.stale_after_s <= 0.0 ||
                        now_s - v.position_time_s < options_.stale_after_s;
    if (recent) take_position = false;
  }
  if (take_position) {
    v.has_position = true;
    v.lat_deg = m.lat_deg;
    v.lon_deg = m.lon_deg;
    v.position_time_s = now_s;
    v.position_type = m.type;
    v.position_accurate = m.position_accurate;
    switch (m.type) {
      case 1: case 2: case 3: CopyMotion(m, &v, true, true, true); break;
      case 18: case 19: CopyMotion(m, &v, true, false, false); break;
      case 27: CopyMotion(m, &v, false, true, false); break;
      case 9:
        CopyMotion(m, &v, false, false, false);
        v.has_altitude = m.has_altitude;
        v.altitude_m = m.altitude_m;
        break;
      default: break;
    }
    if (!IsFixed(v.kind) && options_.trail_max_points > 0) {
      const bool moved = v.trail.empty() || v.trail.back().lat_deg != v.lat_deg ||
                         v.trail.back().lon_deg != v.lon_deg;
      const bool due = v.trail.empty() ||
                       now_s - v.trail.back().time_s >= options_.trail_min_interval_s;
      if (moved && due) v.trail.push_back({v.lat_deg, v.lon_deg, now_s});
      PruneTrail(&v, now_s);
    }
  }

  if (MergeStatic(m, &v)) {
    v.has_static = true;
    v.static_time_s = now_s;
  }

  v.revision = ++revision_;
  if (listener_) listener_(added ? VesselChange::kAdded : VesselChange::kUpdated, v);
  return &v;
}

void VesselTable::Expire(double now_s) {
  for (auto it = vessels_.begin(); it != vessels_.end();) {
    Vessel& v = it->second;
    if (options_.drop_after_s > 0.0 && now_s - v.last_heard_s >= options_.drop_after_s) {
      auto next = std::next(it);
      Remove(it);
      it = next;
      continue;
    }
    const std::size_t before = v.trail.size();
    PruneTrail(&v, now_s);
    if (v.trail.size() != before) {
      v.revision = ++revision_;
      if (listener_) listener_(VesselChange::kUpdated, v);
    }
    ++it;
  }
}

bool VesselTable::IsStale(const Vessel& vessel, double now_s) const {
  // A static message keeps a record heard but says nothing about where it is.
  const double since = vessel.has_position ? vessel.position_time_s : vessel.last_heard_s;
  return options_.stale_after_s > 0.0 && now_s - since >= options_.stale_after_s;
}

const Vessel* VesselTable::Find(std::uint32_t mmsi) const {
  auto it = vessels_.find(mmsi);
  return it == vessels_.end() ? nullptr : &it->second;
}

void VesselTable::Clear() {
  while (!vessels_.empty()) Remove(vessels_.begin());
  own_ship_mmsi_ = 0;
}

void VesselTable::Remove(std::unordered_map<std::uint32_t, Vessel>::iterator it) {
  ++revision_;
  if (listener_) listener_(VesselChange::kRemoved, it->second);
  if (it->first == own_ship_mmsi_) own_ship_mmsi_ = 0;
  vessels_.erase(it);
}

void VesselTable::EvictOldest() {
  auto oldest = vessels_.end();
  for (auto it = vessels_.begin(); it != vessels_.end(); ++it) {
    if (it->second.own_ship) continue;
    if (oldest == vessels_.end() || it->second.last_heard_s < oldest->second.last_heard_s) oldest = it;
  }
  if (oldest == vessels_.end()) oldest = vessels_.begin();
  if (oldest != vessels_.end()) Remove(oldest);
}

void VesselTable::PruneTrail(Vessel* v, double now_s) const {
  while (v->trail.size() > options_.trail_max_points) v->trail.pop_front();
  if (options_.trail_max_age_s > 0.0) {
    while (!v->trail.empty() && now_s - v->trail.front().time_s > options_.trail_max_age_s)
      v->trail.pop_front();
  }
}

}  // namespace ais
}  // namespace fv
