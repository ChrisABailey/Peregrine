// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RideActivityContent.swift — what the ride's Live Activity shows.
//
// Compiled into the app and into the PippinActivity widget extension, which
// renders it. Everything is pre-formatted in the app, so the extension needs
// no PippinKit, no unit preference and no turn table of its own.

import Foundation

/// The Live Activity's content state: the next turn, or off route, or arrived.
struct RideActivityContent: Codable, Hashable {
    enum Phase: String, Codable, Hashable {
        case approaching, offRoute, arrived
    }

    var phase: Phase
    /// SF Symbol for the turn being counted down to.
    var symbol: String = ""
    /// SF Symbol for the corner straight after it, at a staggered junction.
    var thenSymbol: String?
    /// "400 yd", "1.1 km", "Now".
    var distance: String = ""
    /// When the rider reaches the turn at the current speed. Nil when stopped
    /// or too slow for the estimate to mean anything; the card then shows
    /// distance alone.
    var turnAt: Date?
    /// The road being joined; empty for an unnamed way.
    var road: String = ""
}

extension RideActivityContent {
    /// The time to the turn in whole minutes: "7 min", or "<1 min" inside the
    /// last minute. Whole minutes because the dimmed lock screen redraws about
    /// once a minute and shows a running timer's seconds as "--".
    static func minutesText(until turnAt: Date, now: Date) -> String {
        let seconds = turnAt.timeIntervalSince(now)
        if seconds < 60 { return "<1 min" }
        return "\(Int((seconds / 60).rounded())) min"
    }
}
