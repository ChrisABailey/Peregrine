// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// recording_marker_test.swift — the interrupted-ride decision, off the phone.
//
//   ctest --test-dir build -R recording_marker

import Foundation

var failures = 0
var checks = 0

func check(_ condition: Bool, _ what: String, line: UInt = #line) {
    checks += 1
    if !condition {
        failures += 1
        print("FAIL (line \(line)): \(what)")
    }
}

let start = Date(timeIntervalSince1970: 1_791_200_000)  // whole seconds: ISO 8601 drops fractions
let ride = InflightRecording(fileName: "ride-2026-10-04-1430.gpx", startedAt: start)
let always: (String) -> Bool = { _ in true }
let never: (String) -> Bool = { _ in false }

// --- the round trip ---------------------------------------------------------

do {
    let data = RecordingMarker.encode(ride)
    check(data != nil, "a marker encodes")
    check(data.flatMap(RecordingMarker.decode) == ride, "and decodes to the same ride")
    check(RecordingMarker.decode(Data("not json".utf8)) == nil, "garbage does not decode")
    check(RecordingMarker.decode(Data()) == nil, "an empty file does not decode")
}

// --- the launch decision ----------------------------------------------------

do {
    check(RecordingMarker.recovery(marker: nil, rideExists: always) == .none,
          "no marker: nothing to do")

    let data = RecordingMarker.encode(ride)!
    check(RecordingMarker.recovery(marker: data, rideExists: always)
            == .keep(from: "ride-2026-10-04-1430.gpx",
                     to: "ride-2026-10-04-1430-interrupted.gpx",
                     startedAt: start),
          "a marker whose ride exists keeps it, renamed")

    var asked: [String] = []
    _ = RecordingMarker.recovery(marker: data) { asked.append($0); return true }
    check(asked == ["ride-2026-10-04-1430.gpx"], "it asks about the marker's own file name")

    check(RecordingMarker.recovery(marker: data, rideExists: never) == .discardMarker,
          "a marker whose ride is gone is discarded")
    check(RecordingMarker.recovery(marker: Data("{}".utf8), rideExists: always)
            == .discardMarker,
          "an unreadable marker is discarded, not followed")
    // Killed between creating the marker and the recorder opening its file.
    check(RecordingMarker.recovery(marker: Data(), rideExists: always) == .discardMarker,
          "an empty marker is discarded")
}

// A marker is input from disk: it names a file in the rides directory and
// nothing else.
do {
    for bad in ["", "../Points.xml", "trips/ride.gpx", ".hidden.gpx", "ride.csv",
                "recording.inflight"] {
        let data = RecordingMarker.encode(InflightRecording(fileName: bad, startedAt: start))!
        check(RecordingMarker.recovery(marker: data, rideExists: always) == .discardMarker,
              "'\(bad)' is not followed")
    }
}

// --- names ------------------------------------------------------------------

do {
    check(RecordingMarker.interruptedName(for: "ride-2026-10-04-1430-2.gpx")
            == "ride-2026-10-04-1430-2-interrupted.gpx",
          "a uniqued name keeps its counter")
    check(RecordingMarker.interruptedName(for: "ride-x-interrupted.gpx")
            == "ride-x-interrupted.gpx",
          "an interrupted name is not suffixed twice")
    check(RecordingMarker.isInterrupted("ride-2026-10-04-1430-interrupted.gpx"),
          "the suffix is recognised")
    check(!RecordingMarker.isInterrupted("ride-2026-10-04-1430.gpx"),
          "a normal ride is not interrupted")
    check(!RecordingMarker.isInterrupted("interrupted.gpx"),
          "a bare word is not the suffix")
}

print("recording_marker: \(checks - failures)/\(checks) checks passed")
if failures > 0 {
    print("recording_marker: \(failures) FAILED")
    exit(1)
}
