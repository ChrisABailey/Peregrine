// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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
  // <wpt><desc>, one per waypoint, empty where there is none. A TRACK point's
  // <desc> is still dropped — it has nowhere to live on a `PositionFix` — but
  // a waypoint is a PLACE somebody wrote a sentence about, and the sentence is
  // most of what makes it worth sharing. Parallel to `waypoints` for the same
  // reason `waypoint_names` is: a waypoint is a fix plus two strings, and
  // inventing a struct for two strings would change the reader's whole shape.
  std::vector<std::string> waypoint_descriptions;

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

// The same with a chosen number of fractional digits (0..3). A recorder is
// the reason: a phone stamps a fix at 20:55:59.482, and a writer that rounds
// every stamp to a whole second can hand two points of a 1 Hz ride the SAME
// time — which the reader is then right to drop as non-monotonic. The reader
// has always accepted a fraction; the writer now emits one.
std::string FormatIso8601UtcFractional(double epoch_seconds, int fractional_digits);

// ---------------------------------------------------------------------------
// Writing (P10)
// ---------------------------------------------------------------------------
//
// GPX 1.1 ONLY. The reader takes both versions because a file arrives from
// wherever it arrives; a writer that emits both would be two formats to keep
// right for no reader's benefit.
//
// FOUR RULES, and they are the mirror images of the reader's.
//
// 1. A FIELD WITH NO VALIDITY IS NOT WRITTEN. `has_altitude` false means no
//    <ele> element, not `<ele>0</ele>` — the reader would take the zero for a
//    sea-level fix, and rule 1 of the reader (a missing <time> yields
//    has_time false) only survives a round trip if the writer honours it.
//
// 2. SPEED IS NOT WRITTEN AT ALL. Base GPX has nowhere to put it: Garmin's
//    TrackPointExtension does, and writing a vendor namespace to carry a
//    number the reader DERIVES anyway (reader rule 2) would be a second
//    source of truth for a quantity that already has one. A ride written here
//    and read back derives its speed from the geometry, which is what the
//    reader does for every other file it is handed.
//
// 3. A SEGMENT BOUNDARY IS WRITTEN AS A SEGMENT BOUNDARY. `GpxTrackSegment`
//    is what the reader's `split_gap_s` produced, and <trkseg> is where it
//    goes back — so a recorder that saw the rider stop for lunch writes the
//    pen coming off the paper rather than relying on the next reader to
//    guess the same gap threshold.
//
// 4. THE PRECISION IS THE FORMAT'S, NOT THE DOUBLE'S. Seven decimals of
//    latitude is 11 mm, which is finer than any receiver knows and is where
//    every GPX on the internet sits; a %.17g round trip would be bit-exact
//    and unreadable. Round-trip equality is therefore stated as a TOLERANCE
//    (1e-7 degrees, 0.05 m of elevation), and the tests pin it there.
struct GpxWriteOptions {
  // The <gpx creator="..."> attribute: what wrote the file. Every consumer
  // shows it, so it is a real field and not decoration.
  std::string creator = "Peregrine";

  // Decimal places for lat/lon (rule 4) and for <ele>.
  int coordinate_decimals = 7;
  int elevation_decimals = 1;

  // Fractional digits on every <time>. -1 means AUTO: three digits when the
  // stamp actually carries a fraction, none when it does not — so a file read
  // from a 1 Hz device and written straight back out looks the way it came in.
  int time_decimals = -1;

  // Write <metadata><time> from GpxDocument::time_s when it is valid.
  bool write_metadata_time = true;

  // Indent with two spaces per level. Off gives one long line per element and
  // is only useful to a diff.
  bool pretty = true;
};

// The document as GPX 1.1 text. Never fails: a document with nothing in it is
// a valid empty GPX, which is the reader's own position on the matter.
std::string WriteGpx(const GpxDocument& document,
                     const GpxWriteOptions& options = GpxWriteOptions());

// The same, to a file. kIoError if it will not open or the write fails.
// Written to `path` + ".tmp" and renamed over `path`, so a reader that opens
// the file while it is being written sees the OLD ride and never half of the
// new one. (The recorder in `fvkit/nav/gpx_recorder.h` does NOT go through
// here — it appends, for the reason stated there.)
Status WriteGpxFile(const std::string& path, const GpxDocument& document,
                    const GpxWriteOptions& options = GpxWriteOptions());

// A one-track document from a run of fixes: what a recorder, a replay or a
// snapshot of a live ride all want. A new <trkseg> is started wherever
// consecutive fixes are more than `split_gap_s` apart (0 to never split),
// which is `GpxReadOptions::split_gap_s`'s mirror. Fixes without a position
// are skipped — there is no such thing as a <trkpt> without one.
GpxDocument BuildGpxTrack(const std::vector<PositionFix>& fixes, const std::string& track_name,
                          const std::string& track_type, double split_gap_s = 0.0);

// One <trkpt> element, indented `indent_level` levels, newline-terminated —
// exactly the bytes `WriteGpx` puts inside a <trkseg>. Public because the
// recorder (`fvkit/nav/gpx_recorder.h`) APPENDS points to a file it never
// holds in memory, and it must produce byte-identical points to the whole-
// document writer or a recorded ride and a saved one would be two formats.
std::string FormatGpxTrackPoint(const PositionFix& fix, const GpxWriteOptions& options,
                                int indent_level);

}  // namespace fv

#endif  // FVKIT_NAV_GPX_H_
