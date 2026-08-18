// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/nmea.h — NMEA 0183 in and out (nav plan MM6).
//
// Ported from Applications/FalconView/MovingMapOverlay/nmea.cpp (the
// `NMEA_sentence` class; NetNMEA/nmea.cpp is the same code with an extra ALM
// branch and an `#if OLD_GPS_TIME_DATA` fork). The parse rules below are that
// file's, field index for field index. What changed, and why:
//
//   - CString/COleDateTime/sscanf_s -> std::string, epoch seconds, std::from_chars
//     style parsing. The class parsed IN PLACE through a `char* m_buffer`
//     member, which is what made it non-reentrant; here a sentence is a
//     string_view and the parse has no state.
//
//   - THE -1000.0 SENTINELS DO NOT PORT. position.h's rule 1: every field of
//     a PositionFix carries its own validity, so "no heading" is
//     `has_true_heading == false` and not -1.0. This is the whole reason the
//     partial-sentence problem (GLL has no speed, VTG has no position) is
//     expressible at all.
//
//   - ANY TALKER IS ACCEPTED, not just `$GP`. The original does
//     `strncmp(s, "$GPRMC", 6)`, which was right in 1994 and rejects every
//     sentence a modern receiver sends: a multi-constellation phone talks
//     `$GNRMC`, GLONASS `$GLRMC`, Galileo `$GARMC`. Since MM6 exists so that a
//     phone can drive the map, matching `$??RMC` is the point rather than a
//     liberty. `talker` is reported, so a caller that wants the old behaviour
//     can still filter on "GP".
//
// THREE ORIGINAL QUIRKS ARE PRESERVED ON PURPOSE (bit-faithful rule):
//
//   Q1. A SENTENCE WITH TOO FEW FIELDS IS REJECTED WHOLE. RMC needs 11 fields,
//       GGA 14, GLL 6, VTG 8, counted after the sentence id. A receiver that
//       truncates a trailing empty field therefore loses the sentence rather
//       than delivering the position it did send. Kept because a "repaired"
//       short sentence is a guess about which field is missing.
//
//   Q2. GGA ALTITUDE IS ONLY READ WITH MORE THAN 3 SATELLITES. The original's
//       comment is "gotta have 4 satellites"; a 3-satellite fix's altitude is
//       genuinely a 2D solution's fiction, so this one is defensible as well
//       as faithful — but it does mean a GGA whose satellite field is empty
//       never yields an altitude.
//
//   Q3. RMC's MAGNETIC HEADING IS DERIVED FROM THE VARIATION, NOT REPORTED.
//       true - variation for an easterly variation, true + for a westerly,
//       wrapped by a single add or subtract of 360 (so a variation over 360
//       stays out of range — the original's arithmetic, unchanged).
//
// The BUILD side is kept, per the plan, for the recorder that does not exist
// yet. One deliberate deviation there is documented at BuildRmc.

#ifndef FVKIT_NAV_NMEA_H_
#define FVKIT_NAV_NMEA_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/line_transport.h"
#include "fvkit/nav/position.h"

namespace fv {

// NMEA 0183 caps a sentence at 82 characters including the leading '$' and the
// <CR><LF>. The original's MAX_NMEA_SENTENCE_LENGTH, same number.
constexpr std::size_t kMaxNmeaSentenceLength = 82;

// The four sentences FalconView reads. NetNMEA also parsed ALM (almanac),
// which carries no position and no time-of-fix and so has nothing to give a
// PositionFix; it is deliberately not ported.
enum class NmeaType {
  kUnknown,
  kRmc,  // position, time, date, speed, course, magnetic variation
  kGga,  // position, time, altitude, satellites, HDOP, geoid separation
  kGll,  // position, time
  kVtg,  // course and speed only — no position at all
};

const char* NmeaTypeName(NmeaType type);

// ---------------------------------------------------------------------------
// One sentence
// ---------------------------------------------------------------------------

// What a single sentence said.
//
// TIME IS SPLIT IN TWO because NMEA splits it in two: every sentence but VTG
// carries a time of DAY, and only RMC carries the date that turns it into an
// instant. So a single-sentence parse reports the two halves HERE and leaves
// `fix.time_s` alone — even for an RMC, which carries both — because stamping
// a fix is NmeaFixAssembler's job and it is the only thing that knows the date
// a GGA-only receiver never states.
struct NmeaReading {
  NmeaType type = NmeaType::kUnknown;
  std::string talker;  // "GP", "GN", "GL", "GA", … — the two chars after '$'

  // Every field the sentence carried, in FvKit units (metres, m/s, degrees).
  PositionFix fix;

  bool has_time_of_day = false;
  double time_of_day_s = 0.0;  // seconds since 00:00:00 UTC

  bool has_date = false;
  int year = 0;  // 4-digit, y2k-expanded exactly as the original did
  int month = 0;
  int day = 0;

  // GGA's height of the geoid above WGS-84, metres. Reported and NOT applied:
  // the altitude in `fix` is already the MSL height the receiver computed, and
  // subtracting the separation would give the ellipsoidal height under a name
  // that says otherwise.
  bool has_geoid_separation = false;
  double geoid_separation_m = 0.0;
};

// XOR of every byte — the NMEA checksum. `payload` is what lies BETWEEN the
// '$' and the '*', both excluded.
//
// NOTE: the original stores this in a `char` and compares it to a `short` read
// with %hX, which would mis-compare for any value with the high bit set. It
// cannot happen: an NMEA payload is 7-bit ASCII, so the XOR is always <= 0x7F.
unsigned char NmeaChecksum(const char* payload, std::size_t length);
unsigned char NmeaChecksum(const std::string& payload);

// The port of `NMEA_sentence::NMEA_test`: starts with '$', no longer than 82,
// and — only if a '*' is present — the checksum matches. A sentence with no
// checksum passes, which is the original's rule and a real receiver's habit.
bool NmeaSentenceLooksValid(const std::string& line);

// Splits `line` into its comma-separated fields. `fields[0]` is the sentence
// id without the '$' ("GPRMC"); the checksum and its '*' are not a field.
// Empty fields are preserved as empty strings, which is the whole point: an
// NMEA field's position is its meaning.
std::vector<std::string> SplitNmeaFields(const std::string& line);

// The sentence id, talker-agnostic. `talker` may be null.
NmeaType NmeaTypeOf(const std::string& line, std::string* talker = nullptr);

// Parses one sentence. Returns false when the sentence is not one of the four,
// fails NmeaSentenceLooksValid, or fails its own validity rules (a location
// flag saying "invalid", a lat/lon out of range, too few fields — Q1) — which
// is exactly what `process_RMC` and friends returned FALSE for.
//
// On false, *out is left in whatever state the parse reached; treat it as
// undefined. On true, every `has_*` in `out->fix` says what was actually there.
bool ParseNmeaSentence(const std::string& line, NmeaReading* out);

// The original's 2-digit year rule, verbatim (gps.cpp's
// GPS_get_y2k_compliant_year): 71..99 are 1971..1999, 00..70 are 2000..2070.
// A year already >= 100 is returned unchanged.
int NmeaY2kYear(int year);

// Epoch seconds from a UTC calendar date and a time of day, and back. A plain
// proleptic-Gregorian day count — no timezone database, because NMEA is
// always UTC and so (rule 1 of gpx.h) is GPX. The pair lives here because the
// NMEA parse is what first needed it; `fvkit/nav/gpx.h` uses the same two
// rather than growing a second calendar.
double UtcToEpochSeconds(int year, int month, int day, double seconds_of_day);
void EpochSecondsToUtc(double epoch_seconds, int* year, int* month, int* day,
                       double* seconds_of_day);

// ---------------------------------------------------------------------------
// A stream of sentences
// ---------------------------------------------------------------------------

// Turns lines into fixes.
//
// A receiver spreads one instant across several sentences — the GGA has the
// altitude and the satellites, the RMC of the same second has the speed and
// the course — so the assembler MERGES sentences sharing a time of day
// (PositionFix::Merge, which is why MM1 built it) and emits the merged fix
// when the epoch changes.
//
// THAT COSTS ONE EPOCH OF LATENCY, because a group is only known to be over
// when the next one starts. At 1 Hz that is a second, which a live moving map
// feels. `SetEmitPerSentence(true)` is the way out: every sentence that
// carries a position emits immediately, still merged with whatever the
// sentences before it in the same epoch contributed. A recorded log wants the
// default (one fix per second, complete); a live feed wants the other.
//
// The DATE comes from the last RMC seen, or from SetDateHint. Until one of
// those has happened, fixes have `has_time` false rather than a made-up 1970
// stamp — a GGA-only receiver genuinely does not say what day it is.
class NmeaFixAssembler {
 public:
  NmeaFixAssembler() = default;

  // Feeds one line. Returns true when *out received a fix.
  bool AddLine(const std::string& line, PositionFix* out);

  // Emits whatever is pending, if it has a position. Call at end of stream.
  bool Flush(PositionFix* out);

  // Throws away the pending group without emitting it.
  void Reset();

  void SetEmitPerSentence(bool per_sentence) { emit_per_sentence_ = per_sentence; }
  bool emit_per_sentence() const { return emit_per_sentence_; }

  // The date to stamp fixes with until an RMC supplies one. Month is 1-12,
  // day 1-31; a 2-digit year goes through NmeaY2kYear.
  void SetDateHint(int year, int month, int day);
  bool has_date() const { return has_date_; }

  // Counters, so a shell can say "receiving, but nothing parses" — the single
  // most common thing to be wrong about a feed.
  std::size_t lines_seen() const { return lines_seen_; }
  std::size_t sentences_parsed() const { return sentences_parsed_; }
  std::size_t sentences_rejected() const { return sentences_rejected_; }
  std::size_t fixes_emitted() const { return fixes_emitted_; }

 private:
  void StartGroup(const NmeaReading& reading);
  void MergeReading(const NmeaReading& reading);
  void StampTime(PositionFix* fix) const;

  PositionFix pending_;
  bool has_pending_ = false;
  // The pending group has already been delivered (per-sentence mode). Closing
  // the epoch, and Flush, must not deliver the same instant a second time.
  bool pending_emitted_ = false;
  bool pending_has_time_of_day_ = false;
  double pending_time_of_day_s_ = 0.0;

  bool emit_per_sentence_ = false;

  bool has_date_ = false;
  int year_ = 0;
  int month_ = 0;
  int day_ = 0;

  std::size_t lines_seen_ = 0;
  std::size_t sentences_parsed_ = 0;
  std::size_t sentences_rejected_ = 0;
  std::size_t fixes_emitted_ = 0;
};

// A live NMEA feed: an ILineTransport in, PositionFixes out.
//
// It has NO THREAD, exactly like ScriptedSource and for the same reason — the
// transport never blocks, so `Poll()` from the shell's existing tick is the
// whole loop and a test drives a whole stream with no sleeping. A shell that
// would rather have a thread wires one to a FixQueue (position.h), which is
// what that class is for.
//
// `max_lines_per_poll` bounds one tick's work: a file transport handed a
// 3-hour recording would otherwise replay the lot inside a single frame.
class NmeaLineSource : public PositionSourceBase {
 public:
  explicit NmeaLineSource(std::shared_ptr<ILineTransport> transport);

  // Opens the transport. Fails with its Status if it will not open.
  Status Start() override;
  void Stop() override;

  // Reads what has arrived and emits the fixes it made. Returns the number
  // emitted. On end of stream it flushes the pending group once, so the last
  // second of a recording is not lost, and `at_end()` then answers true.
  std::size_t Poll();

  bool at_end() const { return at_end_; }

  void SetMaxLinesPerPoll(std::size_t lines);
  std::size_t max_lines_per_poll() const { return max_lines_per_poll_; }

  NmeaFixAssembler& assembler() { return assembler_; }
  const NmeaFixAssembler& assembler() const { return assembler_; }
  const std::shared_ptr<ILineTransport>& transport() const { return transport_; }

  std::size_t lines_read() const { return lines_read_; }
  std::size_t fixes_emitted() const { return fixes_emitted_; }

  // Set when the transport reported an error. The source stops on one.
  const Status& error() const { return error_; }

 private:
  std::shared_ptr<ILineTransport> transport_;
  NmeaFixAssembler assembler_;
  std::size_t max_lines_per_poll_ = 256;
  std::size_t lines_read_ = 0;
  std::size_t fixes_emitted_ = 0;
  bool at_end_ = false;
  Status error_;
};

// Reads a whole recorded log into fixes.
//
// This is the OTHER half of "playback of a recorded log", and the half a
// replay actually wants: the fixes carry their own timestamps, so
// BuildScriptedTrackFromFixes (scripted_source.h) turns them into a script
// that plays at the speed it was recorded at. Same shape as ReadGpxFile — the
// two recorded-track readers deliberately arrive at the same seam.
//
// kNotFound if the file will not open. A file that opens and yields no fix is
// kOk with an empty vector: an empty log is data, not an error.
Status ReadNmeaLog(const std::string& path, std::vector<PositionFix>* out);

// ---------------------------------------------------------------------------
// Building sentences (for the recorder that does not exist yet)
// ---------------------------------------------------------------------------

// Each returns the complete sentence including '$', the '*hh' checksum and the
// <CR><LF>, or an empty string when the fix carries nothing to say: no valid
// position for RMC/GGA, and no speed or heading at all for VTG.
//
// ONE DELIBERATE DEVIATION FROM THE ORIGINAL. Its build_RMC and build_VTG
// format the checksum "*%2hX" — space-padded, not zero-padded — so a checksum
// under 0x10 came out as "* 5" and the sentence was invalid to every reader
// but this one. build_GGA has it right ("%02hX"). The port uses GGA's form
// everywhere. Nothing is pinned over the old output, and a recorder whose
// files no other tool can read is not a recorder.
//
// The original's build_GLL is a stub that returns FALSE without writing
// anything; there is no BuildGll here rather than a function that cannot work.
std::string BuildRmc(const PositionFix& fix);
std::string BuildGga(const PositionFix& fix);
std::string BuildVtg(const PositionFix& fix);

// hhmmss.ss for a time of day, the format every sentence above uses.
std::string NmeaTimeOfDayString(double seconds_of_day);

}  // namespace fv

#endif  // FVKIT_NAV_NMEA_H_
