// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// AIS decoding: NMEA 0183 `!xxVDM`/`!xxVDO` sentences, with an optional
/// NMEA 4.10 TAG block, to one `AisMessage` per complete radio message.
/// Field layouts follow ITU-R M.1371-5. Plan: port/ais-plan.md (AIS1).

#ifndef FVKIT_AIS_DECODER_H_
#define FVKIT_AIS_DECODER_H_

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fv {
namespace ais {

/// Fields of an NMEA 4.10 TAG block (`\s:station,c:unix_time,t:text*hh\`).
/// Kept only when the block's checksum matches.
struct TagBlock {
  bool has_source = false;
  std::string source;      ///< s: receiving station
  bool has_time = false;
  std::int64_t unix_s = 0; ///< c: seconds since 1970 (a 13-digit value is read as ms)
  bool has_text = false;
  std::string text;        ///< t: free text
};

/// One `!xxVDM`/`!xxVDO` sentence, checksum verified, payload still armoured.
struct AisSentence {
  std::string talker;      ///< "AI", "BS", "AB", …
  bool own_ship = false;   ///< VDO: the receiver's own transponder
  int fragment_count = 1;
  int fragment_number = 1;
  int sequence_id = -1;    ///< -1 when the field is empty
  char channel = 0;        ///< 'A', 'B', '1', '2', or 0 when empty
  std::string payload;
  int fill_bits = 0;
  TagBlock tag;
};

/// Parses one line. Returns false with `error` set when the line is not a
/// well-formed AIS sentence or its checksum does not match.
bool ParseAisSentence(std::string_view line, AisSentence* out, std::string* error);

/// Unpacks 6-bit armoured payload text into one bit per byte, dropping
/// `fill_bits` from the end. Returns false on a character outside the armour set.
bool UnarmorPayload(std::string_view payload, int fill_bits, std::vector<std::uint8_t>* bits);

/// Kind of station, from the message type and the MMSI.
enum class StationKind {
  kVessel,
  kBaseStation,  ///< type 4
  kAidToNavigation,  ///< type 21
  kSarAircraft,  ///< type 9
};

/// One decoded AIS message. Values AIS marks "not available" leave the
/// matching `has_` flag false. Units: degrees, metres, metres per second.
struct AisMessage {
  int type = 0;
  int repeat = 0;
  std::uint32_t mmsi = 0;
  bool own_ship = false;
  char channel = 0;
  std::string talker;
  TagBlock tag;
  StationKind kind = StationKind::kVessel;

  // Position report (1–3, 4, 9, 18, 19, 21, 27).
  bool has_position = false;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  bool position_accurate = false;  ///< DGNSS quality, better than 10 m
  bool raim = false;
  bool has_sog = false;
  double sog_mps = 0.0;
  bool sog_at_limit = false;  ///< reported as the field maximum, "or more"
  bool has_cog = false;
  double cog_deg = 0.0;
  bool has_heading = false;
  int heading_deg = 0;
  bool has_nav_status = false;
  int nav_status = 15;  ///< ITU table; 15 is "not defined"
  bool has_rot = false;
  double rot_deg_per_min = 0.0;  ///< positive is turning right
  int turn_indicator = 0;  ///< -1 / +1 turning left / right faster than 5°/30 s with no rate, else 0
  bool has_timestamp = false;
  int timestamp_s = 0;  ///< UTC second of the report, 0–59

  // Static and voyage data (5, 19, 21, 24).
  int part = -1;  ///< type 24 part: 0 (name) or 1 (type, call sign, dimensions)
  bool has_imo = false;
  std::uint32_t imo = 0;
  std::string call_sign;
  std::string name;
  bool has_ship_type = false;
  int ship_type = 0;  ///< ITU ship and cargo type as sent (only 20–99 are defined); for type 21 the aid type
  bool has_dimensions = false;
  int to_bow_m = 0;
  int to_stern_m = 0;
  int to_port_m = 0;
  int to_starboard_m = 0;
  bool has_mothership = false;
  std::uint32_t mothership_mmsi = 0;  ///< type 24 B, auxiliary craft (98xxxxxxx)
  int epfd = 0;  ///< position fixing device; 0 is undefined
  bool has_eta = false;
  int eta_month = 0, eta_day = 0, eta_hour = 0, eta_minute = 0;
  bool has_draught = false;
  double draught_m = 0.0;
  std::string destination;

  // Base station UTC (4).
  bool has_utc = false;
  int utc_year = 0, utc_month = 0, utc_day = 0, utc_hour = 0, utc_minute = 0, utc_second = 0;

  // SAR aircraft (9).
  bool has_altitude = false;
  int altitude_m = 0;
  bool altitude_at_limit = false;

  // Aid to navigation (21).
  bool off_position = false;
  bool virtual_aid = false;
};

/// Decodes an unarmoured bit string. Returns false with `error` set when the
/// type is not supported or the message is too short for its fixed fields.
bool DecodeAisBits(const std::vector<std::uint8_t>& bits, AisMessage* out, std::string* error);

/// Outcome of `AisDecoder::Feed`.
enum class FeedResult {
  kMessage,      ///< `out` holds a complete message
  kPending,      ///< a fragment was stored; more are expected
  kNotAis,       ///< not a VDM/VDO sentence (other NMEA traffic); ignored
  kError,        ///< malformed sentence, bad checksum, or undecodable payload
  kUnsupported,  ///< well formed, but a message type this decoder does not read
};

/// Running counts since construction.
struct DecoderStats {
  std::uint64_t messages = 0;
  std::uint64_t errors = 0;
  std::uint64_t unsupported = 0;
  std::uint64_t fragments_dropped = 0;  ///< incomplete multipart messages that timed out or were broken
};

/// Turns a stream of NMEA lines into AIS messages, reassembling multipart
/// messages. Not thread safe; one decoder per feed.
class AisDecoder {
 public:
  /// `fragment_timeout_s`: an incomplete multipart message older than this is dropped.
  explicit AisDecoder(double fragment_timeout_s = 10.0);

  /// Feeds one line received at `now_s` (any monotonic clock, seconds).
  FeedResult Feed(std::string_view line, double now_s, AisMessage* out, std::string* error = nullptr);

  const DecoderStats& stats() const { return stats_; }

  /// Number of multipart messages waiting for fragments.
  std::size_t pending() const { return pending_.size(); }

 private:
  struct Pending {
    double first_s = 0.0;
    std::vector<std::string> payloads;  ///< by fragment number - 1; empty until received
    int received = 0;
    int fill_bits = 0;
    TagBlock tag;
  };
  // (talker + own flag, sequence id, channel, fragment count)
  using Key = std::tuple<std::string, int, char, int>;

  void Expire(double now_s);

  double timeout_s_;
  std::map<Key, Pending> pending_;
  DecoderStats stats_;
};

}  // namespace ais
}  // namespace fv

#endif  // FVKIT_AIS_DECODER_H_
