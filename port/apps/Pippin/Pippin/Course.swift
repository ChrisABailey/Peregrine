// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Course.swift — true courses as text, the course-over-ground vector drawn
// ahead of the ship, and the course deviation indicator's scale and readouts.
// Foundation-only, so it is tested on the mac (`ctest -R pippin_course`).

import Foundation

/// Which north courses and bearings are given from. Chart data is always true.
enum BearingReference: String, CaseIterable, Identifiable {
    case trueNorth = "true"
    case magnetic

    var id: String { rawValue }

    var menuName: String { self == .trueNorth ? "True" : "Magnetic" }
}

enum Course {
    /// A course as three digits and its reference, "047°T" or "055°M".
    /// `variation` is degrees east positive and is read only for magnetic.
    static func text(_ degrees: Double, reference: BearingReference = .trueNorth,
                     variation: Double = 0) -> String {
        let shown = reference == .magnetic ? degrees - variation : degrees
        var whole = Int(shown.rounded()) % 360
        if whole < 0 { whole += 360 }
        return String(format: reference == .magnetic ? "%03d°M" : "%03d°T", whole)
    }

    /// How far ahead the COG vector reaches: the distance made good in six
    /// minutes, the usual chart-plotter default (one tenth of SOG in nm).
    static let vectorSeconds: TimeInterval = 360

    /// Below half a knot the receiver's course is noise and no vector is drawn.
    static let vectorMinimumSpeed = 0.5 * 1852.0 / 3600.0

    /// Mean earth radius, metres (IUGG).
    private static let earthRadius = 6_371_008.8

    /// The end of the COG vector: the point reached from (`latitude`,
    /// `longitude`) along the rhumb line `courseDegrees` true at
    /// `speedMetersPerSecond` for `vectorSeconds`. Nil below the minimum speed.
    static func vectorEnd(latitude: Double, longitude: Double, courseDegrees: Double,
                          speedMetersPerSecond: Double) -> (latitude: Double, longitude: Double)? {
        guard speedMetersPerSecond >= vectorMinimumSpeed,
              courseDegrees.isFinite, latitude.isFinite, longitude.isFinite else { return nil }
        let delta = speedMetersPerSecond * vectorSeconds / earthRadius
        let theta = courseDegrees * .pi / 180
        let phi1 = latitude * .pi / 180
        var phi2 = phi1 + delta * cos(theta)
        // Clamp at the pole rather than wrap; a six-minute vector never gets there.
        phi2 = min(max(phi2, -.pi / 2 + 1e-9), .pi / 2 - 1e-9)
        let dPsi = log(tan(.pi / 4 + phi2 / 2) / tan(.pi / 4 + phi1 / 2))
        // East-west legs have no stretched-latitude change; use cos(lat) there.
        let q = abs(dPsi) > 1e-12 ? (phi2 - phi1) / dPsi : cos(phi1)
        let lambda = longitude * .pi / 180 + delta * sin(theta) / q
        var lon = lambda * 180 / .pi
        lon = (lon + 540).truncatingRemainder(dividingBy: 360) - 180
        return (phi2 * 180 / .pi, lon)
    }

    // MARK: - Course deviation indicator

    /// The needle's position on the CDI scale, -1 (full left) to 1 (full
    /// right). The needle is the course line: a boat right of track sees it to
    /// the left, which is also the way to steer. Full scale is `limitMeters`.
    static func needle(crossTrackMeters: Double, limitMeters: Double) -> Double {
        guard limitMeters > 0, crossTrackMeters.isFinite else { return 0 }
        return min(max(-crossTrackMeters / limitMeters, -1), 1)
    }

    /// Inside this fraction of full scale the boat is called on track.
    static let onTrackFraction = 0.1

    /// Cross-track error with its side, "0.04 nm R". Nautical miles keep two
    /// decimals, the plotter convention; the land units use `distance(_:)`.
    static func crossTrackText(_ meters: Double, units: DistanceUnits) -> String {
        let size = abs(meters)
        let side = size < 0.5 ? "" : (meters > 0 ? " R" : " L")
        switch units {
        case .nauticalMiles:
            return String(format: "%.2f nm", size / DistanceUnits.metersPerNauticalMile) + side
        case .kilometres, .miles:
            return units.distance(size) + side
        }
    }

    /// Which way to steer back to the leg, or "On track" near the centre.
    static func steerText(crossTrackMeters: Double, limitMeters: Double) -> String {
        if abs(crossTrackMeters) <= limitMeters * onTrackFraction { return "On track" }
        return crossTrackMeters > 0 ? "Steer left" : "Steer right"
    }

    /// Time to go in minutes, "<1 min", "25 min", "2 h 05 min".
    static func timeToGoText(_ seconds: Double) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "–" }
        if seconds < 60 { return "<1 min" }
        let minutes = Int((seconds / 60).rounded())
        if minutes < 60 { return "\(minutes) min" }
        return String(format: "%d h %02d min", minutes / 60, minutes % 60)
    }
}
