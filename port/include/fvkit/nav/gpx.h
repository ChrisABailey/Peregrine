// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/gpx.h — GPX 1.0/1.1 tracks, waypoints and routes (nav plan MM6).
//
// NO WINDOWS ORIGINAL. FalconView has no GPX reader — GPlan has three, all in
// C# — so every rule here is a decision rather than a port, and the ones worth
// stating are stated. Chris asked for it beside the NMEA work (2026-08-17),
// and it earns its place for a plain reason: GPX is what a bike computer, a
// phone or a watch actually hands you, while a raw NMEA log is what a receiver
// hands a program. The Kiawah fixture the port now tests against
// (`testdata/kiawah_cycle.gpx`, 1705 track points off a Garmin FIT export) is
// a real ride on the same island the routing and snapping work is pinned over.
//
// IT ARRIVES AT THE SAME SEAM AS EVERYTHING ELSE. A GPX track point becomes a
// `PositionFix`, so a recorded ride replays through the machinery MM1–MM5
// already built:
//
//   ReadGpxFile -> FlattenGpxFixes -> BuildScriptedTrackFromFixes
//     -> ScriptedSource -> MovingMapOverlay (camera, heading, road snap)
//
// `ReadNmeaLog` (nmea.h) is the same three calls with the first one swapped,
// which is the point: the moving map does not learn a second kind of track.
//
// THREE RULES.
//
// 1. A TRACK POINT'S TIME IS THE FIX'S TIME. GPX stamps ISO 8601 UTC, so
//    there is no clock to guess at, and a replay paced from those stamps runs
//    at the speed it was ridden. A point with no <time> yields a fix with
//    `has_time` false rather than an invented one.
//
// 2. SPEED IS DERIVED, HEADING IS NOT (by default). GPX carries neither in
//    its base schema. Speed is worth deriving — MM5 holds the snapped road
//    below a speed threshold, and a track with no speed at all would hold
//    forever — and it comes from the great-circle distance to the previous
//    point over the elapsed time, which is what a receiver's own Doppler
//    speed approximates. Heading is NOT derived here, because
//    `HeadingResolver` (MM1) already derives one and does it in SCREEN space,
//    which is the frame the ownship symbol is drawn in; a true bearing
//    written into `has_true_heading` would look REPORTED and quietly win.
//    `derive_true_heading` exists for a caller that wants the bearing anyway.
//
// 3. ELEVATION IS TAKEN AS MSL. GPX's <ele> is metres above the WGS-84
//    ellipsoid by the letter of the schema and metres above sea level in
//    every device that writes it. The port takes it as MSL — the same
//    pragmatic reading every GPX consumer makes — and says so here rather
//    than silently.

#ifndef FVKIT_NAV_GPX_H_
#define FVKIT_NAV_GPX_H_

#include <cstddef>
#include <string>
#include <vector>

#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {

// One <trkseg>: a run of points the recorder believes are continuous. A new
// segment is how a device says "the pen came off the paper" (a pause, a lost
// lock), and it is kept as a boundary rather than flattened away, because a
// straight line across a lunch break is not a track.
struct GpxTrackSegment {
  std::vector<PositionFix> points;
};

// One <trk>, or one <rte> read as a single-segment track.
struct GpxTrack {
  std::string name;
  std::string type;  // GPX 1.1's <type>, e.g. "cycling"
  std::vector<GpxTrackSegment> segments;

  std::size_t point_count() const;
};

// A whole GPX file.
struct GpxDocument {
  std::string creator;  // the <gpx creator="..."> attribute
  std::string version;  // "1.0" or "1.1"
  std::string name;     // <metadata><name> (1.1) or <name> (1.0)

  // <metadata><time>, epoch seconds. When the file was written, which is not
  // when the ride happened — the track points carry that.
  bool has_time = false;
  double time_s = 0.0;

  std::vector<PositionFix> waypoints;  // <wpt>, with their names alongside
  std::vector<std::string> waypoint_names;

  std::vector<GpxTrack> tracks;  // <trk>
  std::vector<GpxTrack> routes;  // <rte>, a PLANNED line and not a recorded one

  std::size_t track_point_count() const;

  // The track with the most points, or nullptr when there are none. What a
  // shell means by "open this ride": a file with one track answers it exactly,
  // and a file with a 4000-point ride and a 3-point marker track answers it
  // usefully.
  const GpxTrack* longest_track() const;
};

struct GpxReadOptions {
  // Fill in speed_mps from the distance and time between consecutive points of
  // a segment. See rule 2. The first point of each segment has no previous
  // point and so no speed.
  bool derive_speed = true;

  // Fill in true_heading_deg from the great-circle bearing to the NEXT point.
  // Off by default; see rule 2 for why.
  bool derive_true_heading = false;

  // Split a segment wherever consecutive points are more than this many
  // seconds apart, 0 to never split. A device that records through a stop
  // without starting a new <trkseg> otherwise leaves a straight line across
  // it, at an implausible derived speed.
  double split_gap_s = 0.0;

  // Drop a point that is not strictly later than the one before it. Devices
  // do repeat a timestamp, and a zero time delta is a division by zero in the
  // speed derivation and a duplicate frame in a replay.
  bool drop_non_monotonic_time = true;
};

// Reads a GPX file. kNotFound if it will not open, kInvalidArg if the XML is
// malformed (the message carries expat's line and column). A well-formed file
// with no tracks in it is kOk and an empty document: an empty GPX is data.
Status ReadGpxFile(const std::string& path, GpxDocument* out,
                   const GpxReadOptions& options = GpxReadOptions());

// The same over a buffer already in memory.
Status ParseGpx(const std::string& xml, GpxDocument* out,
                const GpxReadOptions& options = GpxReadOptions());

// Every track point of every track, segment boundaries flattened away, in
// file order. This is what feeds a replay; `GpxSegmentPath` below is what
// feeds a drawing, where the boundaries matter.
std::vector<PositionFix> FlattenGpxFixes(const GpxDocument& document);

// The positions of one segment, ready for GeoDraw::DrawGeoPolyline or for
// BuildScriptedTrack.
std::vector<GeoPoint> GpxSegmentPath(const GpxTrackSegment& segment);

// ISO 8601 UTC ("2026-05-08T20:55:59Z", with optional fractional seconds and
// an optional ±hh:mm offset) to epoch seconds. Exposed because it is the one
// piece of GPX that is genuinely tricky and worth testing on its own.
bool ParseIso8601Utc(const std::string& text, double* epoch_seconds);

// The inverse, always in Z form with whole seconds — what a GPX writer needs.
std::string FormatIso8601Utc(double epoch_seconds);

}  // namespace fv

#endif  // FVKIT_NAV_GPX_H_
