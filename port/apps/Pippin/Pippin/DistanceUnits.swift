// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DistanceUnits.swift — how a distance is written for a rider.
//
// Foundation only, and deliberately: the formatting is arithmetic and words,
// so it is testable on the mac (`test/run_guidance_format_test.sh`) without a
// simulator. `DisplayUnits`, the observable preference behind it, stays in
// MapMenu.swift with the menu that sets it.

import Foundation

/// The unit distances are shown in. A display preference only: the trip
/// computer, router and search all work in metres.
enum DistanceUnits: String, CaseIterable, Identifiable {
    case kilometres
    case miles

    var id: String { rawValue }

    var menuName: String {
        switch self {
        case .kilometres: return "Kilometres"
        case .miles: return "Miles"
        }
    }

    /// Formats a distance for reading at arm's length: the small unit close
    /// in, the big one further out, one decimal at most. The changeover is
    /// where the big unit gets its first digit, since "0.06 mi" is not a
    /// number anyone can picture.
    func distance(_ meters: Double) -> String {
        switch self {
        case .kilometres:
            if meters < 1000 { return String(format: "%.0f m", meters) }
            return String(format: "%.1f km", meters / 1000.0)
        case .miles:
            let feet = meters / Self.metersPerFoot
            if feet < Self.feetPerMile / 10 {
                return String(format: "%.0f ft", feet)
            }
            return String(format: "%.1f mi", feet / Self.feetPerMile)
        }
    }

    /// Whole units per hour, which is all the precision a bicycle has.
    func speed(_ metersPerSecond: Double) -> String {
        switch self {
        case .kilometres:
            return String(format: "%.0f km/h", metersPerSecond * 3.6)
        case .miles:
            let mph = metersPerSecond / Self.metersPerFoot / Self.feetPerMile
                * 3600.0
            return String(format: "%.0f mph", mph)
        }
    }

    /// A turn countdown, which is NOT `distance(_:)` and departs from it in
    /// two ways on purpose.
    ///
    /// The small imperial unit is the YARD rather than the foot: a rider is
    /// told "in 200 yards, turn right", and "in 600 feet" is the same
    /// distance in a unit nobody navigates in. Metric keeps the metre.
    ///
    /// The number is rounded to a step a rider can read at a glance — 10 below
    /// a hundred, 50 above — because the countdown redraws at the receiver's
    /// rate and an unrounded one flickers through every value on the way down.
    /// The unit is chosen from the rounded number AND the true one, because
    /// rounding crosses the boundary both ways: 995 m rounds UP to a
    /// kilometre and must not read "1000 m", while an exact mile rounds DOWN
    /// to 1750 yd and must not read as yards. Whichever form has reached the
    /// bigger unit wins.
    func countdown(_ meters: Double) -> String {
        // Inside the junction itself there is no distance left to give.
        if meters < Self.nowMeters { return "Now" }
        switch self {
        case .kilometres:
            let rounded = Self.readableStep(meters)
            if rounded >= 1000 || meters >= 1000 {
                return String(format: "%.1f km", meters / 1000.0)
            }
            return "\(rounded) m"
        case .miles:
            let yards = meters / Self.metersPerYard
            let rounded = Self.readableStep(yards)
            if Double(rounded) >= Self.yardsPerMile || yards >= Self.yardsPerMile {
                return String(format: "%.1f mi", yards / Self.yardsPerMile)
            }
            return "\(rounded) yd"
        }
    }

    /// The step the countdown is rounded to. Matches `GuidanceSettings`'
    /// act-now floor of 20 m at the bottom of its range, so the last few
    /// alerts land on numbers a rider has just heard.
    private static func readableStep(_ value: Double) -> Int {
        if value < 100 { return Int((value / 10).rounded()) * 10 }
        return Int((value / 50).rounded()) * 50
    }

    /// `fv::nav::GuidanceSettings::at_m`. Kept in metres for both units: it is
    /// a real distance from a real junction, not a display step.
    private static let nowMeters = 10.0

    /// The international foot and the statute mile, kept separate so the feet
    /// conversion is exact too. The yard is the countdown's unit and the mile
    /// is 1760 of them exactly.
    private static let metersPerFoot = 0.3048
    private static let feetPerMile = 5280.0
    private static let metersPerYard = 0.9144
    private static let yardsPerMile = 1760.0
}

