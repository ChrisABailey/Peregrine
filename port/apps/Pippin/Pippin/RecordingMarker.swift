// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RecordingMarker.swift — the ride that outlived the app.
//
// `MapModel.recordingURL` is main-actor state, and a jetsam kill or a swipe
// from the app switcher ends the process without `stopRecording`. Under When
// In Use the app is not relaunched, so the GPX is left valid (the recorder
// keeps its footer written) but orphaned, and the app forgets it was
// recording. `startRecording` writes `Documents/recording.inflight` and
// `stopRecording` removes it; a marker found at launch names a ride that was
// interrupted. That ride is kept, renamed `…-interrupted.gpx`, and never
// resumed: appending would mean seeking behind a closed footer.
//
// Foundation-only so `ctest -R recording_marker` runs it on the mac.

import Foundation

/// What `Documents/recording.inflight` records about the ride being written.
struct InflightRecording: Codable, Equatable {
    /// The GPX's file name inside `Documents/trips/`. A name rather than a
    /// path: the app container's path changes across updates and reinstalls.
    var fileName: String
    /// When recording started.
    var startedAt: Date
}

enum RecordingMarker {
    /// The marker's name in `Documents/`.
    static let fileName = "recording.inflight"

    /// The suffix an interrupted ride's file name carries before `.gpx`.
    static let interruptedSuffix = "-interrupted"

    /// What launch does with a marker it finds.
    enum Recovery: Equatable {
        /// No marker: the last ride ended normally.
        case none
        /// A marker with nothing usable behind it (unreadable, or its ride is
        /// gone): remove the marker and say nothing.
        case discardMarker
        /// Rename `from` to `to` in the rides directory, remove the marker,
        /// and tell the rider the ride started at `startedAt` was kept.
        case keep(from: String, to: String, startedAt: Date)
    }

    static func encode(_ recording: InflightRecording) -> Data? {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        return try? encoder.encode(recording)
    }

    static func decode(_ data: Data) -> InflightRecording? {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return try? decoder.decode(InflightRecording.self, from: data)
    }

    /// Decides what to do with the marker's contents at launch.
    /// `rideExists` answers whether a file of the given name is in the rides
    /// directory. A marker naming anything but a plain `.gpx` file name is
    /// discarded rather than followed.
    static func recovery(marker: Data?, rideExists: (String) -> Bool) -> Recovery {
        guard let marker else { return .none }
        guard let recording = decode(marker),
              isPlainGpxName(recording.fileName),
              rideExists(recording.fileName) else { return .discardMarker }
        return .keep(from: recording.fileName,
                     to: interruptedName(for: recording.fileName),
                     startedAt: recording.startedAt)
    }

    /// `ride-2026-10-04-1430.gpx` → `ride-2026-10-04-1430-interrupted.gpx`.
    /// Already-suffixed names are returned unchanged.
    static func interruptedName(for fileName: String) -> String {
        guard !isInterrupted(fileName) else { return fileName }
        let stem = (fileName as NSString).deletingPathExtension
        return stem + interruptedSuffix + ".gpx"
    }

    /// Whether a ride's file name marks it as interrupted.
    static func isInterrupted(_ fileName: String) -> Bool {
        (fileName as NSString).deletingPathExtension.hasSuffix(interruptedSuffix)
    }

    /// A bare `*.gpx` name with no directory part and nothing hidden.
    private static func isPlainGpxName(_ name: String) -> Bool {
        !name.isEmpty && !name.hasPrefix(".") && !name.contains("/")
            && (name as NSString).pathExtension.lowercased() == "gpx"
    }
}
