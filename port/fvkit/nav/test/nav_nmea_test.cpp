// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// MM6: the NMEA parse, the assembler, the source and the build side.
//
// The sentences below are a stationary receiver off Kiawah at 32°36.2694'N
// 80°04.7436'W — the same water the routing and snapping work is pinned over.
// Every assertion that pins a QUIRK names it (Q1/Q2/Q3, nmea.h).

#include "fvkit/nav/nmea.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/nav/scripted_source.h"

namespace fv {
namespace {

// 32 + 36.2694/60, and -(80 + 4.7436/60).
constexpr double kLat = 32.60449;
constexpr double kLon = -80.07906;
constexpr double kTolerance = 1e-5;

// What a position survives a BUILD round trip to: minutes go out with three
// decimals, so half of the last digit is 0.0005 minutes ≈ 0.93 m ≈ 8.4e-6
// degrees of latitude.
constexpr double kBuildPositionTolerance = 1e-5;

// 12.50 knots in metres per second (exactly 1852/3600 per knot).
constexpr double kSpeedMps = 6.430555555555555;

// 2026-05-08T20:55:59Z.
constexpr double kEpoch = 1778273759.0;

const char kRmc[] = "$GPRMC,205559.00,A,3236.2694,N,08004.7436,W,12.50,187.30,080526,006.1,W*64";
const char kGga[] = "$GPGGA,205559.00,3236.2694,N,08004.7436,W,1,08,0.9,9.4,M,-33.9,M,,*5F";
const char kGll[] = "$GPGLL,3236.2694,N,08004.7436,W,205559.00,A*1F";
const char kVtg[] = "$GPVTG,187.30,T,193.40,M,12.50,N,23.15,K*4F";

// ---------------------------------------------------------------------------
// Checksum and the sentence sniff test
// ---------------------------------------------------------------------------

TEST(NmeaChecksumTest, MatchesTheSentencesOwnChecksum) {
  // The payload is what lies between the '$' and the '*'.
  const std::string sentence = kRmc;
  const std::size_t star = sentence.find('*');
  ASSERT_NE(star, std::string::npos);
  EXPECT_EQ(NmeaChecksum(sentence.data() + 1, star - 1), 0x64);
}

TEST(NmeaSentenceLooksValidTest, AcceptsGoodRejectsCorrupt) {
  EXPECT_TRUE(NmeaSentenceLooksValid(kRmc));
  EXPECT_TRUE(NmeaSentenceLooksValid(kGga));

  // Wrong checksum.
  EXPECT_FALSE(NmeaSentenceLooksValid(
      "$GPRMC,205559.00,A,3236.2694,N,08004.7436,W,12.50,187.30,080526,006.1,W*00"));
  // No leading '$'.
  EXPECT_FALSE(NmeaSentenceLooksValid("GPRMC,205559.00,A"));
  // Longer than 82 characters.
  EXPECT_FALSE(NmeaSentenceLooksValid("$" + std::string(100, 'A')));
}

// The original tests the checksum ONLY when one is present; plenty of
// receivers and hand-written logs omit it.
TEST(NmeaSentenceLooksValidTest, AcceptsASentenceWithNoChecksumAtAll) {
  EXPECT_TRUE(NmeaSentenceLooksValid("$GPGLL,3236.2694,N,08004.7436,W,205559.00,A"));
}

TEST(NmeaFieldsTest, KeepsEmptyFieldsAndDropsTheChecksum) {
  const std::vector<std::string> fields = SplitNmeaFields("$GPVTG,,,,,12.50,N,,K*7F");
  ASSERT_EQ(fields.size(), 9u);
  EXPECT_EQ(fields[0], "GPVTG");
  EXPECT_EQ(fields[1], "");
  EXPECT_EQ(fields[5], "12.50");
  EXPECT_EQ(fields[6], "N");
  EXPECT_EQ(fields[8], "K");
}

// ---------------------------------------------------------------------------
// The four sentences
// ---------------------------------------------------------------------------

TEST(NmeaParseTest, RmcCarriesPositionSpeedCourseAndDate) {
  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(kRmc, &reading));
  EXPECT_EQ(reading.type, NmeaType::kRmc);
  EXPECT_EQ(reading.talker, "GP");

  ASSERT_TRUE(reading.fix.has_position);
  EXPECT_NEAR(reading.fix.lat, kLat, kTolerance);
  EXPECT_NEAR(reading.fix.lon, kLon, kTolerance);

  ASSERT_TRUE(reading.fix.has_speed);
  EXPECT_NEAR(reading.fix.speed_mps, kSpeedMps, 1e-9);
  ASSERT_TRUE(reading.fix.has_true_heading);
  EXPECT_NEAR(reading.fix.true_heading_deg, 187.30, 1e-6);

  ASSERT_TRUE(reading.has_time_of_day);
  EXPECT_NEAR(reading.time_of_day_s, 20 * 3600 + 55 * 60 + 59, 1e-6);
  ASSERT_TRUE(reading.has_date);
  EXPECT_EQ(reading.year, 2026);
  EXPECT_EQ(reading.month, 5);
  EXPECT_EQ(reading.day, 8);

  // RMC carries no altitude and no satellite count, and says so per field —
  // position.h rule 1, which is the whole reason the sentinels went away.
  EXPECT_FALSE(reading.fix.has_altitude);
  EXPECT_FALSE(reading.fix.has_satellite_count);
}

// Q3: the magnetic heading is DERIVED from the variation. West adds, east
// subtracts — and both directions are asserted, because a sign error here
// looks perfectly plausible in one direction alone.
TEST(NmeaParseTest, RmcDerivesMagneticHeadingFromTheVariation) {
  NmeaReading westerly;
  ASSERT_TRUE(ParseNmeaSentence(kRmc, &westerly));
  ASSERT_TRUE(westerly.fix.has_magnetic_heading);
  EXPECT_NEAR(westerly.fix.magnetic_heading_deg, 187.30 + 6.1, 1e-5);

  NmeaReading easterly;
  ASSERT_TRUE(ParseNmeaSentence(
      "$GNRMC,205559.00,A,3236.2694,N,08004.7436,W,12.50,187.30,080526,006.1,E*68", &easterly));
  ASSERT_TRUE(easterly.fix.has_magnetic_heading);
  EXPECT_NEAR(easterly.fix.magnetic_heading_deg, 187.30 - 6.1, 1e-5);
}

// Any talker, not just "$GP" — the deliberate deviation the header describes.
// A phone talks GN, and MM6 exists so a phone can drive the map.
TEST(NmeaParseTest, AcceptsAnyTalkerAndReportsIt) {
  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(
      "$GNRMC,205559.00,A,3236.2694,N,08004.7436,W,12.50,187.30,080526,006.1,E*68", &reading));
  EXPECT_EQ(reading.talker, "GN");
  EXPECT_EQ(reading.type, NmeaType::kRmc);
}

TEST(NmeaParseTest, GgaCarriesAltitudeQualityAndSatellites) {
  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(kGga, &reading));
  EXPECT_EQ(reading.type, NmeaType::kGga);
  EXPECT_NEAR(reading.fix.lat, kLat, kTolerance);
  ASSERT_TRUE(reading.fix.has_altitude);
  EXPECT_NEAR(reading.fix.altitude_msl_m, 9.4, 1e-6);
  ASSERT_TRUE(reading.fix.has_hdop);
  EXPECT_NEAR(reading.fix.hdop, 0.9, 1e-6);
  ASSERT_TRUE(reading.fix.has_satellite_count);
  EXPECT_EQ(reading.fix.satellite_count, 8);

  // The geoid separation is reported and NOT applied: the altitude stays the
  // MSL height the receiver computed.
  ASSERT_TRUE(reading.has_geoid_separation);
  EXPECT_NEAR(reading.geoid_separation_m, -33.9, 1e-6);
  EXPECT_NEAR(reading.fix.altitude_msl_m, 9.4, 1e-6);

  // GGA has no speed and no course at all.
  EXPECT_FALSE(reading.fix.has_speed);
  EXPECT_FALSE(reading.fix.has_true_heading);
}

// Q2, preserved: fewer than four satellites and the altitude is not read, even
// though the field is right there and well formed.
TEST(NmeaParseTest, GgaWithhholdsAltitudeBelowFourSatellites) {
  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(
      "$GPGGA,205600.00,3236.2694,N,08004.7436,W,1,03,0.9,9.4,M,-33.9,M,,*5B", &reading));
  EXPECT_TRUE(reading.fix.has_position);
  EXPECT_EQ(reading.fix.satellite_count, 3);
  EXPECT_FALSE(reading.fix.has_altitude);
}

TEST(NmeaParseTest, GgaWithNoFixIsRejected) {
  NmeaReading reading;
  EXPECT_FALSE(ParseNmeaSentence(
      "$GPGGA,205559.00,3236.2694,N,08004.7436,W,0,08,0.9,9.4,M,-33.9,M,,*5E", &reading));
}

TEST(NmeaParseTest, GllCarriesPositionAndTimeOnly) {
  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(kGll, &reading));
  EXPECT_EQ(reading.type, NmeaType::kGll);
  EXPECT_NEAR(reading.fix.lat, kLat, kTolerance);
  EXPECT_NEAR(reading.fix.lon, kLon, kTolerance);
  EXPECT_TRUE(reading.has_time_of_day);
  EXPECT_FALSE(reading.has_date);
  EXPECT_FALSE(reading.fix.has_speed);
  EXPECT_FALSE(reading.fix.has_altitude);
}

TEST(NmeaParseTest, VtgCarriesMotionAndNoPosition) {
  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(kVtg, &reading));
  EXPECT_EQ(reading.type, NmeaType::kVtg);
  EXPECT_FALSE(reading.fix.has_position);
  ASSERT_TRUE(reading.fix.has_speed);
  EXPECT_NEAR(reading.fix.speed_mps, kSpeedMps, 1e-9);
  EXPECT_NEAR(reading.fix.true_heading_deg, 187.30, 1e-6);
  EXPECT_NEAR(reading.fix.magnetic_heading_deg, 193.40, 1e-6);
}

// A VTG with nothing in it is the original's FALSE: "no valid speed or
// heading information".
TEST(NmeaParseTest, EmptyVtgIsRejected) {
  NmeaReading reading;
  EXPECT_FALSE(ParseNmeaSentence("$GPVTG,,,,,,,,*52", &reading));
}

// Q1, preserved: a sentence one field short is rejected whole, position and
// all. A "repaired" short sentence would be a guess about which field is
// missing.
TEST(NmeaParseTest, ShortSentenceIsRejectedWhole) {
  NmeaReading reading;
  EXPECT_FALSE(
      ParseNmeaSentence("$GPRMC,205559.00,A,3236.2694,N,08004.7436,W,12.50,187.30,080526*1A",
                        &reading));
}

TEST(NmeaParseTest, InvalidStatusAndOutOfRangePositionsAreRejected) {
  NmeaReading reading;
  // 'V' — the receiver saying it has no fix.
  EXPECT_FALSE(ParseNmeaSentence(
      "$GPRMC,205559.00,V,3236.2694,N,08004.7436,W,12.50,187.30,080526,006.1,W*73", &reading));
  // 99 degrees of latitude.
  EXPECT_FALSE(ParseNmeaSentence(
      "$GPRMC,205559.00,A,9936.2694,N,08004.7436,W,12.50,187.30,080526,006.1,W*65", &reading));
  // A hemisphere character that is neither: make_degrees' -1000.0 branch.
  EXPECT_FALSE(ParseNmeaSentence(
      "$GPRMC,205559.00,A,3236.2694,X,08004.7436,W,12.50,187.30,080526,006.1,W*72", &reading));
}

TEST(NmeaParseTest, IgnoresSentencesItDoesNotRead) {
  NmeaReading reading;
  EXPECT_FALSE(ParseNmeaSentence("$GPGSV,3,1,11,01,05,040,25*4C", &reading));
  EXPECT_EQ(NmeaTypeOf("$GPGSV,3,1,11"), NmeaType::kUnknown);
}

// ---------------------------------------------------------------------------
// The calendar
// ---------------------------------------------------------------------------

TEST(NmeaTimeTest, Y2kRuleIsTheOriginals) {
  // gps.cpp: 71..99 are 1971..1999, 00..70 are 2000..2070.
  EXPECT_EQ(NmeaY2kYear(26), 2026);
  EXPECT_EQ(NmeaY2kYear(70), 2070);
  EXPECT_EQ(NmeaY2kYear(71), 1971);
  EXPECT_EQ(NmeaY2kYear(99), 1999);
  EXPECT_EQ(NmeaY2kYear(2026), 2026);  // already four digits
}

TEST(NmeaTimeTest, EpochRoundTrips) {
  EXPECT_NEAR(UtcToEpochSeconds(2026, 5, 8, 20 * 3600 + 55 * 60 + 59), kEpoch, 1e-6);
  EXPECT_NEAR(UtcToEpochSeconds(1970, 1, 1, 0.0), 0.0, 1e-9);

  int year = 0;
  int month = 0;
  int day = 0;
  double seconds = 0.0;
  EpochSecondsToUtc(kEpoch, &year, &month, &day, &seconds);
  EXPECT_EQ(year, 2026);
  EXPECT_EQ(month, 5);
  EXPECT_EQ(day, 8);
  EXPECT_NEAR(seconds, 20 * 3600 + 55 * 60 + 59, 1e-6);
}

// ---------------------------------------------------------------------------
// NmeaFixAssembler
// ---------------------------------------------------------------------------

// The reason MM1 built PositionFix::Merge: one instant arrives as several
// sentences and has to leave as one fix.
TEST(NmeaAssemblerTest, MergesOneEpochsSentencesIntoOneFix) {
  NmeaFixAssembler assembler;
  PositionFix fix;

  EXPECT_FALSE(assembler.AddLine(kGga, &fix));  // the group opens
  EXPECT_FALSE(assembler.AddLine(kRmc, &fix));  // same second: merged
  EXPECT_FALSE(assembler.AddLine(kVtg, &fix));  // no time at all: merged

  // The next epoch closes the first one.
  ASSERT_TRUE(assembler.AddLine(
      "$GPRMC,205600.00,A,3236.3000,N,08004.7000,W,13.00,190.00,080526,006.1,W*61", &fix));

  EXPECT_TRUE(fix.has_position);
  EXPECT_NEAR(fix.lat, kLat, kTolerance);
  ASSERT_TRUE(fix.has_altitude);          // from the GGA
  EXPECT_NEAR(fix.altitude_msl_m, 9.4, 1e-6);
  ASSERT_TRUE(fix.has_speed);             // from the RMC
  ASSERT_TRUE(fix.has_satellite_count);   // from the GGA
  EXPECT_EQ(fix.satellite_count, 8);

  // And the RMC's date turned the time of day into an instant.
  ASSERT_TRUE(fix.has_time);
  EXPECT_NEAR(fix.time_s, kEpoch, 1e-6);
}

// Without a date from anywhere, a fix has no time rather than a 1970 one.
TEST(NmeaAssemblerTest, NoDateMeansNoTimeNotAFakeOne) {
  NmeaFixAssembler assembler;
  PositionFix fix;
  EXPECT_FALSE(assembler.AddLine(kGll, &fix));
  ASSERT_TRUE(assembler.Flush(&fix));
  EXPECT_TRUE(fix.has_position);
  EXPECT_FALSE(fix.has_time);
  EXPECT_FALSE(assembler.has_date());
}

TEST(NmeaAssemblerTest, DateHintStampsATimeOnlyStream) {
  NmeaFixAssembler assembler;
  assembler.SetDateHint(26, 5, 8);  // a 2-digit year goes through the Y2K rule
  PositionFix fix;
  EXPECT_FALSE(assembler.AddLine(kGll, &fix));
  ASSERT_TRUE(assembler.Flush(&fix));
  ASSERT_TRUE(fix.has_time);
  EXPECT_NEAR(fix.time_s, kEpoch, 1e-6);
}

// The low-latency mode: a live feed gets each sentence as it lands, still
// merged with what the epoch already contributed.
TEST(NmeaAssemblerTest, EmitPerSentenceDoesNotWaitForTheNextEpoch) {
  NmeaFixAssembler assembler;
  assembler.SetEmitPerSentence(true);
  PositionFix fix;

  ASSERT_TRUE(assembler.AddLine(kGga, &fix));
  EXPECT_TRUE(fix.has_altitude);
  EXPECT_FALSE(fix.has_speed);

  ASSERT_TRUE(assembler.AddLine(kRmc, &fix));
  EXPECT_TRUE(fix.has_altitude);  // still carrying the GGA's contribution
  EXPECT_TRUE(fix.has_speed);
  EXPECT_EQ(assembler.fixes_emitted(), 2u);
}

// An epoch that has already gone out per sentence must not go out AGAIN when
// the next epoch closes it, nor a third time on Flush. Two sentences of one
// second followed by one of the next is three emissions, not five.
TEST(NmeaAssemblerTest, EmitPerSentenceNeverDeliversAnInstantTwice) {
  NmeaFixAssembler assembler;
  assembler.SetEmitPerSentence(true);
  PositionFix fix;

  EXPECT_TRUE(assembler.AddLine(kGga, &fix));
  EXPECT_TRUE(assembler.AddLine(kRmc, &fix));
  EXPECT_TRUE(assembler.AddLine(
      "$GPRMC,205600.00,A,3236.3000,N,08004.7000,W,13.00,190.00,080526,006.1,W*61", &fix));
  EXPECT_NEAR(fix.lat, 32.605, 1e-4);  // the NEW epoch, not the closed one
  EXPECT_FALSE(assembler.Flush(&fix));
  EXPECT_EQ(assembler.fixes_emitted(), 3u);
}

// A VTG on its own never emits: it has no position, and a fix with no
// position is not a fix.
TEST(NmeaAssemblerTest, MotionOnlySentencesNeverEmitAlone) {
  NmeaFixAssembler assembler;
  PositionFix fix;
  EXPECT_FALSE(assembler.AddLine(kVtg, &fix));
  EXPECT_FALSE(assembler.Flush(&fix));
  EXPECT_EQ(assembler.sentences_parsed(), 1u);
  EXPECT_EQ(assembler.fixes_emitted(), 0u);
}

TEST(NmeaAssemblerTest, CountsWhatItRejects) {
  NmeaFixAssembler assembler;
  PositionFix fix;
  assembler.AddLine("not a sentence", &fix);
  assembler.AddLine("$GPGSV,3,1,11,01,05,040,25*4C", &fix);
  assembler.AddLine(kRmc, &fix);
  EXPECT_EQ(assembler.lines_seen(), 3u);
  EXPECT_EQ(assembler.sentences_parsed(), 1u);
  EXPECT_EQ(assembler.sentences_rejected(), 2u);
}

// ---------------------------------------------------------------------------
// NmeaLineSource
// ---------------------------------------------------------------------------

TEST(NmeaLineSourceTest, EmitsFixesAndFlushesTheLastEpochAtEndOfStream) {
  const std::string stream = std::string(kGga) + "\r\n" + kRmc + "\r\n" +
                             "$GPRMC,205600.00,A,3236.3000,N,08004.7000,W,13.00,190.00,080526,"
                             "006.1,W*61\r\n";
  auto transport = std::make_shared<StringLineTransport>(stream);
  NmeaLineSource source(transport);

  std::vector<PositionFix> got;
  source.SetListener([&got](const PositionFix& fix) { got.push_back(fix); });
  ASSERT_TRUE(source.Start().ok());
  while (source.running()) source.Poll();

  ASSERT_EQ(got.size(), 2u);
  EXPECT_NEAR(got[0].lat, kLat, kTolerance);
  EXPECT_TRUE(got[0].has_altitude);   // the merged GGA+RMC epoch
  EXPECT_NEAR(got[1].lat, 32.605, 1e-4);
  EXPECT_TRUE(source.at_end());
  EXPECT_EQ(source.lines_read(), 3u);
}

// The cap on one tick's work. Without it a file transport handed a long
// recording replays the lot inside a single frame.
TEST(NmeaLineSourceTest, MaxLinesPerPollBoundsOneTick) {
  std::string stream;
  for (int i = 0; i < 5; ++i) stream += std::string(kGll) + "\r\n";
  auto transport = std::make_shared<StringLineTransport>(stream, /*auto_end=*/false);
  NmeaLineSource source(transport);
  source.SetMaxLinesPerPoll(2);
  ASSERT_TRUE(source.Start().ok());

  source.Poll();
  EXPECT_EQ(source.lines_read(), 2u);
  source.Poll();
  EXPECT_EQ(source.lines_read(), 4u);
}

// A source that will not open does not run, and says why.
TEST(NmeaLineSourceTest, FailsToStartOnAnUnopenableTransport) {
  auto transport = std::make_shared<FileLineTransport>("/nonexistent/fv_nmea_log.txt");
  NmeaLineSource source(transport);
  const Status status = source.Start();
  EXPECT_FALSE(status.ok());
  EXPECT_FALSE(source.running());
}

// ---------------------------------------------------------------------------
// A recorded log, and the seam it shares with GPX
// ---------------------------------------------------------------------------

TEST(ReadNmeaLogTest, ReadsALogAndReplaysItAtRecordedSpeed) {
  const char* tmp = std::getenv("TMPDIR");
  std::string path = (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp";
  if (path.back() != '/') path += '/';
  path += "fv_nmea_read_log_test.nmea";
  {
    std::ofstream out(path, std::ios::binary);
    out << kGga << "\r\n" << kRmc << "\r\n";
    out << "$GPRMC,205600.00,A,3236.3000,N,08004.7000,W,13.00,190.00,080526,006.1,W*61\r\n";
  }

  std::vector<PositionFix> fixes;
  const Status status = ReadNmeaLog(path, &fixes);
  ASSERT_TRUE(status.ok()) << status.message;
  ASSERT_EQ(fixes.size(), 2u);
  EXPECT_NEAR(fixes[0].time_s, kEpoch, 1e-6);
  EXPECT_NEAR(fixes[1].time_s, kEpoch + 1.0, 1e-6);

  // The seam both recorded-track readers arrive at: the fixes' own stamps
  // become the schedule, so the replay runs at the speed it was recorded.
  const std::vector<ScriptedFix> script = BuildScriptedTrackFromFixes(fixes);
  ASSERT_EQ(script.size(), 2u);
  EXPECT_NEAR(script[0].t_s, 0.0, 1e-9);
  EXPECT_NEAR(script[1].t_s, 1.0, 1e-9);

  std::remove(path.c_str());
}

TEST(ReadNmeaLogTest, MissingFileIsNotFound) {
  std::vector<PositionFix> fixes;
  const Status status = ReadNmeaLog("/nonexistent/fv_nmea_log.txt", &fixes);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kNotFound);
}

// ---------------------------------------------------------------------------
// Building sentences
// ---------------------------------------------------------------------------

// The recorder's half. Round-tripping through the port's own parser is the
// assertion that matters: it pins the field COUNT (Q1 rejects one short), the
// zero-padded checksum and every unit conversion, in both directions at once.
TEST(NmeaBuildTest, RmcRoundTripsThroughTheParser) {
  PositionFix fix;
  fix.SetPosition(kLat, kLon);
  fix.speed_mps = kSpeedMps;
  fix.has_speed = true;
  fix.true_heading_deg = 187.30;
  fix.has_true_heading = true;
  fix.magnetic_heading_deg = 193.40;
  fix.has_magnetic_heading = true;
  fix.time_s = kEpoch;
  fix.has_time = true;

  const std::string sentence = BuildRmc(fix);
  ASSERT_FALSE(sentence.empty());
  EXPECT_EQ(sentence.compare(0, 7, "$GPRMC,"), 0);
  EXPECT_EQ(sentence.substr(sentence.size() - 2), "\r\n");
  EXPECT_LE(sentence.size(), kMaxNmeaSentenceLength);

  const std::string stripped = sentence.substr(0, sentence.size() - 2);
  ASSERT_TRUE(NmeaSentenceLooksValid(stripped)) << stripped;

  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(stripped, &reading)) << stripped;
  // A built sentence writes minutes to THREE decimals, which is the
  // original's "%06.3f" and NMEA's own habit — so a position survives its own
  // round trip to about 0.0005 minutes, a little under two metres. That is a
  // property of the wire format and not of the parse: kBuildPositionTolerance
  // is that resolution, and nothing here should be tightened past it.
  EXPECT_NEAR(reading.fix.lat, kLat, kBuildPositionTolerance);
  EXPECT_NEAR(reading.fix.lon, kLon, kBuildPositionTolerance);
  EXPECT_NEAR(reading.fix.speed_mps, kSpeedMps, 1e-3);
  EXPECT_NEAR(reading.fix.true_heading_deg, 187.30, 1e-3);
  EXPECT_NEAR(reading.fix.magnetic_heading_deg, 193.40, 1e-3);
  ASSERT_TRUE(reading.has_date);
  EXPECT_EQ(reading.year, 2026);
  EXPECT_EQ(reading.month, 5);
  EXPECT_EQ(reading.day, 8);
  EXPECT_NEAR(reading.time_of_day_s, 20 * 3600 + 55 * 60 + 59, 1e-3);
}

// The checksum is ZERO-padded, unlike the original's build_RMC/build_VTG
// "%2hX" which emitted "* 5" for a checksum under 0x10. The header records
// why the port does not reproduce that.
TEST(NmeaBuildTest, ChecksumIsAlwaysTwoHexDigits) {
  PositionFix fix;
  fix.SetPosition(kLat, kLon);
  for (int minute = 0; minute < 60; ++minute) {
    fix.time_s = kEpoch + minute * 60.0;
    fix.has_time = true;
    const std::string sentence = BuildRmc(fix);
    ASSERT_FALSE(sentence.empty());
    const std::size_t star = sentence.find('*');
    ASSERT_NE(star, std::string::npos);
    ASSERT_EQ(sentence.size(), star + 5);  // '*', two digits, CR, LF
    EXPECT_NE(sentence[star + 1], ' ') << sentence;
    EXPECT_TRUE(NmeaSentenceLooksValid(sentence.substr(0, sentence.size() - 2))) << sentence;
  }
}

TEST(NmeaBuildTest, GgaRoundTripsIncludingAltitude) {
  PositionFix fix;
  fix.SetPosition(kLat, kLon);
  fix.altitude_msl_m = 9.4;
  fix.has_altitude = true;
  fix.hdop = 0.9;
  fix.has_hdop = true;
  fix.satellite_count = 8;
  fix.has_satellite_count = true;
  fix.time_s = kEpoch;
  fix.has_time = true;

  const std::string sentence = BuildGga(fix);
  ASSERT_FALSE(sentence.empty());
  const std::string stripped = sentence.substr(0, sentence.size() - 2);

  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(stripped, &reading)) << stripped;
  EXPECT_NEAR(reading.fix.altitude_msl_m, 9.4, 1e-3);
  EXPECT_EQ(reading.fix.satellite_count, 8);
  EXPECT_NEAR(reading.fix.hdop, 0.9, 1e-3);
}

// The writing side of Q2: a fix with an altitude and too few satellites to
// read it back would be a sentence that loses data on its own round trip, so
// the count is floored at 4 — and the fix's own count is otherwise reported,
// where the original always wrote 4.
TEST(NmeaBuildTest, GgaFloorsTheSatelliteCountOnlyWhenThereIsAnAltitude) {
  PositionFix with_altitude;
  with_altitude.SetPosition(kLat, kLon);
  with_altitude.altitude_msl_m = 9.4;
  with_altitude.has_altitude = true;
  with_altitude.satellite_count = 2;
  with_altitude.has_satellite_count = true;

  NmeaReading reading;
  std::string sentence = BuildGga(with_altitude);
  ASSERT_TRUE(ParseNmeaSentence(sentence.substr(0, sentence.size() - 2), &reading));
  EXPECT_EQ(reading.fix.satellite_count, 4);
  EXPECT_TRUE(reading.fix.has_altitude);

  PositionFix without_altitude;
  without_altitude.SetPosition(kLat, kLon);
  without_altitude.satellite_count = 2;
  without_altitude.has_satellite_count = true;
  sentence = BuildGga(without_altitude);
  ASSERT_TRUE(ParseNmeaSentence(sentence.substr(0, sentence.size() - 2), &reading));
  EXPECT_EQ(reading.fix.satellite_count, 2);
}

TEST(NmeaBuildTest, VtgRoundTripsAndKeepsItsFieldCount) {
  PositionFix fix;
  fix.speed_mps = kSpeedMps;
  fix.has_speed = true;
  fix.true_heading_deg = 187.30;
  fix.has_true_heading = true;

  const std::string sentence = BuildVtg(fix);
  ASSERT_FALSE(sentence.empty());
  const std::string stripped = sentence.substr(0, sentence.size() - 2);
  EXPECT_EQ(SplitNmeaFields(stripped).size(), 9u);  // the id plus Q1's eight

  NmeaReading reading;
  ASSERT_TRUE(ParseNmeaSentence(stripped, &reading)) << stripped;
  EXPECT_NEAR(reading.fix.speed_mps, kSpeedMps, 1e-3);
  EXPECT_NEAR(reading.fix.true_heading_deg, 187.30, 1e-3);
  EXPECT_FALSE(reading.fix.has_magnetic_heading);
}

TEST(NmeaBuildTest, RefusesToInventWhatIsNotThere) {
  PositionFix empty;
  EXPECT_TRUE(BuildRmc(empty).empty());
  EXPECT_TRUE(BuildGga(empty).empty());
  EXPECT_TRUE(BuildVtg(empty).empty());  // no speed and no heading
}

// A southern, eastern position: the hemisphere characters and the sign are
// asymmetric, so a golden round trip in one quadrant proves nothing about the
// other three (the ledger's rule on directional assertions).
TEST(NmeaBuildTest, HandlesEverySignOfLatitudeAndLongitude) {
  const double lats[] = {kLat, -kLat};
  const double lons[] = {kLon, -kLon};
  for (double lat : lats) {
    for (double lon : lons) {
      PositionFix fix;
      fix.SetPosition(lat, lon);
      const std::string sentence = BuildGga(fix);
      ASSERT_FALSE(sentence.empty());
      NmeaReading reading;
      const std::string stripped = sentence.substr(0, sentence.size() - 2);
      ASSERT_TRUE(ParseNmeaSentence(stripped, &reading)) << stripped;
      EXPECT_NEAR(reading.fix.lat, lat, kBuildPositionTolerance) << stripped;
      EXPECT_NEAR(reading.fix.lon, lon, kBuildPositionTolerance) << stripped;
    }
  }
}

}  // namespace
}  // namespace fv
