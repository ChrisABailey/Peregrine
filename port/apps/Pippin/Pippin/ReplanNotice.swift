// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ReplanNotice.swift — the notice after map-key changes re-plan the route.
// Foundation only, so `test/run_replan_notice_test.sh` runs it on the mac.

import Foundation

enum ReplanNotice {
    /// Length changes smaller than this read as the same length.
    static let sameLengthMeters = 10.0

    /// The notice for a route re-planned with new preferences: the change in
    /// length and the road it now avoids, or that the route did not move.
    /// `avoidedRoad` is empty when no road was dropped.
    static func text(previousMeters: Double, meters: Double, avoidedRoad: String,
                     lineChanged: Bool, units: DistanceUnits) -> String {
        guard lineChanged else { return "Your preferences leave this route as it was." }
        let delta = meters - previousMeters
        var parts: [String] = []
        if delta >= sameLengthMeters {
            parts.append("\(units.distance(delta)) longer")
        } else if delta <= -sameLengthMeters {
            parts.append("\(units.distance(-delta)) shorter")
        } else {
            parts.append("about the same length")
        }
        if !avoidedRoad.isEmpty { parts.append("now avoids \(avoidedRoad)") }
        return "Route re-planned with your preferences: " + parts.joined(separator: ", ") + "."
    }
}
