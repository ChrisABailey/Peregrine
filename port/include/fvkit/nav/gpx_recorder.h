// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fvkit/nav/gpx_recorder.h — a ride, written as it happens (Pippin plan P10).
//
// NO WINDOWS ORIGINAL, for the same reason `gpx.h` has none.
//
// WHY THIS IS NOT `WriteGpxFile` CALLED ONCE AT THE END. A phone records for
// three hours in a jersey pocket, and every way that ends badly ends the same
// way: the app is killed in the background, the battery dies, the rider force-
// quits it at a traffic light. A writer that keeps the ride in RAM and saves
// on Stop() loses ALL of it in each of those cases, which is the one failure a
// recorder must not have. So this appends: every fix is on disk, in a file
// that parses, before the call returns.
//
// THE TRICK, AND IT IS THE ONLY CLEVER THING IN HERE. The footer of a GPX file
// is a fixed 3 lines — `</trkseg></trk></gpx>` — so the recorder remembers
// where the footer STARTS, seeks back to it for the next point, writes the
// point, and writes the footer again after it. The footer's length never
// changes, so nothing has to be truncated and the file on disk is a COMPLETE,
// VALID GPX after every single fix. A crash between two fixes loses at most
// the fix that was in flight.
//
// Cost: one seek and one flush per fix. At 1 Hz over a 40-byte line that is
// nothing, and it is bought with the property that makes the feature worth
// having.
//
// WHAT IS RECORDED IS WHAT HAPPENED. The caller feeds the RAW fix — the one
// the receiver reported — and never `MovingMapOverlay::Applied()`, which is
// the snapped guess. A recording is evidence; the snap is an interpretation of
// it, and next year's snapper will interpret the same ride better.
//
// THREADING: none. The recorder belongs to whichever thread drains the fixes
// (in Pippin, the render queue, where both feeds converge) and does not
// pretend otherwise — the same position `PPMap` takes.

#ifndef FVKIT_NAV_GPX_RECORDER_H_
#define FVKIT_NAV_GPX_RECORDER_H_

#include <cstddef>
#include <fstream>
#include <string>

#include "fvkit/nav/gpx.h"
#include "fvkit/nav/position.h"

namespace fv {

struct GpxRecorderOptions {
  // Goes into <metadata><name> and <trk><name>. A rider recognises "Morning
  // ride" in a list of files; they do not recognise a UUID.
  std::string track_name;

  // <trk><type>, e.g. "cycling". Strava and Garmin Connect both read it, so a
  // shared ride lands in the right sport rather than as "other".
  std::string track_type;

  // Start a new <trkseg> when this many seconds pass with no fix (0 to never
  // split). 20 s is the default because it is long enough to survive a
  // receiver's ordinary stumble under trees and short enough that a stop at a
  // café does not become a straight line across the marsh. It mirrors
  // `GpxReadOptions::split_gap_s`, deliberately: the boundary the reader would
  // have INFERRED is one the recorder KNOWS, so it is written down.
  double split_gap_s = 20.0;

  // Drop a fix that is not strictly later than the one before it, which is
  // `GpxReadOptions::drop_non_monotonic_time` applied at the writing end.
  // iOS does hand back a cached fix with an old stamp on the first tick after
  // a resume.
  bool drop_non_monotonic_time = true;

  // Formatting, shared with the whole-document writer so a recorded file and a
  // saved one are byte-identical for the same points.
  GpxWriteOptions write;
};

// Records fixes into one GPX file, appending. Open one, Add() every fix,
// Close() when the ride ends — and if Close() is never reached, the file is
// still a complete ride up to the last fix.
class GpxRecorder {
 public:
  GpxRecorder() = default;
  ~GpxRecorder();
  GpxRecorder(const GpxRecorder&) = delete;
  GpxRecorder& operator=(const GpxRecorder&) = delete;

  // Creates (or TRUNCATES) `path` and writes the header and an empty track.
  // kIoError if the file will not open. Recording into an existing file is
  // deliberately not offered: appending to somebody else's GPX means parsing
  // it first, and a ride is a file.
  Status Open(const std::string& path, const GpxRecorderOptions& options = GpxRecorderOptions());

  bool is_open() const { return stream_.is_open(); }
  const std::string& path() const { return path_; }

  // Writes one fix. A fix with no position is IGNORED and answers kOk: an NMEA
  // VTG carries a speed and no position, and a recorder that failed on one
  // would fail on a perfectly ordinary feed.
  //
  // Returns kInvalidArg when the recorder is not open — that is a caller bug,
  // not a disk that went away.
  Status Add(const PositionFix& fix);

  // Points actually written (which is not the number of fixes offered — see
  // Add()).
  std::size_t point_count() const { return point_count_; }
  // Segments so far, 1 once the first point lands.
  std::size_t segment_count() const { return segment_count_; }
  // The stamp of the first and last written point; has_* false until one with
  // a time arrives. A shell names the file and titles the share sheet from
  // these rather than from its own clock.
  bool has_time_span() const { return has_time_span_; }
  double first_time_s() const { return first_time_s_; }
  double last_time_s() const { return last_time_s_; }

  // Flushes, closes, and leaves the finished file behind. Idempotent, and the
  // destructor calls it — a recorder that goes out of scope mid-ride still
  // leaves a valid file, because every fix already did.
  Status Close();

 private:
  Status WriteFooterAndFlush();

  std::ofstream stream_;
  std::string path_;
  GpxRecorderOptions options_;

  // Where the footer begins. The whole design is this one number.
  std::streampos footer_pos_ = 0;

  bool segment_open_ = false;
  std::size_t point_count_ = 0;
  std::size_t segment_count_ = 0;
  bool has_time_span_ = false;
  double first_time_s_ = 0.0;
  double last_time_s_ = 0.0;
  bool have_last_time_ = false;
};

}  // namespace fv

#endif  // FVKIT_NAV_GPX_RECORDER_H_
