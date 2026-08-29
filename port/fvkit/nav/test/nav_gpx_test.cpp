// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// MM6: the GPX reader.
//
// Two halves. The synthetic documents below pin the MECHANISM — which element
// lands in which field, what a malformed one does, where a segment splits —
// and then `testdata/kiawah_cycle.gpx` (a real 28-minute ride on Kiawah, 1705
// points at 1 Hz off a Garmin FIT export) pins that the mechanism survives a
// file nobody here wrote.
//
// Per the ledger's rule on counts: the exact numbers below are pinned on ONE
// NAMED file and never on whatever else is in testdata/.

#include "fvkit/nav/gpx.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "fvkit/nav/scripted_source.h"

namespace fv {
namespace {

namespace fs = std::filesystem;

std::string TestDataDir() {
  const char* dir = std::getenv("FVW_TESTDATA_DIR");
  return (dir != nullptr && dir[0] != '\0') ? std::string(dir) : std::string("testdata");
}

std::string KiawahGpxPath() { return TestDataDir() + "/kiawah_cycle.gpx"; }

// A minimal GPX 1.1 document in the shape a real device writes: named track,
// one segment, points carrying elevation and an ISO 8601 stamp.
const char kSimpleGpx[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="Peregrine test" xmlns="http://www.topografix.com/GPX/1/1">
  <metadata>
    <name>A ride</name>
    <time>2026-05-08T20:00:00Z</time>
  </metadata>
  <wpt lat="32.60" lon="-80.07"><name>Start gate</name><ele>3.0</ele></wpt>
  <trk>
    <name>Bike</name>
    <type>cycling</type>
    <trkseg>
      <trkpt lat="32.6000000" lon="-80.0700000">
        <ele>9.4</ele>
        <time>2026-05-08T20:55:59Z</time>
        <hdop>0.9</hdop>
        <sat>8</sat>
      </trkpt>
      <trkpt lat="32.6000900" lon="-80.0700000">
        <ele>8.8</ele>
        <time>2026-05-08T20:56:09Z</time>
      </trkpt>
      <trkpt lat="32.6001800" lon="-80.0700000">
        <ele>8.4</ele>
        <time>2026-05-08T20:56:19Z</time>
      </trkpt>
    </trkseg>
  </trk>
</gpx>
)";

// ---------------------------------------------------------------------------
// ISO 8601
// ---------------------------------------------------------------------------

TEST(GpxTimeTest, ParsesTheFormsADeviceWrites) {
  double epoch = 0.0;
  ASSERT_TRUE(ParseIso8601Utc("2026-05-08T20:55:59Z", &epoch));
  EXPECT_NEAR(epoch, 1778273759.0, 1e-6);

  // Fractional seconds — a 10 Hz recorder writes them.
  ASSERT_TRUE(ParseIso8601Utc("2026-05-08T20:55:59.250Z", &epoch));
  EXPECT_NEAR(epoch, 1778273759.25, 1e-6);

  // No zone marker at all: the schema says UTC, so it is read as UTC.
  ASSERT_TRUE(ParseIso8601Utc("2026-05-08T20:55:59", &epoch));
  EXPECT_NEAR(epoch, 1778273759.0, 1e-6);

  // An explicit offset is applied, and in the right direction: 15:55:59 five
  // hours WEST of Greenwich is the same instant as 20:55:59 UTC, and so is
  // 01:55:59 the next morning five hours east of it.
  ASSERT_TRUE(ParseIso8601Utc("2026-05-08T15:55:59-05:00", &epoch));
  EXPECT_NEAR(epoch, 1778273759.0, 1e-6);
  ASSERT_TRUE(ParseIso8601Utc("2026-05-09T01:55:59+05:00", &epoch));
  EXPECT_NEAR(epoch, 1778273759.0, 1e-6);
}

TEST(GpxTimeTest, RejectsWhatIsNotATimestamp) {
  double epoch = 0.0;
  EXPECT_FALSE(ParseIso8601Utc("", &epoch));
  EXPECT_FALSE(ParseIso8601Utc("yesterday", &epoch));
  EXPECT_FALSE(ParseIso8601Utc("2026-05-08", &epoch));
  EXPECT_FALSE(ParseIso8601Utc("2026-13-08T20:55:59Z", &epoch));  // month 13
}

TEST(GpxTimeTest, FormatRoundTrips) {
  EXPECT_EQ(FormatIso8601Utc(1778273759.0), "2026-05-08T20:55:59Z");
  double epoch = 0.0;
  ASSERT_TRUE(ParseIso8601Utc(FormatIso8601Utc(0.0), &epoch));
  EXPECT_NEAR(epoch, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// The mechanism
// ---------------------------------------------------------------------------

TEST(GpxParseTest, ReadsMetadataTrackAndWaypoints) {
  GpxDocument document;
  const Status status = ParseGpx(kSimpleGpx, &document);
  ASSERT_TRUE(status.ok()) << status.message;

  EXPECT_EQ(document.creator, "Peregrine test");
  EXPECT_EQ(document.version, "1.1");
  EXPECT_EQ(document.name, "A ride");
  ASSERT_TRUE(document.has_time);
  EXPECT_NEAR(document.time_s, 1778270400.0, 1e-6);  // 2026-05-08T20:00:00Z

  ASSERT_EQ(document.waypoints.size(), 1u);
  ASSERT_EQ(document.waypoint_names.size(), 1u);
  EXPECT_EQ(document.waypoint_names[0], "Start gate");
  EXPECT_NEAR(document.waypoints[0].lat, 32.60, 1e-9);
  EXPECT_NEAR(document.waypoints[0].lon, -80.07, 1e-9);

  ASSERT_EQ(document.tracks.size(), 1u);
  const GpxTrack& track = document.tracks[0];
  EXPECT_EQ(track.name, "Bike");
  EXPECT_EQ(track.type, "cycling");
  ASSERT_EQ(track.segments.size(), 1u);
  EXPECT_EQ(track.point_count(), 3u);
  EXPECT_EQ(document.longest_track(), &track);
}

TEST(GpxParseTest, EveryTrackPointFieldLandsWhereItBelongs) {
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kSimpleGpx, &document).ok());
  const PositionFix& first = document.tracks[0].segments[0].points[0];

  ASSERT_TRUE(first.has_position);
  EXPECT_NEAR(first.lat, 32.6, 1e-9);
  EXPECT_NEAR(first.lon, -80.07, 1e-9);
  ASSERT_TRUE(first.has_altitude);
  EXPECT_NEAR(first.altitude_msl_m, 9.4, 1e-9);  // rule 3: <ele> is read as MSL
  ASSERT_TRUE(first.has_time);
  EXPECT_NEAR(first.time_s, 1778273759.0, 1e-6);
  ASSERT_TRUE(first.has_hdop);
  EXPECT_NEAR(first.hdop, 0.9, 1e-9);
  ASSERT_TRUE(first.has_satellite_count);
  EXPECT_EQ(first.satellite_count, 8);
}

// Rule 2: speed is derived and heading is not, so HeadingResolver's derived
// branch stays in charge of where the ownship symbol points. The first point
// of a segment has no previous point and so no speed.
TEST(GpxParseTest, DerivesSpeedButNotHeadingByDefault) {
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kSimpleGpx, &document).ok());
  const std::vector<PositionFix>& points = document.tracks[0].segments[0].points;
  ASSERT_EQ(points.size(), 3u);

  EXPECT_FALSE(points[0].has_speed);
  ASSERT_TRUE(points[1].has_speed);
  // 0.00009 degrees of latitude is very close to 10 m, over 10 seconds.
  EXPECT_NEAR(points[1].speed_mps, 1.0, 0.05);
  EXPECT_TRUE(points[2].has_speed);

  for (const PositionFix& point : points) {
    EXPECT_FALSE(point.has_true_heading);
  }
}

TEST(GpxParseTest, DerivesHeadingWhenAsked) {
  GpxReadOptions options;
  options.derive_true_heading = true;
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kSimpleGpx, &document, options).ok());
  const std::vector<PositionFix>& points = document.tracks[0].segments[0].points;

  // The track runs due north, so every heading is 0 — and a heading is taken
  // toward the NEXT point, with the last inheriting the one before it.
  for (const PositionFix& point : points) {
    ASSERT_TRUE(point.has_true_heading);
    EXPECT_NEAR(NormalizeHeadingDeg(point.true_heading_deg + 180.0), 180.0, 0.5);
  }
}

// A file that carries its own speed and course is not second-guessed.
TEST(GpxParseTest, KeepsSpeedAndCourseAFileAlreadyCarries) {
  const char kWithExtensions[] = R"(<?xml version="1.0"?>
<gpx version="1.1" creator="t" xmlns="http://www.topografix.com/GPX/1/1"
     xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1">
  <trk><trkseg>
    <trkpt lat="32.60" lon="-80.07"><time>2026-05-08T20:55:59Z</time></trkpt>
    <trkpt lat="32.61" lon="-80.07">
      <time>2026-05-08T20:56:59Z</time>
      <course>93.5</course>
      <extensions><gpxtpx:TrackPointExtension>
        <gpxtpx:speed>4.25</gpxtpx:speed>
        <gpxtpx:hr>142</gpxtpx:hr>
      </gpxtpx:TrackPointExtension></extensions>
    </trkpt>
  </trkseg></trk>
</gpx>)";

  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kWithExtensions, &document).ok());
  const PositionFix& second = document.tracks[0].segments[0].points[1];
  ASSERT_TRUE(second.has_speed);
  EXPECT_NEAR(second.speed_mps, 4.25, 1e-9);  // not the ~18 m/s the geometry says
  ASSERT_TRUE(second.has_true_heading);
  EXPECT_NEAR(second.true_heading_deg, 93.5, 1e-9);
}

TEST(GpxParseTest, SegmentsAreKeptApart) {
  const char kTwoSegments[] = R"(<gpx version="1.1" creator="t">
  <trk><name>Two</name>
    <trkseg><trkpt lat="32.60" lon="-80.07"/><trkpt lat="32.61" lon="-80.07"/></trkseg>
    <trkseg><trkpt lat="32.70" lon="-80.07"/></trkseg>
  </trk>
</gpx>)";
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kTwoSegments, &document).ok());
  ASSERT_EQ(document.tracks.size(), 1u);
  ASSERT_EQ(document.tracks[0].segments.size(), 2u);
  EXPECT_EQ(document.tracks[0].segments[0].points.size(), 2u);
  EXPECT_EQ(document.tracks[0].segments[1].points.size(), 1u);
  EXPECT_EQ(document.tracks[0].point_count(), 3u);
}

// A device that records through a stop without starting a new <trkseg> leaves
// a straight line across it at an implausible speed; split_gap_s is the way
// out, and it is off by default.
TEST(GpxParseTest, SplitsOnATimeGapWhenAsked) {
  const char kGappy[] = R"(<gpx version="1.1" creator="t"><trk><trkseg>
    <trkpt lat="32.60" lon="-80.07"><time>2026-05-08T20:00:00Z</time></trkpt>
    <trkpt lat="32.61" lon="-80.07"><time>2026-05-08T20:00:10Z</time></trkpt>
    <trkpt lat="32.62" lon="-80.07"><time>2026-05-08T21:00:00Z</time></trkpt>
    <trkpt lat="32.63" lon="-80.07"><time>2026-05-08T21:00:10Z</time></trkpt>
  </trkseg></trk></gpx>)";

  GpxDocument unsplit;
  ASSERT_TRUE(ParseGpx(kGappy, &unsplit).ok());
  EXPECT_EQ(unsplit.tracks[0].segments.size(), 1u);

  GpxReadOptions options;
  options.split_gap_s = 60.0;
  GpxDocument split;
  ASSERT_TRUE(ParseGpx(kGappy, &split, options).ok());
  ASSERT_EQ(split.tracks[0].segments.size(), 2u);
  EXPECT_EQ(split.tracks[0].segments[0].points.size(), 2u);
  EXPECT_EQ(split.tracks[0].segments[1].points.size(), 2u);
  EXPECT_EQ(split.tracks[0].point_count(), 4u);  // nothing was lost, only cut
}

TEST(GpxParseTest, DropsRepeatedTimestamps) {
  const char kRepeated[] = R"(<gpx version="1.1" creator="t"><trk><trkseg>
    <trkpt lat="32.60" lon="-80.07"><time>2026-05-08T20:00:00Z</time></trkpt>
    <trkpt lat="32.61" lon="-80.07"><time>2026-05-08T20:00:00Z</time></trkpt>
    <trkpt lat="32.62" lon="-80.07"><time>2026-05-08T20:00:01Z</time></trkpt>
  </trkseg></trk></gpx>)";
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kRepeated, &document).ok());
  EXPECT_EQ(document.tracks[0].point_count(), 2u);

  GpxReadOptions keep;
  keep.drop_non_monotonic_time = false;
  GpxDocument kept;
  ASSERT_TRUE(ParseGpx(kRepeated, &kept, keep).ok());
  EXPECT_EQ(kept.tracks[0].point_count(), 3u);
}

TEST(GpxParseTest, ReadsRoutesAsWellAsTracks) {
  const char kRoute[] = R"(<gpx version="1.1" creator="t">
  <rte><name>Planned</name>
    <rtept lat="32.60" lon="-80.07"><name>A</name></rtept>
    <rtept lat="32.61" lon="-80.06"><name>B</name></rtept>
  </rte></gpx>)";
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kRoute, &document).ok());
  EXPECT_TRUE(document.tracks.empty());  // a route is NOT a recorded track
  ASSERT_EQ(document.routes.size(), 1u);
  EXPECT_EQ(document.routes[0].name, "Planned");
  EXPECT_EQ(document.routes[0].point_count(), 2u);
  EXPECT_EQ(document.longest_track(), nullptr);
}

// GPX 1.0 puts the document name directly under <gpx> and has no <metadata>.
TEST(GpxParseTest, ReadsGpx10) {
  const char kGpx10[] = R"(<gpx version="1.0" creator="an old device">
  <name>Older</name>
  <trk><trkseg>
    <trkpt lat="32.60" lon="-80.07"><ele>1.0</ele><speed>3.5</speed></trkpt>
  </trkseg></trk></gpx>)";
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kGpx10, &document).ok());
  EXPECT_EQ(document.version, "1.0");
  EXPECT_EQ(document.name, "Older");
  ASSERT_EQ(document.track_point_count(), 1u);
  EXPECT_NEAR(document.tracks[0].segments[0].points[0].speed_mps, 3.5, 1e-9);
}

TEST(GpxParseTest, MalformedXmlIsAnErrorAndNotAPartialDocument) {
  GpxDocument document;
  const Status status = ParseGpx("<gpx><trk><trkseg></gpx>", &document);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kInvalidArg);
  EXPECT_TRUE(document.tracks.empty());
}

// A well-formed file with nothing in it is data, not an error.
TEST(GpxParseTest, AnEmptyGpxIsOk) {
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(R"(<gpx version="1.1" creator="t"></gpx>)", &document).ok());
  EXPECT_EQ(document.track_point_count(), 0u);
  EXPECT_TRUE(document.waypoints.empty());
}

// A point outside the graticule is a corrupt file, not a place. It is dropped
// rather than clamped, and the points around it survive.
TEST(GpxParseTest, DropsPointsOutsideTheGraticule) {
  const char kBad[] = R"(<gpx version="1.1" creator="t"><trk><trkseg>
    <trkpt lat="32.60" lon="-80.07"/>
    <trkpt lat="132.60" lon="-80.07"/>
    <trkpt lat="32.61" lon="-380.07"/>
    <trkpt lat="not a number" lon="-80.07"/>
    <trkpt lat="32.62" lon="-80.07"/>
  </trkseg></trk></gpx>)";
  GpxDocument document;
  ASSERT_TRUE(ParseGpx(kBad, &document).ok());
  EXPECT_EQ(document.track_point_count(), 2u);
}

// A GPX file comes off the open internet. An entity declaration has no
// legitimate use in one, so the parse refuses it outright rather than relying
// on an expansion limit.
TEST(GpxParseTest, RefusesEntityDeclarations) {
  const char kEntities[] =
      "<?xml version=\"1.0\"?>\n"
      "<!DOCTYPE gpx [<!ENTITY a \"aaaaaaaaaa\">]>\n"
      "<gpx version=\"1.1\" creator=\"t\"><name>&a;</name></gpx>";
  GpxDocument document;
  const Status status = ParseGpx(kEntities, &document);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kInvalidArg);
}

TEST(GpxReadFileTest, MissingFileIsNotFound) {
  GpxDocument document;
  const Status status = ReadGpxFile("/nonexistent/nothing.gpx", &document);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kNotFound);
}

// ---------------------------------------------------------------------------
// The real ride
// ---------------------------------------------------------------------------

// Exact counts on ONE NAMED file, per the ledger's rule. If this file is ever
// re-exported these numbers move together and the failure says so plainly.
TEST(GpxKiawahTest, ReadsTheRecordedRide) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  const Status status = ReadGpxFile(KiawahGpxPath(), &document);
  ASSERT_TRUE(status.ok()) << status.message << " (" << KiawahGpxPath() << ")";

  EXPECT_EQ(document.name, "Bike");
  ASSERT_EQ(document.tracks.size(), 1u);
  EXPECT_EQ(document.tracks[0].name, "Bike");
  EXPECT_EQ(document.tracks[0].type, "cycling");
  ASSERT_EQ(document.tracks[0].segments.size(), 1u);
  EXPECT_EQ(document.track_point_count(), 1705u);

  const std::vector<PositionFix>& points = document.tracks[0].segments[0].points;
  EXPECT_NEAR(points.front().lat, 32.6044914, 1e-9);
  EXPECT_NEAR(points.front().lon, -80.0790595, 1e-9);
  EXPECT_NEAR(points.front().altitude_msl_m, 9.4, 1e-9);
  ASSERT_TRUE(points.front().has_time);
  EXPECT_NEAR(points.front().time_s, 1778273759.0, 1e-6);  // 2026-05-08T20:55:59Z

  EXPECT_NEAR(points.back().lat, 32.6167474, 1e-9);
  EXPECT_NEAR(points.back().lon, -80.0214873, 1e-9);
  // A ride that ends below sea level is a barometric altimeter, not a bug —
  // and it is the reason the fixture is worth having: an unsigned or clamped
  // elevation would show up right here.
  EXPECT_NEAR(points.back().altitude_msl_m, -6.2, 1e-9);
  EXPECT_NEAR(points.back().time_s - points.front().time_s, 1704.0, 1e-6);
}

// The derived speed over a real ride: a 6.1 km bike ride in 28 minutes is
// about 3.6 m/s, and no 1-second step is a teleport.
TEST(GpxKiawahTest, DerivedSpeedIsPlausibleThroughout) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &document).ok());
  const std::vector<PositionFix>& points = document.tracks[0].segments[0].points;

  double total = 0.0;
  std::size_t counted = 0;
  double fastest = 0.0;
  for (std::size_t i = 1; i < points.size(); ++i) {
    ASSERT_TRUE(points[i].has_speed) << "point " << i;
    total += points[i].speed_mps;
    fastest = points[i].speed_mps > fastest ? points[i].speed_mps : fastest;
    ++counted;
  }
  ASSERT_GT(counted, 1700u);
  EXPECT_NEAR(total / counted, 3.58, 0.10);  // ~13 km/h, a cruising bicycle
  EXPECT_LT(fastest, 15.0);                  // no 1-second jump over ~54 km/h
}

// The whole point of the reader: a recorded ride replays through MM1's
// machinery, paced by its own timestamps, with no new code in the moving map.
TEST(GpxKiawahTest, ReplaysThroughScriptedSourceAtRecordedSpeed) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &document).ok());

  const std::vector<PositionFix> fixes = FlattenGpxFixes(document);
  ASSERT_EQ(fixes.size(), 1705u);

  const std::vector<ScriptedFix> track = BuildScriptedTrackFromFixes(fixes);
  ASSERT_EQ(track.size(), 1705u);
  EXPECT_NEAR(track.front().t_s, 0.0, 1e-9);
  EXPECT_NEAR(track.back().t_s, 1704.0, 1e-6);  // the ride's own duration

  // Play it on a clock the test owns, so this is an equality and not a sleep.
  double now = 1000.0;
  ScriptedSource source(track);
  source.SetClock([&now]() { return now; });
  std::vector<PositionFix> seen;
  source.SetListener([&seen](const PositionFix& fix) { seen.push_back(fix); });

  ASSERT_TRUE(source.Start().ok());
  EXPECT_EQ(seen.size(), 1u);  // the fix due at t = 0, from inside Start
  now += 10.5;
  source.Poll();
  EXPECT_EQ(seen.size(), 11u);  // 1 Hz: ten more have come due
  now += 1704.0;
  source.Poll();
  EXPECT_EQ(seen.size(), 1705u);
  EXPECT_TRUE(source.finished());
}

// max_gap_s caps a pause rather than replaying it. Nothing in this ride
// pauses, so the capped and uncapped scripts are identical — which is the
// assertion: the cap does not quietly reshape a track that has no gaps.
TEST(GpxKiawahTest, GapCapLeavesAGaplessRideAlone) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &document).ok());
  const std::vector<PositionFix> fixes = FlattenGpxFixes(document);

  FixScriptOptions capped;
  capped.max_gap_s = 5.0;
  const std::vector<ScriptedFix> with_cap = BuildScriptedTrackFromFixes(fixes, capped);
  const std::vector<ScriptedFix> without = BuildScriptedTrackFromFixes(fixes);
  ASSERT_EQ(with_cap.size(), without.size());
  EXPECT_NEAR(with_cap.back().t_s, without.back().t_s, 1e-9);
}

TEST(GpxKiawahTest, SegmentPathIsReadyToDraw) {
  if (!fs::exists(KiawahGpxPath())) GTEST_SKIP() << "kiawah_cycle.gpx not present";
  GpxDocument document;
  ASSERT_TRUE(ReadGpxFile(KiawahGpxPath(), &document).ok());
  const std::vector<GeoPoint> path = GpxSegmentPath(document.tracks[0].segments[0]);
  ASSERT_EQ(path.size(), 1705u);
  EXPECT_NEAR(path.front().lat, 32.6044914, 1e-9);
  EXPECT_NEAR(path.back().lon, -80.0214873, 1e-9);
}

}  // namespace
}  // namespace fv
