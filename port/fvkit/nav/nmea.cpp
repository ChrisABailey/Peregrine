// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/nav/nmea.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace fv {
namespace {

// NMEA speaks knots and km/h; FvKit speaks metres per second (D2). One
// nautical mile is 1852 m exactly, which makes both of these exact.
constexpr double kMetresPerSecondPerKnot = 1852.0 / 3600.0;
constexpr double kMetresPerSecondPerKmh = 1000.0 / 3600.0;

// Two sentences belong to the same instant when their times of day agree to
// well inside one hundredth of a second — the finest an hhmmss.ss field can
// express. Not an exact compare: "205559.00" and "205559.000" are the same
// second written by two firmware versions.
constexpr double kSameEpochToleranceS = 1e-4;

// Parses a leading decimal number, the way the original's sscanf("%f") did:
// trailing rubbish after a valid number is ignored, an empty or non-numeric
// field fails. Returns false and leaves *out alone when nothing parsed.
bool ParseLeadingDouble(const std::string& text, double* out) {
  if (text.empty()) return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) return false;
  if (out != nullptr) *out = value;
  return true;
}

bool ParseLeadingInt(const std::string& text, int* out) {
  if (text.empty()) return false;
  const char* begin = text.c_str();
  char* end = nullptr;
  const long value = std::strtol(begin, &end, 10);
  if (end == begin) return false;
  if (out != nullptr) *out = static_cast<int>(value);
  return true;
}

// Fixed-width 2-digit integer out of `text` at `offset`. The original leaned
// on sscanf's "%2d"; this is that, without the locale.
bool ParseTwoDigits(const std::string& text, std::size_t offset, int* out) {
  if (text.size() < offset + 2) return false;
  const char a = text[offset];
  const char b = text[offset + 1];
  if (a < '0' || a > '9' || b < '0' || b > '9') return false;
  if (out != nullptr) *out = (a - '0') * 10 + (b - '0');
  return true;
}

// The port of NMEA_sentence::make_degrees. ddmm.mmmm (or dddmm.mmmm) plus a
// hemisphere character; how many digits of degrees to take is decided by the
// hemisphere character and by nothing else, which is the original's rule and
// the reason a longitude field with an 'N' beside it decodes as a latitude.
bool MakeDegrees(const std::string& text, char dir_char, double* out) {
  int degree_digits = 0;
  int sign = 1;
  switch (dir_char) {
    case 'N':
    case 'n':
      degree_digits = 2;
      sign = 1;
      break;
    case 'S':
    case 's':
      degree_digits = 2;
      sign = -1;
      break;
    case 'E':
    case 'e':
      degree_digits = 3;
      sign = 1;
      break;
    case 'W':
    case 'w':
      degree_digits = 3;
      sign = -1;
      break;
    default:
      return false;  // the original's -1000.0, which its caller range-rejected
  }
  if (text.size() < static_cast<std::size_t>(degree_digits)) return false;

  int degrees = 0;
  for (int i = 0; i < degree_digits; ++i) {
    const char c = text[static_cast<std::size_t>(i)];
    if (c < '0' || c > '9') return false;
    degrees = degrees * 10 + (c - '0');
  }
  double minutes = 0.0;
  if (!ParseLeadingDouble(text.substr(static_cast<std::size_t>(degree_digits)), &minutes)) {
    return false;
  }
  if (out != nullptr) *out = sign * (degrees + minutes / 60.0);
  return true;
}

// hhmmss.ss -> seconds since midnight. The original returned -1.0 on a failed
// parse and then fed that to set_time; here a failure is simply "no time",
// which is position.h's rule 1 applied to the one field the original did NOT
// give a sentinel of its own.
bool UtcFieldToSecondsOfDay(const std::string& text, double* out) {
  int hour = 0;
  int minute = 0;
  if (!ParseTwoDigits(text, 0, &hour)) return false;
  if (!ParseTwoDigits(text, 2, &minute)) return false;
  double second = 0.0;
  if (!ParseLeadingDouble(text.substr(4), &second)) return false;
  if (out != nullptr) *out = hour * 3600.0 + minute * 60.0 + second;
  return true;
}

// Days from 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// civil_from_days inverse). No timezone anywhere: NMEA is UTC by definition.
long long DaysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy =
      (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

void CivilFromDays(long long z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long yr = static_cast<long long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp + (mp < 10 ? 3 : -9);
  *y = static_cast<int>(yr + (*m <= 2));
}

// Sets speed from whichever of the two speed fields a sentence carried.
// Knots first: it is the field every talker fills, and km/h is often derived
// from it by the receiver anyway.
void SetSpeedFromKnots(double knots, PositionFix* fix) {
  fix->speed_mps = knots * kMetresPerSecondPerKnot;
  fix->has_speed = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Small public helpers
// ---------------------------------------------------------------------------

const char* NmeaTypeName(NmeaType type) {
  switch (type) {
    case NmeaType::kRmc: return "RMC";
    case NmeaType::kGga: return "GGA";
    case NmeaType::kGll: return "GLL";
    case NmeaType::kVtg: return "VTG";
    case NmeaType::kUnknown: break;
  }
  return "unknown";
}

unsigned char NmeaChecksum(const char* payload, std::size_t length) {
  unsigned char sum = 0;
  for (std::size_t i = 0; i < length; ++i) {
    sum ^= static_cast<unsigned char>(payload[i]);
  }
  return sum;
}

unsigned char NmeaChecksum(const std::string& payload) {
  return NmeaChecksum(payload.data(), payload.size());
}

int NmeaY2kYear(int year) {
  // gps.cpp's GPS_get_y2k_compliant_year, verbatim: 71..99 are 1971..1999 and
  // 00..70 are 2000..2070, on the argument that there is no GPS data before
  // 1980 and ten years of slack is enough.
  if (year < 100) {
    year += (year > 70) ? 1900 : 2000;
  }
  return year;
}

double UtcToEpochSeconds(int year, int month, int day, double seconds_of_day) {
  return static_cast<double>(DaysFromCivil(year, month, day)) * 86400.0 + seconds_of_day;
}

void EpochSecondsToUtc(double epoch_seconds, int* year, int* month, int* day,
                       double* seconds_of_day) {
  const double days_float = std::floor(epoch_seconds / 86400.0);
  unsigned m = 0;
  unsigned d = 0;
  int y = 0;
  CivilFromDays(static_cast<long long>(days_float), &y, &m, &d);
  if (year != nullptr) *year = y;
  if (month != nullptr) *month = static_cast<int>(m);
  if (day != nullptr) *day = static_cast<int>(d);
  if (seconds_of_day != nullptr) *seconds_of_day = epoch_seconds - days_float * 86400.0;
}

bool NmeaSentenceLooksValid(const std::string& line) {
  // All NMEA sentences start with '$'.
  if (line.empty() || line[0] != '$') return false;

  // 82 characters including the '$' and the <CR><LF>. The transport has
  // already stripped the terminator, so this is two characters more generous
  // than the original was — the only direction in which that matters is
  // accepting a sentence the original would have called too long by <= 2.
  if (line.size() > kMaxNmeaSentenceLength) return false;

  // Test the checksum only when one is present, which is the original's rule.
  const std::size_t star = line.find('*');
  if (star == std::string::npos) return true;
  if (line.size() < star + 3) return false;

  unsigned sent = 0;
  if (std::sscanf(line.c_str() + star + 1, "%2x", &sent) != 1) return false;
  const unsigned char calculated = NmeaChecksum(line.data() + 1, star - 1);
  return calculated == static_cast<unsigned char>(sent);
}

std::vector<std::string> SplitNmeaFields(const std::string& line) {
  std::vector<std::string> fields;
  if (line.empty()) return fields;

  std::size_t begin = (line[0] == '$') ? 1 : 0;
  // The checksum is not a field: field_parse stopped at the '*' too.
  std::size_t end = line.find('*');
  if (end == std::string::npos) end = line.size();

  while (begin <= end) {
    const std::size_t comma = line.find(',', begin);
    const std::size_t stop = (comma == std::string::npos || comma > end) ? end : comma;
    fields.push_back(line.substr(begin, stop - begin));
    if (stop == end) break;
    begin = stop + 1;
  }
  return fields;
}

NmeaType NmeaTypeOf(const std::string& line, std::string* talker) {
  // "$" + 2-char talker + 3-char sentence id. Any talker: see the header —
  // the original demanded "GP" and a modern receiver says "GN".
  if (line.size() < 6 || line[0] != '$') return NmeaType::kUnknown;
  const std::string id = line.substr(3, 3);
  NmeaType type = NmeaType::kUnknown;
  if (id == "RMC") {
    type = NmeaType::kRmc;
  } else if (id == "GGA") {
    type = NmeaType::kGga;
  } else if (id == "GLL") {
    type = NmeaType::kGll;
  } else if (id == "VTG") {
    type = NmeaType::kVtg;
  } else {
    return NmeaType::kUnknown;
  }
  if (talker != nullptr) *talker = line.substr(1, 2);
  return type;
}

// ---------------------------------------------------------------------------
// ParseNmeaSentence
// ---------------------------------------------------------------------------

namespace {

// The field indices below are the original's, shifted by one because
// SplitNmeaFields keeps the sentence id as field 0 while field_parse started
// after it. `f[n]` here is `fields[n-1]` there.
bool ParseRmc(const std::vector<std::string>& f, NmeaReading* out) {
  // Q1: 11 fields after the id, or the sentence is rejected whole.
  if (f.size() < 12) return false;
  if (!f[2].empty() && f[2][0] == 'V') return false;  // location invalid flag
  if (f[3].empty() || f[4].empty() || f[5].empty() || f[6].empty()) return false;

  double lat = 0.0;
  double lon = 0.0;
  if (!MakeDegrees(f[3], f[4][0], &lat) || lat < -90.0 || lat > 90.0) return false;
  if (!MakeDegrees(f[5], f[6][0], &lon) || lon < -180.0 || lon > 180.0) return false;
  out->fix.SetPosition(lat, lon);

  double knots = 0.0;
  if (ParseLeadingDouble(f[7], &knots)) SetSpeedFromKnots(knots, &out->fix);

  double course = 0.0;
  if (ParseLeadingDouble(f[8], &course)) {
    out->fix.true_heading_deg = course;
    out->fix.has_true_heading = true;

    // Q3: the magnetic heading is DERIVED from the variation, and only when
    // there is a true heading to derive it from. The wrap is the original's:
    // one add or subtract of 360, no modulo.
    double variation = 0.0;
    if (!f[10].empty() && !f[11].empty() && ParseLeadingDouble(f[10], &variation)) {
      double magnetic = 0.0;
      if (f[11][0] == 'E' || f[11][0] == 'e') {
        magnetic = course - variation;
        if (magnetic < 0.0) magnetic += 360.0;
      } else {
        magnetic = course + variation;
        if (magnetic > 360.0) magnetic -= 360.0;
      }
      out->fix.magnetic_heading_deg = magnetic;
      out->fix.has_magnetic_heading = true;
    }
  }

  double seconds = 0.0;
  if (UtcFieldToSecondsOfDay(f[1], &seconds)) {
    out->has_time_of_day = true;
    out->time_of_day_s = seconds;
  }

  // ddmmyy — the original's "%02d%02d%02d" into day, month, year.
  int day = 0;
  int month = 0;
  int year = 0;
  if (ParseTwoDigits(f[9], 0, &day) && ParseTwoDigits(f[9], 2, &month) &&
      ParseTwoDigits(f[9], 4, &year)) {
    out->has_date = true;
    out->day = day;
    out->month = month;
    out->year = NmeaY2kYear(year);
  }
  return true;
}

bool ParseGga(const std::vector<std::string>& f, NmeaReading* out) {
  if (f.size() < 15) return false;                    // Q1: 14 fields after the id
  if (!f[6].empty() && f[6][0] == '0') return false;  // fix quality 0 = no fix
  if (f[2].empty() || f[3].empty() || f[4].empty() || f[5].empty()) return false;

  double lat = 0.0;
  double lon = 0.0;
  if (!MakeDegrees(f[2], f[3][0], &lat) || lat < -90.0 || lat > 90.0) return false;
  if (!MakeDegrees(f[4], f[5][0], &lon) || lon < -180.0 || lon > 180.0) return false;
  out->fix.SetPosition(lat, lon);

  double seconds = 0.0;
  if (UtcFieldToSecondsOfDay(f[1], &seconds)) {
    out->has_time_of_day = true;
    out->time_of_day_s = seconds;
  }

  int satellites = 0;
  if (ParseLeadingInt(f[7], &satellites)) {
    out->fix.satellite_count = satellites;
    out->fix.has_satellite_count = true;
  }

  double hdop = 0.0;
  if (ParseLeadingDouble(f[8], &hdop)) {
    out->fix.hdop = hdop;
    out->fix.has_hdop = true;
  }

  // Q2: altitude only with MORE THAN THREE satellites, and only when the unit
  // character really says metres. An empty satellite field therefore yields no
  // altitude, which is the original's behaviour and not an oversight here.
  double altitude = 0.0;
  if (satellites > 3 && !f[10].empty() && f[10][0] == 'M' &&
      ParseLeadingDouble(f[9], &altitude)) {
    out->fix.altitude_msl_m = altitude;
    out->fix.has_altitude = true;
  }

  double geoid = 0.0;
  if (!f[12].empty() && f[12][0] == 'M' && ParseLeadingDouble(f[11], &geoid)) {
    out->has_geoid_separation = true;
    out->geoid_separation_m = geoid;
  }
  return true;
}

bool ParseGll(const std::vector<std::string>& f, NmeaReading* out) {
  if (f.size() < 7) return false;                     // Q1: 6 fields after the id
  if (!f[6].empty() && f[6][0] == 'V') return false;  // status
  if (f[1].empty() || f[2].empty() || f[3].empty() || f[4].empty()) return false;

  double lat = 0.0;
  double lon = 0.0;
  if (!MakeDegrees(f[1], f[2][0], &lat) || lat < -90.0 || lat > 90.0) return false;
  if (!MakeDegrees(f[3], f[4][0], &lon) || lon < -180.0 || lon > 180.0) return false;
  out->fix.SetPosition(lat, lon);

  double seconds = 0.0;
  if (UtcFieldToSecondsOfDay(f[5], &seconds)) {
    out->has_time_of_day = true;
    out->time_of_day_s = seconds;
  }
  return true;
}

bool ParseVtg(const std::vector<std::string>& f, NmeaReading* out) {
  if (f.size() < 9) return false;  // Q1: 8 fields after the id
  bool any = false;

  double course = 0.0;
  if (!f[2].empty() && f[2][0] == 'T' && ParseLeadingDouble(f[1], &course)) {
    out->fix.true_heading_deg = course;
    out->fix.has_true_heading = true;
    any = true;
  }
  double magnetic = 0.0;
  if (!f[4].empty() && f[4][0] == 'M' && ParseLeadingDouble(f[3], &magnetic)) {
    out->fix.magnetic_heading_deg = magnetic;
    out->fix.has_magnetic_heading = true;
    any = true;
  }
  // Knots wins over km/h when both are present: it is the field every talker
  // fills, and the other is usually derived from it inside the receiver.
  double knots = 0.0;
  if (!f[6].empty() && f[6][0] == 'N' && ParseLeadingDouble(f[5], &knots)) {
    SetSpeedFromKnots(knots, &out->fix);
    any = true;
  }
  double kmh = 0.0;
  if (!f[8].empty() && f[8][0] == 'K' && ParseLeadingDouble(f[7], &kmh)) {
    if (!out->fix.has_speed) {
      out->fix.speed_mps = kmh * kMetresPerSecondPerKmh;
      out->fix.has_speed = true;
    }
    any = true;
  }
  return any;  // "no valid speed or heading" is the original's FALSE
}

}  // namespace

bool ParseNmeaSentence(const std::string& line, NmeaReading* out) {
  if (out == nullptr) return false;
  *out = NmeaReading{};

  const NmeaType type = NmeaTypeOf(line, &out->talker);
  if (type == NmeaType::kUnknown) return false;
  if (!NmeaSentenceLooksValid(line)) return false;

  const std::vector<std::string> fields = SplitNmeaFields(line);
  out->type = type;
  switch (type) {
    case NmeaType::kRmc: return ParseRmc(fields, out);
    case NmeaType::kGga: return ParseGga(fields, out);
    case NmeaType::kGll: return ParseGll(fields, out);
    case NmeaType::kVtg: return ParseVtg(fields, out);
    case NmeaType::kUnknown: break;
  }
  return false;
}

// ---------------------------------------------------------------------------
// NmeaFixAssembler
// ---------------------------------------------------------------------------

void NmeaFixAssembler::SetDateHint(int year, int month, int day) {
  year_ = NmeaY2kYear(year);
  month_ = month;
  day_ = day;
  has_date_ = true;
}

void NmeaFixAssembler::Reset() {
  pending_ = PositionFix{};
  has_pending_ = false;
  pending_emitted_ = false;
  pending_has_time_of_day_ = false;
  pending_time_of_day_s_ = 0.0;
}

void NmeaFixAssembler::StampTime(PositionFix* fix) const {
  if (!pending_has_time_of_day_ || !has_date_) return;
  fix->time_s = UtcToEpochSeconds(year_, month_, day_, pending_time_of_day_s_);
  fix->has_time = true;
}

void NmeaFixAssembler::StartGroup(const NmeaReading& reading) {
  pending_ = reading.fix;
  has_pending_ = true;
  pending_emitted_ = false;
  pending_has_time_of_day_ = reading.has_time_of_day;
  pending_time_of_day_s_ = reading.time_of_day_s;
}

void NmeaFixAssembler::MergeReading(const NmeaReading& reading) {
  pending_.Merge(reading.fix);
  if (reading.has_time_of_day && !pending_has_time_of_day_) {
    pending_has_time_of_day_ = true;
    pending_time_of_day_s_ = reading.time_of_day_s;
  }
}

bool NmeaFixAssembler::AddLine(const std::string& line, PositionFix* out) {
  ++lines_seen_;
  NmeaReading reading;
  if (!ParseNmeaSentence(line, &reading)) {
    ++sentences_rejected_;
    return false;
  }
  ++sentences_parsed_;

  // An RMC's date is the stream's date from here on.
  if (reading.has_date) {
    year_ = reading.year;
    month_ = reading.month;
    day_ = reading.day;
    has_date_ = true;
  }

  // Does this sentence belong to the group being assembled?
  const bool same_epoch =
      has_pending_ && (!reading.has_time_of_day || !pending_has_time_of_day_ ||
                       std::fabs(reading.time_of_day_s - pending_time_of_day_s_) <
                           kSameEpochToleranceS);

  bool emitted = false;
  if (!same_epoch) {
    // The group is over: the previous epoch is complete and this sentence
    // begins the next one. `pending_emitted_` is what keeps this from
    // delivering the same instant twice in per-sentence mode, where the group
    // has already gone out.
    if (has_pending_ && pending_.has_position && !pending_emitted_) {
      PositionFix closed = pending_;
      StampTime(&closed);
      if (out != nullptr) *out = closed;
      ++fixes_emitted_;
      emitted = true;
    }
    StartGroup(reading);
  } else {
    MergeReading(reading);
  }

  if (emit_per_sentence_ && !emitted && pending_.has_position) {
    PositionFix now = pending_;
    StampTime(&now);
    if (out != nullptr) *out = now;
    ++fixes_emitted_;
    pending_emitted_ = true;
    emitted = true;
  }
  return emitted;
}

bool NmeaFixAssembler::Flush(PositionFix* out) {
  if (!has_pending_ || !pending_.has_position || pending_emitted_) {
    Reset();
    return false;
  }
  PositionFix closed = pending_;
  StampTime(&closed);
  if (out != nullptr) *out = closed;
  ++fixes_emitted_;
  Reset();
  return true;
}

// ---------------------------------------------------------------------------
// NmeaLineSource
// ---------------------------------------------------------------------------

NmeaLineSource::NmeaLineSource(std::shared_ptr<ILineTransport> transport)
    : transport_(std::move(transport)) {}

void NmeaLineSource::SetMaxLinesPerPoll(std::size_t lines) {
  max_lines_per_poll_ = lines != 0 ? lines : 1;
}

Status NmeaLineSource::Start() {
  if (transport_ == nullptr) {
    error_ = Status::Error(kInvalidArg, "NmeaLineSource: no transport");
    return error_;
  }
  if (running_) return Status::Ok();
  const Status opened = transport_->Open();
  if (!opened.ok()) {
    error_ = opened;
    return opened;
  }
  assembler_.Reset();
  lines_read_ = 0;
  fixes_emitted_ = 0;
  at_end_ = false;
  error_ = Status::Ok();
  running_ = true;
  return Status::Ok();
}

void NmeaLineSource::Stop() {
  if (transport_ != nullptr) transport_->Close();
  // Deliberately NOT flushed: a stop is a user closing the feed, and the
  // pending group is a fix the receiver never finished sending.
  assembler_.Reset();
  running_ = false;
}

std::size_t NmeaLineSource::Poll() {
  if (!running_ || transport_ == nullptr) return 0;

  std::size_t emitted = 0;
  std::string line;
  for (std::size_t i = 0; i < max_lines_per_poll_; ++i) {
    const LineResult result = transport_->ReadLine(&line);
    if (result == LineResult::kLine) {
      ++lines_read_;
      PositionFix fix;
      if (assembler_.AddLine(line, &fix)) {
        ++fixes_emitted_;
        ++emitted;
        Emit(fix);
        if (!running_) return emitted;  // a listener stopped us
      }
      continue;
    }
    if (result == LineResult::kAgain) break;
    if (result == LineResult::kEnd) {
      at_end_ = true;
      PositionFix fix;
      if (assembler_.Flush(&fix)) {
        ++fixes_emitted_;
        ++emitted;
        Emit(fix);
      }
      running_ = false;
      break;
    }
    // kError
    error_ = transport_->error();
    running_ = false;
    break;
  }
  return emitted;
}

Status ReadNmeaLog(const std::string& path, std::vector<PositionFix>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "ReadNmeaLog: null out");
  out->clear();

  auto transport = std::make_shared<FileLineTransport>(path);
  const Status opened = transport->Open();
  if (!opened.ok()) return opened;

  NmeaLineSource source(transport);
  source.SetMaxLinesPerPoll(1 << 20);
  source.SetListener([out](const PositionFix& fix) { out->push_back(fix); });
  const Status started = source.Start();
  if (!started.ok()) return started;
  while (source.running()) {
    const std::size_t before = source.lines_read();
    source.Poll();
    // Poll consumes lines until the transport says kEnd (which stops the
    // source) or kAgain. A plain file never says kAgain, so no progress means
    // there is nothing further to read.
    if (source.lines_read() == before) break;
  }
  source.Stop();
  return source.error();
}

// ---------------------------------------------------------------------------
// Building sentences
// ---------------------------------------------------------------------------

namespace {

// Degrees -> the ddmm.mmm / dddmm.mmm halves NMEA writes, plus the hemisphere
// character. Same decomposition as the original's build_* functions.
void SplitDegrees(double value, bool is_latitude, int* degrees, double* minutes, char* dir) {
  const bool negative = value < 0.0;
  const double magnitude = negative ? -value : value;
  *degrees = static_cast<int>(magnitude);
  *minutes = (magnitude - *degrees) * 60.0;
  if (is_latitude) {
    *dir = negative ? 'S' : 'N';
  } else {
    *dir = negative ? 'W' : 'E';
  }
}

// Joins the sentence id and its fields with commas, then appends the "*hh"
// checksum and the <CR><LF>.
//
// The fields are passed as a LIST rather than assembled by string appends
// because their COUNT is the thing Q1 rejects a sentence over: written this
// way, "RMC has 11 fields" is something a reader can count in the source.
//
// The port zero-pads the checksum everywhere; see the header for why the
// original's space-padded "%2hX" in build_RMC/build_VTG is not reproduced.
std::string Assemble(const char* sentence_id, const std::vector<std::string>& fields) {
  std::string body = sentence_id;
  for (const std::string& field : fields) {
    body += ",";
    body += field;
  }
  char tail[8];
  std::snprintf(tail, sizeof(tail), "*%02X\r\n", NmeaChecksum(body.data(), body.size()));
  return "$" + body + tail;
}

std::string TwoDecimals(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%0.2f", value);
  return buffer;
}

bool PositionIsSendable(const PositionFix& fix) {
  return fix.has_position && fix.lat >= -90.0 && fix.lat <= 90.0 && fix.lon >= -180.0 &&
         fix.lon <= 180.0;
}

// The four fields NMEA writes a position as: ddmm.mmm, hemisphere,
// dddmm.mmm, hemisphere.
void AppendLatLonFields(const PositionFix& fix, std::vector<std::string>* fields) {
  int deg_lat = 0;
  int deg_lon = 0;
  double min_lat = 0.0;
  double min_lon = 0.0;
  char dir_lat = 'N';
  char dir_lon = 'E';
  SplitDegrees(fix.lat, true, &deg_lat, &min_lat, &dir_lat);
  SplitDegrees(fix.lon, false, &deg_lon, &min_lon, &dir_lon);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02d%06.3f", deg_lat, min_lat);
  fields->push_back(buffer);
  fields->push_back(std::string(1, dir_lat));
  std::snprintf(buffer, sizeof(buffer), "%03d%06.3f", deg_lon, min_lon);
  fields->push_back(buffer);
  fields->push_back(std::string(1, dir_lon));
}

// Epoch seconds -> the calendar parts a sentence needs.
struct CalendarTime {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  double seconds_of_day = 0.0;
};

CalendarTime CalendarFromEpoch(double time_s) {
  CalendarTime out;
  const double days_float = std::floor(time_s / 86400.0);
  const long long days = static_cast<long long>(days_float);
  out.seconds_of_day = time_s - days_float * 86400.0;
  CivilFromDays(days, &out.year, &out.month, &out.day);
  return out;
}

}  // namespace

std::string NmeaTimeOfDayString(double seconds_of_day) {
  const int hour = static_cast<int>(seconds_of_day / 3600.0);
  const int minute = static_cast<int>((seconds_of_day - hour * 3600.0) / 60.0);
  const double second = seconds_of_day - (hour * 3600.0 + minute * 60.0);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02d%02d%05.2f", hour, minute, second);
  return buffer;
}

std::string BuildRmc(const PositionFix& fix) {
  if (!PositionIsSendable(fix)) return std::string();

  CalendarTime calendar;
  std::vector<std::string> fields;  // 11 of them, and Q1 rejects 10
  fields.reserve(11);

  if (fix.has_time) {
    calendar = CalendarFromEpoch(fix.time_s);
    fields.push_back(NmeaTimeOfDayString(calendar.seconds_of_day));
  } else {
    fields.emplace_back();
  }
  fields.emplace_back("A");  // the original writes the valid flag unconditionally
  AppendLatLonFields(fix, &fields);
  fields.push_back(fix.has_speed ? TwoDecimals(fix.speed_mps / kMetresPerSecondPerKnot)
                                 : std::string());
  fields.push_back(fix.has_true_heading ? TwoDecimals(fix.true_heading_deg) : std::string());
  if (fix.has_time) {
    char date[16];
    std::snprintf(date, sizeof(date), "%02u%02u%02d", calendar.day, calendar.month,
                  calendar.year % 100);
    fields.push_back(date);
  } else {
    fields.emplace_back();
  }

  // The inverse of Q3: M - T < 0 is an easterly variation, > 0 a westerly one.
  if (fix.has_true_heading && fix.has_magnetic_heading) {
    double variation = fix.magnetic_heading_deg - fix.true_heading_deg;
    if (variation < -180.0) {
      variation += 360.0;
    } else if (variation > 180.0) {
      variation -= 360.0;
    }
    fields.push_back(TwoDecimals(variation < 0.0 ? -variation : variation));
    fields.emplace_back(variation < 0.0 ? "E" : "W");
  } else {
    fields.emplace_back();
    fields.emplace_back();
  }
  return Assemble("GPRMC", fields);
}

std::string BuildGga(const PositionFix& fix) {
  if (!PositionIsSendable(fix)) return std::string();

  std::vector<std::string> fields;  // 14 of them
  fields.reserve(14);
  fields.push_back(fix.has_time
                       ? NmeaTimeOfDayString(CalendarFromEpoch(fix.time_s).seconds_of_day)
                       : std::string());
  AppendLatLonFields(fix, &fields);
  fields.emplace_back("1");  // fix quality: GPS

  // The original set m_satellites = 4 unconditionally here, its comment being
  // "gotta have 4 satellites for GPS file reader to pay attention to
  // altitude" — that is Q2 seen from the writing side. The port reports what
  // the fix actually carries, and only floors it at 4 when there is an
  // altitude that would otherwise be unreadable on the way back in.
  int satellites = fix.has_satellite_count ? fix.satellite_count : 4;
  if (fix.has_altitude && satellites < 4) satellites = 4;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02d", satellites);
  fields.push_back(buffer);

  if (fix.has_hdop) {
    std::snprintf(buffer, sizeof(buffer), "%.1f", fix.hdop);
    fields.push_back(buffer);
  } else {
    fields.emplace_back();
  }
  if (fix.has_altitude) {
    std::snprintf(buffer, sizeof(buffer), "%0.1f", fix.altitude_msl_m);
    fields.push_back(buffer);
    fields.emplace_back("M");
  } else {
    fields.emplace_back();
    fields.emplace_back();
  }
  // Geoid separation and its unit: a PositionFix does not carry one (its
  // altitude is already MSL), so both go out empty. Then the two DGPS fields.
  fields.emplace_back();
  fields.emplace_back();
  fields.emplace_back();
  fields.emplace_back();
  return Assemble("GPGGA", fields);
}

std::string BuildVtg(const PositionFix& fix) {
  if (!fix.has_speed && !fix.has_true_heading && !fix.has_magnetic_heading) {
    return std::string();
  }
  std::vector<std::string> fields;  // 8 of them
  fields.reserve(8);
  if (fix.has_true_heading) {
    fields.push_back(TwoDecimals(fix.true_heading_deg));
    fields.emplace_back("T");
  } else {
    fields.emplace_back();
    fields.emplace_back();
  }
  if (fix.has_magnetic_heading) {
    fields.push_back(TwoDecimals(fix.magnetic_heading_deg));
    fields.emplace_back("M");
  } else {
    fields.emplace_back();
    fields.emplace_back();
  }
  if (fix.has_speed) {
    fields.push_back(TwoDecimals(fix.speed_mps / kMetresPerSecondPerKnot));
    fields.emplace_back("N");
    fields.push_back(TwoDecimals(fix.speed_mps / kMetresPerSecondPerKmh));
    fields.emplace_back("K");
  } else {
    fields.emplace_back();
    fields.emplace_back();
    fields.emplace_back();
    fields.emplace_back();
  }
  return Assemble("GPVTG", fields);
}

}  // namespace fv
