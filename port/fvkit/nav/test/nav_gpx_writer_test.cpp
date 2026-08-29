// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// P10: the GPX WRITER and the recorder.
//
// The reader is the oracle. Nearly every test here writes something and reads
// it back with the code MM6 already pinned against a real Garmin file — which
// is the only way to test a writer without pinning a golden string that any
// harmless whitespace change would break. The exceptions are the few places
// where the BYTES are the point (no `<ele>0</ele>` for a fix with no altitude,
// a valid file after every recorded fix), and those look at the text.
//
// Per the ledger's rule on scratch files: every test names its OWN file, so
// the suite still runs in parallel.

#include "fvkit/nav/gpx_recorder.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fvkit/nav/gpx.h"

namespace fv {
namespace {

namespace fs = std::filesystem;

std::string TestDataDir() {
  const char* dir = std::getenv("FVW_TESTDATA_DIR");
  return (dir != nullptr && dir[0] != '\0') ? std::string(dir) : std::string("testdata");
}

std::string KiawahGpxPath() { return TestDataDir() + "/kiawah_cycle.gpx"; }

std::string ScratchPath(const std::string& name) {
  return (fs::temp_directory_path() / ("fvkit_gpx_" + name)).string();
}

std::string ReadWholeFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

PositionFix MakeFix(double lat, double lon, double time_s) {
  PositionFix fix;
  fix.SetPosition(lat, lon);
  fix.time_s = time_s;
  fix.has_time = true;
  return fix;
}

// The writer's own precision (gpx.h rule 4), not the double's.
constexpr double kDegreeTol = 1e-7;
constexpr double kEleTol = 0.05;

// ---------------------------------------------------------------------------
// The time format the recorder needs
// ---------------------------------------------------------------------------

TEST(GpxWriteTimeTest, FractionalDigitsRoundTrip) {
  double stamp = 0.0;
  ASSERT_TRUE(ParseIso8601Utc("2026-05-08T20:55:59.482Z", &stamp));
  const std::string text = FormatIso8601UtcFractional(stamp, 3);
  EXPECT_EQ(text, "2026-05-08T20:55:59.482Z");
  double parsed = 0.0;
  ASSERT_TRUE(ParseIso8601Utc(text, &parsed));
  EXPECT_NEAR(parsed, stamp, 0.0005);
}

TEST(GpxWriteTimeTest, ZeroDigitsIsThePlainForm) {
  EXPECT_EQ(FormatIso8601UtcFractional(1778619359.0, 0), FormatIso8601Utc(1778619359.0));
}

// The bug this format is easiest to get wrong on: rounding the fraction up
// through a whole second after the date has already been split off.
TEST(GpxWriteTimeTest, RoundingUpDoesNotProduceSixtySeconds) {
  double midnight = 0.0;
  ASSERT_TRUE(ParseIso8601Utc("2026-05-08T23:59:59.9999Z", &midnight));
  const std::string text = FormatIso8601UtcFractional(midnight, 3);
  EXPECT_EQ(text, "2026-05-09T00:00:00.000Z");
  double parsed = 0.0;
  EXPECT_TRUE(ParseIso8601Utc(text, &parsed));
}

// ---------------------------------------------------------------------------
// WriteGpx: the mechanism
// ---------------------------------------------------------------------------

TEST(GpxWriteTest, TrackPointsSurviveAWriteAndARead) {
  GpxDocument original;
  original.name = "Morning ride";
  original.has_time = true;
  original.time_s = 1778619359.0;
  GpxTrack track;
  track.name = "Morning ride";
  track.type = "cycling";
  GpxTrackSegment segment;
  for (int i = 0; i < 5; ++i) {
    PositionFix fix = MakeFix(32.6084100 + i * 0.0001, -80.0721300 + i * 0.0001,
                              1778619359.0 + i);
    fix.altitude_msl_m = 3.4 + i;
    fix.has_altitude = true;
    segment.points.push_back(fix);
  }
  track.segments.push_back(segment);
  original.tracks.push_back(track);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(original), &reread).ok());

  EXPECT_EQ(reread.version, "1.1");
  EXPECT_EQ(reread.creator, "Peregrine");
  EXPECT_EQ(reread.name, "Morning ride");
  ASSERT_TRUE(reread.has_time);
  EXPECT_NEAR(reread.time_s, original.time_s, 0.0005);
  ASSERT_EQ(reread.tracks.size(), 1u);
  EXPECT_EQ(reread.tracks[0].name, "Morning ride");
  EXPECT_EQ(reread.tracks[0].type, "cycling");
  ASSERT_EQ(reread.tracks[0].segments.size(), 1u);
  ASSERT_EQ(reread.tracks[0].segments[0].points.size(), 5u);
  for (std::size_t i = 0; i < 5; ++i) {
    const PositionFix& a = segment.points[i];
    const PositionFix& b = reread.tracks[0].segments[0].points[i];
    EXPECT_NEAR(b.lat, a.lat, kDegreeTol);
    EXPECT_NEAR(b.lon, a.lon, kDegreeTol);
    ASSERT_TRUE(b.has_altitude);
    EXPECT_NEAR(b.altitude_msl_m, a.altitude_msl_m, kEleTol);
    ASSERT_TRUE(b.has_time);
    EXPECT_NEAR(b.time_s, a.time_s, 0.0005);
  }
}

// Writer rule 1, and it is the rule a round-trip test alone would not catch:
// a zero written for an absent field reads back as a VALID zero.
TEST(GpxWriteTest, AnAbsentFieldIsAbsentAndNotAZero) {
  GpxDocument document;
  GpxTrack track;
  GpxTrackSegment segment;
  PositionFix bare;
  bare.SetPosition(32.6, -80.07);  // no altitude, no time
  segment.points.push_back(bare);
  track.segments.push_back(segment);
  document.tracks.push_back(track);

  const std::string text = WriteGpx(document);
  EXPECT_EQ(text.find("<ele>"), std::string::npos);
  EXPECT_EQ(text.find("<time>"), std::string::npos);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(text, &reread).ok());
  ASSERT_EQ(reread.track_point_count(), 1u);
  const PositionFix& point = reread.tracks[0].segments[0].points[0];
  EXPECT_TRUE(point.has_position);
  EXPECT_FALSE(point.has_altitude);
  EXPECT_FALSE(point.has_time);
}

TEST(GpxWriteTest, SegmentBoundariesAreWrittenAsSegments) {
  GpxDocument document;
  GpxTrack track;
  for (int s = 0; s < 3; ++s) {
    GpxTrackSegment segment;
    for (int i = 0; i < 2; ++i) {
      segment.points.push_back(MakeFix(32.6 + s * 0.01, -80.07 + i * 0.001, 1778619359.0 + s * 600 + i));
    }
    track.segments.push_back(segment);
  }
  document.tracks.push_back(track);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(document), &reread).ok());
  ASSERT_EQ(reread.tracks.size(), 1u);
  EXPECT_EQ(reread.tracks[0].segments.size(), 3u);
  EXPECT_EQ(reread.track_point_count(), 6u);
}

TEST(GpxWriteTest, WaypointsKeepTheirNames) {
  GpxDocument document;
  document.waypoints.push_back(MakeFix(32.6084, -80.0721, 1778619359.0));
  document.waypoint_names.push_back("Beachwalker Park");
  document.waypoints.push_back(MakeFix(32.6100, -80.0800, 1778619400.0));
  document.waypoint_names.push_back("Freshfields");

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(document), &reread).ok());
  ASSERT_EQ(reread.waypoints.size(), 2u);
  ASSERT_EQ(reread.waypoint_names.size(), 2u);
  EXPECT_EQ(reread.waypoint_names[0], "Beachwalker Park");
  EXPECT_EQ(reread.waypoint_names[1], "Freshfields");
  EXPECT_NEAR(reread.waypoints[1].lat, 32.6100, kDegreeTol);
}

// What a shared point carries besides its coordinate: the sentence somebody
// wrote about the place.
TEST(GpxWriteTest, AWaypointsDescriptionSurvives) {
  GpxDocument document;
  document.waypoints.push_back(MakeFix(32.6084, -80.0721, 1778619359.0));
  document.waypoint_names.push_back("Beachwalker Park");
  document.waypoint_descriptions.push_back("Showers, and the only public parking on the west end.");

  const std::string text = WriteGpx(document);
  EXPECT_NE(text.find("<desc>"), std::string::npos);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(text, &reread).ok());
  ASSERT_EQ(reread.waypoint_descriptions.size(), 1u);
  EXPECT_EQ(reread.waypoint_descriptions[0],
            "Showers, and the only public parking on the west end.");
  // A waypoint with no description still gets an entry, so the three arrays
  // stay index-for-index.
  GpxDocument plain;
  plain.waypoints.push_back(MakeFix(32.6, -80.07, 1778619359.0));
  plain.waypoint_names.push_back("Nameless");
  GpxDocument plain_reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(plain), &plain_reread).ok());
  ASSERT_EQ(plain_reread.waypoint_descriptions.size(), 1u);
  EXPECT_TRUE(plain_reread.waypoint_descriptions[0].empty());
}

TEST(GpxWriteTest, RoutesComeBackAsRoutes) {
  GpxDocument document;
  GpxTrack route;
  route.name = "Planned loop";
  GpxTrackSegment segment;
  segment.points.push_back(MakeFix(32.60, -80.07, 1778619359.0));
  segment.points.push_back(MakeFix(32.61, -80.08, 1778619400.0));
  route.segments.push_back(segment);
  document.routes.push_back(route);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(document), &reread).ok());
  EXPECT_TRUE(reread.tracks.empty());
  ASSERT_EQ(reread.routes.size(), 1u);
  EXPECT_EQ(reread.routes[0].name, "Planned loop");
  EXPECT_EQ(reread.routes[0].point_count(), 2u);
}

// A rider's own name for a ride is not guaranteed to be XML.
TEST(GpxWriteTest, NamesAreEscaped) {
  GpxDocument document;
  document.name = "Ben & Jerry's <ride>";
  GpxTrack track;
  track.name = document.name;
  GpxTrackSegment segment;
  segment.points.push_back(MakeFix(32.6, -80.07, 1778619359.0));
  track.segments.push_back(segment);
  document.tracks.push_back(track);

  const std::string text = WriteGpx(document);
  EXPECT_NE(text.find("&amp;"), std::string::npos);
  EXPECT_NE(text.find("&lt;ride&gt;"), std::string::npos);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(text, &reread).ok());
  EXPECT_EQ(reread.name, "Ben & Jerry's <ride>");
}

TEST(GpxWriteTest, AnEmptyDocumentIsAValidEmptyGpx) {
  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(GpxDocument{}), &reread).ok());
  EXPECT_EQ(reread.version, "1.1");
  EXPECT_EQ(reread.track_point_count(), 0u);
}

TEST(GpxWriteFileTest, WritesAFileTheReaderOpens) {
  const std::string path = ScratchPath("write_file.gpx");
  fs::remove(path);
  const GpxDocument document =
      BuildGpxTrack({MakeFix(32.60, -80.07, 1778619359.0), MakeFix(32.61, -80.08, 1778619360.0)},
                    "Scratch ride", "cycling", /*split_gap_s=*/0.0);
  ASSERT_TRUE(WriteGpxFile(path, document).ok());

  GpxDocument reread;
  ASSERT_TRUE(ReadGpxFile(path, &reread).ok());
  EXPECT_EQ(reread.track_point_count(), 2u);
  // The temporary is gone: a directory listing shows a ride and not a ride
  // plus a .tmp.
  EXPECT_FALSE(fs::exists(path + ".tmp"));
  fs::remove(path);
}

TEST(GpxWriteFileTest, ADirectoryThatDoesNotExistIsAnError) {
  const Status status =
      WriteGpxFile(ScratchPath("no_such_dir_p10/ride.gpx"), GpxDocument{});
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kIoError);
}

// ---------------------------------------------------------------------------
// BuildGpxTrack
// ---------------------------------------------------------------------------

TEST(BuildGpxTrackTest, SplitsWhereTheRecorderSawAGap) {
  std::vector<PositionFix> fixes;
  for (int i = 0; i < 3; ++i) fixes.push_back(MakeFix(32.6 + i * 0.001, -80.07, 1000.0 + i));
  for (int i = 0; i < 3; ++i) fixes.push_back(MakeFix(32.7 + i * 0.001, -80.07, 2000.0 + i));

  const GpxDocument document = BuildGpxTrack(fixes, "Two halves", "cycling", /*split_gap_s=*/20.0);
  ASSERT_EQ(document.tracks.size(), 1u);
  EXPECT_EQ(document.tracks[0].segments.size(), 2u);
  EXPECT_EQ(document.track_point_count(), 6u);
  ASSERT_TRUE(document.has_time);
  EXPECT_NEAR(document.time_s, 1000.0, 1e-9);
}

TEST(BuildGpxTrackTest, AFixWithNoPositionIsNotATrackPoint) {
  PositionFix speed_only;  // what a VTG sentence delivers
  speed_only.speed_mps = 5.0;
  speed_only.has_speed = true;
  const GpxDocument document = BuildGpxTrack(
      {MakeFix(32.6, -80.07, 1000.0), speed_only, MakeFix(32.601, -80.07, 1001.0)}, "Ride", "", 0.0);
  EXPECT_EQ(document.track_point_count(), 2u);
}

TEST(BuildGpxTrackTest, NoFixesIsADocumentWithNoTrack) {
  const GpxDocument document = BuildGpxTrack({}, "Nothing", "cycling", 20.0);
  EXPECT_TRUE(document.tracks.empty());
  GpxDocument reread;
  EXPECT_TRUE(ParseGpx(WriteGpx(document), &reread).ok());
}

// ---------------------------------------------------------------------------
// The recorder
// ---------------------------------------------------------------------------

TEST(GpxRecorderTest, EveryFixIsOnDiskInAValidFileBeforeTheCallReturns) {
  const std::string path = ScratchPath("recorder_valid.gpx");
  fs::remove(path);

  GpxRecorderOptions options;
  options.track_name = "Crash test";
  options.track_type = "cycling";
  GpxRecorder recorder;
  ASSERT_TRUE(recorder.Open(path, options).ok());

  // The point of the whole design: read the file back mid-ride, without
  // closing the recorder, and it parses — with exactly the points written so
  // far. This is what a rider gets when the app is killed in their pocket.
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(recorder.Add(MakeFix(32.6 + i * 0.0005, -80.07, 1000.0 + i)).ok());
    GpxDocument mid_ride;
    ASSERT_TRUE(ParseGpx(ReadWholeFile(path), &mid_ride).ok()) << "after fix " << i;
    EXPECT_EQ(mid_ride.track_point_count(), static_cast<std::size_t>(i + 1));
    EXPECT_EQ(mid_ride.name, "Crash test");
    ASSERT_EQ(mid_ride.tracks.size(), 1u);
    EXPECT_EQ(mid_ride.tracks[0].type, "cycling");
  }

  ASSERT_TRUE(recorder.Close().ok());
  EXPECT_EQ(recorder.point_count(), 6u);
  EXPECT_EQ(recorder.segment_count(), 1u);

  GpxDocument finished;
  ASSERT_TRUE(ReadGpxFile(path, &finished).ok());
  EXPECT_EQ(finished.track_point_count(), 6u);
  fs::remove(path);
}

TEST(GpxRecorderTest, AGapStartsANewSegment) {
  const std::string path = ScratchPath("recorder_gap.gpx");
  fs::remove(path);

  GpxRecorderOptions options;
  options.track_name = "Lunch stop";
  options.split_gap_s = 20.0;
  GpxRecorder recorder;
  ASSERT_TRUE(recorder.Open(path, options).ok());
  ASSERT_TRUE(recorder.Add(MakeFix(32.600, -80.07, 1000.0)).ok());
  ASSERT_TRUE(recorder.Add(MakeFix(32.601, -80.07, 1001.0)).ok());
  ASSERT_TRUE(recorder.Add(MakeFix(32.700, -80.07, 4000.0)).ok());  // 50 minutes later
  ASSERT_TRUE(recorder.Add(MakeFix(32.701, -80.07, 4001.0)).ok());
  ASSERT_TRUE(recorder.Close().ok());
  EXPECT_EQ(recorder.segment_count(), 2u);

  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(path, &document).ok());
  ASSERT_EQ(document.tracks.size(), 1u);
  ASSERT_EQ(document.tracks[0].segments.size(), 2u);
  EXPECT_EQ(document.tracks[0].segments[0].points.size(), 2u);
  EXPECT_EQ(document.tracks[0].segments[1].points.size(), 2u);
  fs::remove(path);
}

TEST(GpxRecorderTest, IgnoresAFixWithNoPositionAndOneThatGoesBackwards) {
  const std::string path = ScratchPath("recorder_ignores.gpx");
  fs::remove(path);

  GpxRecorder recorder;
  ASSERT_TRUE(recorder.Open(path).ok());
  ASSERT_TRUE(recorder.Add(MakeFix(32.600, -80.07, 1000.0)).ok());

  PositionFix speed_only;
  speed_only.speed_mps = 4.0;
  speed_only.has_speed = true;
  EXPECT_TRUE(recorder.Add(speed_only).ok());  // kOk, and not a point

  EXPECT_TRUE(recorder.Add(MakeFix(32.599, -80.07, 999.0)).ok());   // cached, older
  EXPECT_TRUE(recorder.Add(MakeFix(32.600, -80.07, 1000.0)).ok());  // the same second
  ASSERT_TRUE(recorder.Add(MakeFix(32.601, -80.07, 1001.0)).ok());
  ASSERT_TRUE(recorder.Close().ok());

  EXPECT_EQ(recorder.point_count(), 2u);
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(path, &document).ok());
  EXPECT_EQ(document.track_point_count(), 2u);
  fs::remove(path);
}

TEST(GpxRecorderTest, ReportsTheSpanTheShellNamesTheFileBy) {
  const std::string path = ScratchPath("recorder_span.gpx");
  fs::remove(path);

  GpxRecorder recorder;
  EXPECT_FALSE(recorder.has_time_span());
  ASSERT_TRUE(recorder.Open(path).ok());
  ASSERT_TRUE(recorder.Add(MakeFix(32.600, -80.07, 1778619359.0)).ok());
  ASSERT_TRUE(recorder.Add(MakeFix(32.601, -80.07, 1778620000.0)).ok());
  EXPECT_TRUE(recorder.has_time_span());
  EXPECT_NEAR(recorder.first_time_s(), 1778619359.0, 1e-9);
  EXPECT_NEAR(recorder.last_time_s(), 1778620000.0, 1e-9);
  recorder.Close();
  fs::remove(path);
}

TEST(GpxRecorderTest, AddBeforeOpenIsACallerBug) {
  GpxRecorder recorder;
  const Status status = recorder.Add(MakeFix(32.6, -80.07, 1000.0));
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kInvalidArg);
}

TEST(GpxRecorderTest, OpeningIntoNowhereFails) {
  GpxRecorder recorder;
  const Status status = recorder.Open(ScratchPath("no_such_dir_p10/ride.gpx"));
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kIoError);
  EXPECT_FALSE(recorder.is_open());
}

// A recorder that is destroyed mid-ride — the app was killed — still leaves a
// file that reads, because the footer went down with every fix.
TEST(GpxRecorderTest, DestructionMidRideLeavesAReadableRide) {
  const std::string path = ScratchPath("recorder_destruct.gpx");
  fs::remove(path);
  {
    GpxRecorder recorder;
    ASSERT_TRUE(recorder.Open(path).ok());
    ASSERT_TRUE(recorder.Add(MakeFix(32.600, -80.07, 1000.0)).ok());
    ASSERT_TRUE(recorder.Add(MakeFix(32.601, -80.07, 1001.0)).ok());
  }
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(path, &document).ok());
  EXPECT_EQ(document.track_point_count(), 2u);
  fs::remove(path);
}

// The recorder and the whole-document writer must not be two formats.
TEST(GpxRecorderTest, ProducesTheSamePointsTheDocumentWriterWould) {
  const std::string path = ScratchPath("recorder_agreement.gpx");
  fs::remove(path);
  std::vector<PositionFix> fixes;
  for (int i = 0; i < 4; ++i) {
    PositionFix fix = MakeFix(32.6 + i * 0.0007, -80.07 - i * 0.0003, 1778619359.25 + i);
    fix.altitude_msl_m = 2.5 + i * 0.3;
    fix.has_altitude = true;
    fixes.push_back(fix);
  }

  GpxRecorder recorder;
  ASSERT_TRUE(recorder.Open(path).ok());
  for (const PositionFix& fix : fixes) ASSERT_TRUE(recorder.Add(fix).ok());
  ASSERT_TRUE(recorder.Close().ok());

  GpxDocument recorded;
  ASSERT_TRUE(ReadGpxFile(path, &recorded).ok());
  GpxDocument saved;
  ASSERT_TRUE(ParseGpx(WriteGpx(BuildGpxTrack(fixes, "", "", 20.0)), &saved).ok());

  ASSERT_EQ(recorded.track_point_count(), saved.track_point_count());
  const auto& a = recorded.tracks[0].segments[0].points;
  const auto& b = saved.tracks[0].segments[0].points;
  for (std::size_t i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i].lat, b[i].lat);
    EXPECT_DOUBLE_EQ(a[i].lon, b[i].lon);
    EXPECT_DOUBLE_EQ(a[i].altitude_msl_m, b[i].altitude_msl_m);
    EXPECT_DOUBLE_EQ(a[i].time_s, b[i].time_s);
  }
  fs::remove(path);
}

// ---------------------------------------------------------------------------
// The real ride, both ways round — the plan's own acceptance test
// ---------------------------------------------------------------------------

TEST(GpxWriteKiawahTest, TheRecordedRideSurvivesAWriteAndARead) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument original;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &original).ok());
  ASSERT_EQ(original.track_point_count(), 1705u);

  GpxDocument reread;
  ASSERT_TRUE(ParseGpx(WriteGpx(original), &reread).ok());
  ASSERT_EQ(reread.track_point_count(), 1705u);

  const std::vector<PositionFix> before = FlattenGpxFixes(original);
  const std::vector<PositionFix> after = FlattenGpxFixes(reread);
  ASSERT_EQ(before.size(), after.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_NEAR(after[i].lat, before[i].lat, kDegreeTol) << "point " << i;
    EXPECT_NEAR(after[i].lon, before[i].lon, kDegreeTol) << "point " << i;
    ASSERT_EQ(after[i].has_altitude, before[i].has_altitude) << "point " << i;
    if (before[i].has_altitude) {
      EXPECT_NEAR(after[i].altitude_msl_m, before[i].altitude_msl_m, kEleTol) << "point " << i;
    }
    ASSERT_EQ(after[i].has_time, before[i].has_time) << "point " << i;
    if (before[i].has_time) EXPECT_NEAR(after[i].time_s, before[i].time_s, 0.0005) << "point " << i;
    // Speed is DERIVED on both reads (writer rule 2), so it has to agree.
    ASSERT_EQ(after[i].has_speed, before[i].has_speed) << "point " << i;
    if (before[i].has_speed) EXPECT_NEAR(after[i].speed_mps, before[i].speed_mps, 0.01);
  }
}

// The loop the plan asks for closed the other way: play the real ride through
// the recorder as if a receiver were reporting it, then read back what landed.
TEST(GpxWriteKiawahTest, RecordingTheRealRideReproducesIt) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument original;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &original).ok());
  const std::vector<PositionFix> ridden = FlattenGpxFixes(original);
  ASSERT_EQ(ridden.size(), 1705u);

  const std::string path = ScratchPath("recorder_kiawah.gpx");
  fs::remove(path);
  GpxRecorderOptions options;
  options.track_name = "Kiawah";
  options.track_type = "cycling";
  GpxRecorder recorder;
  ASSERT_TRUE(recorder.Open(path, options).ok());
  for (const PositionFix& fix : ridden) ASSERT_TRUE(recorder.Add(fix).ok());
  ASSERT_TRUE(recorder.Close().ok());
  EXPECT_EQ(recorder.point_count(), 1705u);
  EXPECT_EQ(recorder.segment_count(), 1u);  // a gapless ride stays one segment

  GpxDocument recorded;
  ASSERT_TRUE(ReadGpxFile(path, &recorded).ok());
  ASSERT_EQ(recorded.track_point_count(), 1705u);
  const std::vector<PositionFix> after = FlattenGpxFixes(recorded);
  for (std::size_t i = 0; i < ridden.size(); ++i) {
    EXPECT_NEAR(after[i].lat, ridden[i].lat, kDegreeTol) << "point " << i;
    EXPECT_NEAR(after[i].lon, ridden[i].lon, kDegreeTol) << "point " << i;
    if (ridden[i].has_time) EXPECT_NEAR(after[i].time_s, ridden[i].time_s, 0.0005) << "point " << i;
  }
  fs::remove(path);
}

}  // namespace
}  // namespace fv
