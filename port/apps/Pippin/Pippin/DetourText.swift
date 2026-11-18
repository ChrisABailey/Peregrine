// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The off-route detour button's wording (re-route plan RR-D4).
/// Foundation only, so `test/run_detour_text_test.sh` runs it on the mac.

import Foundation

/// What the detour button shows: an offer to apply or a detour to end.
struct DetourButton: Equatable {
    let label: String
    let systemImage: String
    /// The button ends the detour being ridden rather than applying an offer.
    let ends: Bool
}

enum DetourText {
    /// Detours within this of what was left of the route read as the same distance.
    static let sameLengthMeters = 30.0

    /// The button while a detour is the guided line.
    static let cancel = "End detour"

    /// The button for an offer.
    static func offer(turnAround: Bool, extraMeters: Double, backMeters: Double,
                      units: DistanceUnits) -> DetourButton {
        DetourButton(label: button(turnAround: turnAround, extraMeters: extraMeters,
                                   backMeters: backMeters, units: units),
                     systemImage: turnAround ? "arrow.uturn.backward"
                                             : "arrow.triangle.turn.up.right.diamond",
                     ends: false)
    }

    /// The button while a detour is being ridden.
    static let end = DetourButton(label: cancel, systemImage: "xmark", ends: true)

    /// "Turn around: 410 ft back", or "Re-route: +0.3 mi" with the metres the
    /// detour adds ("0.2 mi shorter" when it saves some).
    static func button(turnAround: Bool, extraMeters: Double, backMeters: Double,
                       units: DistanceUnits) -> String {
        if turnAround { return "Turn around: \(units.distance(backMeters)) back" }
        if extraMeters >= sameLengthMeters { return "Re-route: +\(units.distance(extraMeters))" }
        if extraMeters <= -sameLengthMeters {
            return "Re-route: \(units.distance(-extraMeters)) shorter"
        }
        return "Re-route: same distance"
    }
}
