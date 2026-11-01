// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// AIS vessel table: one record per MMSI merging static and dynamic data from
/// decoded `AisMessage`s, with trails, ageing, eviction and change
/// notifications, plus CPA/TCPA against an ownship fix. Time always comes from
/// the caller. Plan: port/ais-plan.md (AIS2).

#ifndef FVKIT_AIS_VESSEL_TABLE_H_
#define FVKIT_AIS_VESSEL_TABLE_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

#include "fvkit/ais/decoder.h"
#include "fvkit/nav/position.h"

namespace fv {
namespace ais {

/// What a station is, for symbol choice.
enum class TargetKind {
  kVessel,
  kBaseStation,
  kAidToNavigation,
  kSarAircraft,
  kSart,   ///< AIS search-and-rescue transmitter, MMSI 970xxxxxx
  kMob,    ///< man-overboard device, MMSI 972xxxxxx
  kEpirb,  ///< EPIRB-AIS, MMSI 974xxxxxx
};

/// Transponder class, from the message types heard. Only meaningful for vessels.
enum class VesselClass { kUnknown, kA, kB };

/// Kind implied by the MMSI alone (ITU-R M.585 numbering); `kVessel` when the
/// number says nothing more specific.
TargetKind KindFromMmsi(std::uint32_t mmsi);

/// One position in a vessel's trail.
struct TrailPoint {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double time_s = 0.0;
};

/// Everything known about one MMSI. Fields with a `has_` flag are valid only
/// when it is set. Times are on the caller's clock, in seconds.
struct Vessel {
  std::uint32_t mmsi = 0;
  TargetKind kind = TargetKind::kVessel;
  VesselClass vessel_class = VesselClass::kUnknown;
  bool own_ship = false;  ///< heard as VDO: the receiver's own transponder

  double first_heard_s = 0.0;
  double last_heard_s = 0.0;
  std::uint64_t revision = 0;  ///< the table revision of the last change to this record

  // Dynamic data.
  bool has_position = false;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double position_time_s = 0.0;
  int position_type = 0;  ///< message type that gave the position
  bool position_accurate = false;
  bool has_sog = false;
  double sog_mps = 0.0;
  bool has_cog = false;
  double cog_deg = 0.0;
  bool has_heading = false;
  int heading_deg = 0;
  bool has_nav_status = false;
  int nav_status = 15;
  bool has_rot = false;
  double rot_deg_per_min = 0.0;
  bool has_altitude = false;
  int altitude_m = 0;

  // Static and voyage data.
  bool has_static = false;  ///< any of the fields below has been heard
  double static_time_s = 0.0;
  std::string name;
  std::string call_sign;
  bool has_imo = false;
  std::uint32_t imo = 0;
  bool has_ship_type = false;
  int ship_type = 0;  ///< ITU ship and cargo type; the aid type for an AtoN
  bool has_dimensions = false;
  int to_bow_m = 0, to_stern_m = 0, to_port_m = 0, to_starboard_m = 0;
  bool has_mothership = false;
  std::uint32_t mothership_mmsi = 0;
  bool has_eta = false;
  int eta_month = 0, eta_day = 0, eta_hour = 0, eta_minute = 0;
  bool has_draught = false;
  double draught_m = 0.0;
  std::string destination;
  bool virtual_aid = false;
  bool off_position = false;

  // Attribution of the last message heard.
  std::string station;      ///< TAG block s:
  std::string source_text;  ///< TAG block t:, or the Open Waters credit line

  std::deque<TrailPoint> trail;  ///< oldest first; empty for base stations and AtoNs

  /// Overall length from the dimensions, metres; 0 when unknown.
  int length_m() const { return has_dimensions ? to_bow_m + to_stern_m : 0; }
  /// Overall beam from the dimensions, metres; 0 when unknown.
  int beam_m() const { return has_dimensions ? to_port_m + to_starboard_m : 0; }
};

/// How a record changed, for `VesselTable::Listener`.
enum class VesselChange { kAdded, kUpdated, kRemoved };

/// Table limits and ages. Zero disables a limit.
struct VesselTableOptions {
  double stale_after_s = 360.0;   ///< `IsStale` once the position is this old
  double drop_after_s = 1800.0;   ///< `Expire` removes records unheard this long
  std::size_t max_vessels = 10000;  ///< oldest-heard record evicted beyond this
  std::size_t trail_max_points = 120;  ///< 0 keeps no trail
  double trail_max_age_s = 1800.0;
  double trail_min_interval_s = 10.0;  ///< a new trail point at most this often
};

/// Closest point of approach of a target relative to ownship.
struct CpaResult {
  bool valid = false;      ///< false when either position, or the target's motion, is unknown
  double range_m = 0.0;    ///< present distance
  double bearing_deg = 0.0;  ///< present true bearing from ownship to target
  double cpa_m = 0.0;      ///< distance at closest approach
  double tcpa_s = 0.0;     ///< time to closest approach; negative when it has passed
};

/// Computes CPA/TCPA of `target` from `ownship` at `now_s`, both moving in
/// straight lines on a local flat earth. The target is dead-reckoned from its
/// position time to `now_s`; the ownship fix is taken as current. Ownship
/// without speed or course counts as stationary, as do base stations and
/// AtoNs; a moving target without SOG and COG gives `valid` false but still
/// fills range and bearing.
CpaResult ComputeCpa(const PositionFix& ownship, const Vessel& target, double now_s);

/// MMSI-keyed store of vessels fed by `AisDecoder` output. Not thread safe;
/// one table per feed, updated and read on the same thread.
class VesselTable {
 public:
  /// Called after a record is added or updated, and before one is removed.
  /// It must not modify the table: removals are reported mid-iteration.
  using Listener = std::function<void(VesselChange, const Vessel&)>;

  explicit VesselTable(VesselTableOptions options = {});

  const VesselTableOptions& options() const { return options_; }
  void SetOptions(const VesselTableOptions& options);
  void SetListener(Listener listener) { listener_ = std::move(listener); }

  /// Merges one message heard at `now_s`. Returns the updated record, or
  /// nullptr when the message carries no MMSI.
  const Vessel* Update(const AisMessage& message, double now_s);

  /// Removes records unheard for `drop_after_s` and trail points older than
  /// `trail_max_age_s`. Call periodically.
  void Expire(double now_s);

  /// True when `vessel`'s position (or, without one, the vessel) is older
  /// than `stale_after_s`.
  bool IsStale(const Vessel& vessel, double now_s) const;

  const Vessel* Find(std::uint32_t mmsi) const;
  std::size_t size() const { return vessels_.size(); }
  void Clear();

  /// MMSI heard through VDO, or 0.
  std::uint32_t own_ship_mmsi() const { return own_ship_mmsi_; }

  /// Increments on every change to any record, including removals.
  std::uint64_t revision() const { return revision_; }

  /// Calls `fn` for every record, in no particular order.
  template <typename Fn>
  void ForEach(Fn&& fn) const {
    for (const auto& entry : vessels_) fn(entry.second);
  }

 private:
  void Remove(std::unordered_map<std::uint32_t, Vessel>::iterator it);
  void EvictOldest();
  void PruneTrail(Vessel* v, double now_s) const;

  VesselTableOptions options_;
  Listener listener_;
  std::unordered_map<std::uint32_t, Vessel> vessels_;
  std::uint32_t own_ship_mmsi_ = 0;
  std::uint64_t revision_ = 0;
};

}  // namespace ais
}  // namespace fv

#endif  // FVKIT_AIS_VESSEL_TABLE_H_
