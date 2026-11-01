// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/ais/decoder.h"

#include <cmath>
#include <cstdlib>

#include "fvkit/nav/nmea.h"

namespace fv {
namespace ais {
namespace {

constexpr double kKnotToMps = 1852.0 / 3600.0;

/// Splits on ',' keeping empty fields.
std::vector<std::string_view> SplitFields(std::string_view s) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (;;) {
    const std::size_t comma = s.find(',', start);
    if (comma == std::string_view::npos) {
      out.push_back(s.substr(start));
      return out;
    }
    out.push_back(s.substr(start, comma - start));
    start = comma + 1;
  }
}

/// Parses a non-negative decimal integer; false on empty or non-digit input.
bool ParseUint(std::string_view s, std::int64_t* out) {
  if (s.empty() || s.size() > 18) return false;
  std::int64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  *out = v;
  return true;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

/// Reads the two hex digits after '*'; -1 when malformed.
int ReadChecksum(std::string_view s) {
  if (s.size() < 2) return -1;
  const int hi = HexDigit(s[0]);
  const int lo = HexDigit(s[1]);
  return hi < 0 || lo < 0 ? -1 : hi * 16 + lo;
}

/// Parses the body of a TAG block (between the backslashes). Returns an empty
/// block when the checksum is missing or wrong.
TagBlock ParseTagBlock(std::string_view block) {
  TagBlock tag;
  const std::size_t star = block.rfind('*');
  if (star == std::string_view::npos) return tag;
  const int sum = ReadChecksum(block.substr(star + 1));
  if (sum < 0 || sum != NmeaChecksum(block.data(), star)) return tag;
  for (std::string_view field : SplitFields(block.substr(0, star))) {
    if (field.size() < 2 || field[1] != ':') continue;
    const std::string_view value = field.substr(2);
    switch (field[0]) {
      case 's':
        tag.has_source = true;
        tag.source = std::string(value);
        break;
      case 'c': {
        std::int64_t t = 0;
        if (ParseUint(value, &t)) {
          // Some receivers stamp milliseconds.
          tag.unix_s = value.size() >= 13 ? t / 1000 : t;
          tag.has_time = true;
        }
        break;
      }
      case 't':
        tag.has_text = true;
        tag.text = std::string(value);
        break;
      default:
        break;
    }
  }
  return tag;
}

/// Splits a leading TAG block off `line`. Returns the sentence part.
std::string_view StripTag(std::string_view line, std::string_view* tag) {
  *tag = {};
  if (line.empty() || line[0] != '\\') return line;
  const std::size_t end = line.find('\\', 1);
  if (end == std::string_view::npos) return line;
  *tag = line.substr(1, end - 1);
  return line.substr(end + 1);
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
    s.remove_suffix(1);
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  return s;
}

bool LooksLikeVdmVdo(std::string_view sentence) {
  return sentence.size() >= 6 && (sentence[0] == '!' || sentence[0] == '$') && sentence[3] == 'V' &&
         sentence[4] == 'D' && (sentence[5] == 'M' || sentence[5] == 'O');
}

/// Reads fields from an unarmoured bit string. Bits past the end read as zero,
/// so a short trailing field decodes as "not available" rather than failing.
class BitReader {
 public:
  explicit BitReader(const std::vector<std::uint8_t>& bits) : bits_(bits) {}

  std::uint32_t U(int start, int length) const {
    std::uint32_t v = 0;
    for (int i = start; i < start + length; ++i) {
      v <<= 1;
      if (i < static_cast<int>(bits_.size())) v |= bits_[i];
    }
    return v;
  }

  std::int32_t S(int start, int length) const {
    const std::uint32_t v = U(start, length);
    const std::uint32_t sign = 1u << (length - 1);
    return static_cast<std::int32_t>((v ^ sign) - sign);
  }

  bool B(int bit) const { return U(bit, 1) != 0; }

  /// Six-bit ASCII text of `chars` characters, cut at the first '@' and
  /// trimmed of spaces at both ends. Characters past the end are dropped.
  std::string Text(int start, int chars) const {
    std::string s;
    for (int i = 0; i < chars; ++i) {
      const int at = start + i * 6;
      if (at + 6 > static_cast<int>(bits_.size())) break;
      const std::uint32_t v = U(at, 6);
      if (v == 0) break;  // '@' pads the field
      s.push_back(static_cast<char>(v < 32 ? v + 64 : v));
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    const std::size_t first = s.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : s.substr(first);
  }

  int size() const { return static_cast<int>(bits_.size()); }

 private:
  const std::vector<std::uint8_t>& bits_;
};

/// Longitude and latitude in 1/10000 minute (`scale` 600000) or 1/10 minute (600).
void SetPosition(const BitReader& r, int lon_bit, int lon_len, int lat_bit, int lat_len, double scale,
                 AisMessage* m) {
  const std::int32_t lon = r.S(lon_bit, lon_len);
  const std::int32_t lat = r.S(lat_bit, lat_len);
  const double lon_deg = lon / scale;
  const double lat_deg = lat / scale;
  // 181 and 91 are the "not available" values; anything else out of range is corrupt.
  if (std::fabs(lon_deg) <= 180.0 && std::fabs(lat_deg) <= 90.0) {
    m->has_position = true;
    m->lon_deg = lon_deg;
    m->lat_deg = lat_deg;
  }
}

/// SOG in 1/10 knot, 1023 not available, 1022 "102.2 knots or more".
void SetSogTenths(std::uint32_t v, AisMessage* m) {
  if (v == 1023) return;
  m->has_sog = true;
  m->sog_mps = v / 10.0 * kKnotToMps;
  m->sog_at_limit = v == 1022;
}

/// COG in 1/10 degree, 3600 and above not available.
void SetCogTenths(std::uint32_t v, AisMessage* m) {
  if (v >= 3600) return;
  m->has_cog = true;
  m->cog_deg = v / 10.0;
}

void SetHeading(std::uint32_t v, AisMessage* m) {
  if (v > 359) return;  // 511 not available
  m->has_heading = true;
  m->heading_deg = static_cast<int>(v);
}

void SetTimestamp(std::uint32_t v, AisMessage* m) {
  if (v > 59) return;  // 60 not available, 61-63 positioning-system states
  m->has_timestamp = true;
  m->timestamp_s = static_cast<int>(v);
}

void SetNavStatus(std::uint32_t v, AisMessage* m) {
  m->nav_status = static_cast<int>(v);
  m->has_nav_status = v != 15;
}

void SetDimensions(const BitReader& r, int start, AisMessage* m) {
  m->to_bow_m = static_cast<int>(r.U(start, 9));
  m->to_stern_m = static_cast<int>(r.U(start + 9, 9));
  m->to_port_m = static_cast<int>(r.U(start + 18, 6));
  m->to_starboard_m = static_cast<int>(r.U(start + 24, 6));
  m->has_dimensions = m->to_bow_m || m->to_stern_m || m->to_port_m || m->to_starboard_m;
}

void SetShipType(std::uint32_t v, AisMessage* m) {
  m->ship_type = static_cast<int>(v);
  m->has_ship_type = v != 0;
}

void DecodeClassAPosition(const BitReader& r, AisMessage* m) {
  SetNavStatus(r.U(38, 4), m);
  const std::int32_t rot = r.S(42, 8);
  if (rot == 127 || rot == -127) {
    m->turn_indicator = rot > 0 ? 1 : -1;
  } else if (rot != -128) {
    const double rate = rot / 4.733;
    m->has_rot = true;
    m->rot_deg_per_min = rot < 0 ? -rate * rate : rate * rate;
  }
  SetSogTenths(r.U(50, 10), m);
  m->position_accurate = r.B(60);
  SetPosition(r, 61, 28, 89, 27, 600000.0, m);
  SetCogTenths(r.U(116, 12), m);
  SetHeading(r.U(128, 9), m);
  SetTimestamp(r.U(137, 6), m);
  m->raim = r.B(148);
}

void DecodeBaseStation(const BitReader& r, AisMessage* m) {
  m->utc_year = static_cast<int>(r.U(38, 14));
  m->utc_month = static_cast<int>(r.U(52, 4));
  m->utc_day = static_cast<int>(r.U(56, 5));
  m->utc_hour = static_cast<int>(r.U(61, 5));
  m->utc_minute = static_cast<int>(r.U(66, 6));
  m->utc_second = static_cast<int>(r.U(72, 6));
  m->has_utc = m->utc_year != 0 && m->utc_month >= 1 && m->utc_month <= 12 && m->utc_day != 0 &&
               m->utc_hour < 24 && m->utc_minute < 60 && m->utc_second < 60;
  m->position_accurate = r.B(78);
  SetPosition(r, 79, 28, 107, 27, 600000.0, m);
  m->epfd = static_cast<int>(r.U(134, 4));
  m->raim = r.B(148);
}

void DecodeStaticVoyage(const BitReader& r, AisMessage* m) {
  m->imo = r.U(40, 30);
  m->has_imo = m->imo != 0;
  m->call_sign = r.Text(70, 7);
  m->name = r.Text(112, 20);
  SetShipType(r.U(232, 8), m);
  SetDimensions(r, 240, m);
  m->epfd = static_cast<int>(r.U(270, 4));
  m->eta_month = static_cast<int>(r.U(274, 4));
  m->eta_day = static_cast<int>(r.U(278, 5));
  m->eta_hour = static_cast<int>(r.U(283, 5));
  m->eta_minute = static_cast<int>(r.U(288, 6));
  m->has_eta = m->eta_month >= 1 && m->eta_month <= 12 && m->eta_day != 0;
  const std::uint32_t draught = r.U(294, 8);
  m->has_draught = draught != 0;
  m->draught_m = draught / 10.0;
  m->destination = r.Text(302, 20);
}

void DecodeSarAircraft(const BitReader& r, AisMessage* m) {
  const std::uint32_t alt = r.U(38, 12);
  if (alt != 4095) {
    m->has_altitude = true;
    m->altitude_m = static_cast<int>(alt);
    m->altitude_at_limit = alt == 4094;
  }
  const std::uint32_t sog = r.U(50, 10);  // whole knots for aircraft
  if (sog != 1023) {
    m->has_sog = true;
    m->sog_mps = sog * kKnotToMps;
    m->sog_at_limit = sog == 1022;
  }
  m->position_accurate = r.B(60);
  SetPosition(r, 61, 28, 89, 27, 600000.0, m);
  SetCogTenths(r.U(116, 12), m);
  SetTimestamp(r.U(128, 6), m);
  m->raim = r.B(147);
}

void DecodeClassBPosition(const BitReader& r, AisMessage* m) {
  SetSogTenths(r.U(46, 10), m);
  m->position_accurate = r.B(56);
  SetPosition(r, 57, 28, 85, 27, 600000.0, m);
  SetCogTenths(r.U(112, 12), m);
  SetHeading(r.U(124, 9), m);
  SetTimestamp(r.U(133, 6), m);
}

void DecodeClassBExtended(const BitReader& r, AisMessage* m) {
  DecodeClassBPosition(r, m);
  m->name = r.Text(143, 20);
  SetShipType(r.U(263, 8), m);
  SetDimensions(r, 271, m);
  m->epfd = static_cast<int>(r.U(301, 4));
  m->raim = r.B(305);
}

void DecodeAidToNavigation(const BitReader& r, AisMessage* m) {
  SetShipType(r.U(38, 5), m);
  m->name = r.Text(43, 20);
  m->position_accurate = r.B(163);
  SetPosition(r, 164, 28, 192, 27, 600000.0, m);
  SetDimensions(r, 219, m);
  m->epfd = static_cast<int>(r.U(249, 4));
  SetTimestamp(r.U(253, 6), m);
  m->off_position = r.B(259);
  m->raim = r.B(268);
  m->virtual_aid = r.B(269);
  // A name longer than 20 characters continues after the fixed fields.
  if (r.size() >= 278) m->name += r.Text(272, (r.size() - 272) / 6);
}

bool DecodeStaticReport(const BitReader& r, AisMessage* m, std::string* error) {
  m->part = static_cast<int>(r.U(38, 2));
  if (m->part == 0) {
    m->name = r.Text(40, 20);
    return true;
  }
  if (m->part != 1) {
    if (error) *error = "type 24 part " + std::to_string(m->part) + " is not defined";
    return false;
  }
  SetShipType(r.U(40, 8), m);
  m->call_sign = r.Text(90, 7);
  if (m->mmsi / 10000000 == 98) {
    m->mothership_mmsi = r.U(132, 30);
    m->has_mothership = m->mothership_mmsi != 0;
  } else {
    SetDimensions(r, 132, m);
  }
  return true;
}

void DecodeLongRange(const BitReader& r, AisMessage* m) {
  m->position_accurate = r.B(38);
  m->raim = r.B(39);
  SetNavStatus(r.U(40, 4), m);
  SetPosition(r, 44, 18, 62, 17, 600.0, m);
  const std::uint32_t sog = r.U(79, 6);  // whole knots
  if (sog != 63) {
    m->has_sog = true;
    m->sog_mps = sog * kKnotToMps;
  }
  const std::uint32_t cog = r.U(85, 9);  // whole degrees
  if (cog < 360) {
    m->has_cog = true;
    m->cog_deg = cog;
  }
}

/// Bits each type must carry for its fixed fields to be present.
int MinimumBits(int type, int part) {
  switch (type) {
    case 1: case 2: case 3: return 149;
    case 4: return 149;
    case 5: return 422;
    case 9: return 148;
    case 18: return 148;
    case 19: return 301;
    case 21: return 270;
    case 24: return part == 0 ? 160 : 162;
    case 27: return 94;
    default: return 0;
  }
}

}  // namespace

bool ParseAisSentence(std::string_view line, AisSentence* out, std::string* error) {
  auto fail = [error](const char* why) {
    if (error) *error = why;
    return false;
  };
  *out = AisSentence{};
  std::string_view tag_body;
  std::string_view sentence = StripTag(Trim(line), &tag_body);
  if (!tag_body.empty()) out->tag = ParseTagBlock(tag_body);
  if (!LooksLikeVdmVdo(sentence)) return fail("not a VDM or VDO sentence");

  const std::size_t star = sentence.rfind('*');
  if (star == std::string_view::npos) return fail("no checksum");
  const int sum = ReadChecksum(sentence.substr(star + 1));
  if (sum < 0) return fail("malformed checksum");
  if (sum != NmeaChecksum(sentence.data() + 1, star - 1)) return fail("checksum mismatch");

  const std::vector<std::string_view> f = SplitFields(sentence.substr(1, star - 1));
  if (f.size() < 7) return fail("too few fields");
  out->talker = std::string(f[0].substr(0, 2));
  out->own_ship = f[0][4] == 'O';

  std::int64_t count = 0, number = 0, fill = 0;
  if (!ParseUint(f[1], &count) || count < 1 || count > 9) return fail("bad fragment count");
  if (!ParseUint(f[2], &number) || number < 1 || number > count) return fail("bad fragment number");
  out->fragment_count = static_cast<int>(count);
  out->fragment_number = static_cast<int>(number);
  if (!f[3].empty()) {
    std::int64_t seq = 0;
    if (!ParseUint(f[3], &seq) || seq > 9) return fail("bad sequence id");
    out->sequence_id = static_cast<int>(seq);
  }
  out->channel = f[4].empty() ? 0 : f[4][0];
  if (f[5].empty()) return fail("empty payload");
  out->payload = std::string(f[5]);
  if (!ParseUint(f[6], &fill) || fill > 5) return fail("bad fill bits");
  out->fill_bits = static_cast<int>(fill);
  return true;
}

bool UnarmorPayload(std::string_view payload, int fill_bits, std::vector<std::uint8_t>* bits) {
  bits->clear();
  bits->reserve(payload.size() * 6);
  for (char c : payload) {
    if (c < '0' || c > 'w' || (c > 'W' && c < '`')) return false;
    int v = c - 48;
    if (v > 40) v -= 8;
    for (int b = 5; b >= 0; --b) bits->push_back(static_cast<std::uint8_t>((v >> b) & 1));
  }
  if (fill_bits < 0 || fill_bits > static_cast<int>(bits->size())) return false;
  bits->resize(bits->size() - fill_bits);
  return true;
}

bool DecodeAisBits(const std::vector<std::uint8_t>& bits, AisMessage* out, std::string* error) {
  const BitReader r(bits);
  if (r.size() < 38) {
    if (error) *error = "message shorter than its header";
    return false;
  }
  AisMessage& m = *out;
  m.type = static_cast<int>(r.U(0, 6));
  m.repeat = static_cast<int>(r.U(6, 2));
  m.mmsi = r.U(8, 30);
  const int part = m.type == 24 ? static_cast<int>(r.U(38, 2)) : -1;
  const int minimum = MinimumBits(m.type, part);
  if (minimum == 0) {
    if (error) *error = "message type " + std::to_string(m.type) + " is not decoded";
    return false;
  }
  if (r.size() < minimum) {
    if (error) *error = "type " + std::to_string(m.type) + " message has " + std::to_string(r.size()) +
                        " bits, needs " + std::to_string(minimum);
    return false;
  }
  switch (m.type) {
    case 1: case 2: case 3: DecodeClassAPosition(r, &m); break;
    case 4: m.kind = StationKind::kBaseStation; DecodeBaseStation(r, &m); break;
    case 5: DecodeStaticVoyage(r, &m); break;
    case 9: m.kind = StationKind::kSarAircraft; DecodeSarAircraft(r, &m); break;
    case 18: DecodeClassBPosition(r, &m); break;
    case 19: DecodeClassBExtended(r, &m); break;
    case 21: m.kind = StationKind::kAidToNavigation; DecodeAidToNavigation(r, &m); break;
    case 24: return DecodeStaticReport(r, &m, error);
    case 27: DecodeLongRange(r, &m); break;
  }
  return true;
}

AisDecoder::AisDecoder(double fragment_timeout_s) : timeout_s_(fragment_timeout_s) {}

void AisDecoder::Expire(double now_s) {
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (now_s - it->second.first_s > timeout_s_) {
      ++stats_.fragments_dropped;
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

FeedResult AisDecoder::Feed(std::string_view line, double now_s, AisMessage* out, std::string* error) {
  Expire(now_s);
  std::string_view tag_body;
  if (!LooksLikeVdmVdo(StripTag(Trim(line), &tag_body))) return FeedResult::kNotAis;

  AisSentence s;
  if (!ParseAisSentence(line, &s, error)) {
    ++stats_.errors;
    return FeedResult::kError;
  }

  std::string payload;
  int fill_bits = s.fill_bits;
  TagBlock tag = s.tag;
  if (s.fragment_count == 1) {
    payload = std::move(s.payload);
  } else {
    const Key key{s.talker + (s.own_ship ? "O" : "M"), s.sequence_id, s.channel, s.fragment_count};
    auto it = pending_.find(key);
    if (s.fragment_number == 1) {
      if (it != pending_.end()) {
        ++stats_.fragments_dropped;  // a new message reuses the key before the old one finished
        pending_.erase(it);
      }
      Pending p;
      p.first_s = now_s;
      p.payloads.resize(s.fragment_count);
      it = pending_.emplace(key, std::move(p)).first;
    } else if (it == pending_.end()) {
      ++stats_.fragments_dropped;
      if (error) *error = "fragment without its first part";
      return FeedResult::kError;
    }
    Pending& p = it->second;
    std::string& slot = p.payloads[s.fragment_number - 1];
    if (!slot.empty()) return FeedResult::kPending;  // repeated fragment
    slot = std::move(s.payload);
    ++p.received;
    if (s.fragment_number == s.fragment_count) p.fill_bits = s.fill_bits;
    if (!p.tag.has_source && !p.tag.has_time && !p.tag.has_text) p.tag = s.tag;
    if (p.received < s.fragment_count) return FeedResult::kPending;
    for (const std::string& part : p.payloads) payload += part;
    fill_bits = p.fill_bits;
    tag = p.tag;
    pending_.erase(it);
  }

  std::vector<std::uint8_t> bits;
  if (!UnarmorPayload(payload, fill_bits, &bits)) {
    ++stats_.errors;
    if (error) *error = "payload outside the six-bit armour set";
    return FeedResult::kError;
  }
  AisMessage m;
  std::string why;
  if (!DecodeAisBits(bits, &m, &why)) {
    if (error) *error = why;
    // Types this decoder does not read are not errors in the feed.
    if (bits.size() >= 38 && MinimumBits(m.type, -1) == 0) {
      ++stats_.unsupported;
      return FeedResult::kUnsupported;
    }
    ++stats_.errors;
    return FeedResult::kError;
  }
  m.own_ship = s.own_ship;
  m.channel = s.channel;
  m.talker = s.talker;
  m.tag = std::move(tag);
  ++stats_.messages;
  *out = std::move(m);
  return FeedResult::kMessage;
}

}  // namespace ais
}  // namespace fv
