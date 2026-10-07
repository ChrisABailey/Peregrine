// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RideActivityPolicy.swift — the guidance state as Live Activity content, and
// when a change is worth sending to the system.
//
// Foundation-only, so `ctest -R ride_activity` runs it on the mac.
// `RideActivity` applies the result through ActivityKit.

import Foundation

enum RideActivityPolicy {
    /// Below this speed the time to the turn is not shown. 1 m/s is a walk.
    static let minSpeedForTime = 1.0
    /// Shortest gap between two updates that change only the distance or the
    /// time to the turn. The card's minutes are recomputed on each update.
    static let minUpdateInterval: TimeInterval = 5
    /// A re-estimated arrival at the turn that moves less than this is not
    /// sent; the card shows whole minutes.
    static let turnAtTolerance: TimeInterval = 3

    /// Content for the next turn.
    static func approaching(turn: GuidanceNoticeEvent.Turn,
                            then: GuidanceNoticeEvent.Turn?,
                            distanceMeters: Double,
                            road: String,
                            speedMetersPerSecond: Double?,
                            now: Date,
                            units: DistanceUnits) -> RideActivityContent {
        var turnAt: Date?
        if let speed = speedMetersPerSecond, speed >= minSpeedForTime {
            turnAt = now.addingTimeInterval(max(0, distanceMeters) / speed)
        }
        return RideActivityContent(phase: .approaching,
                                   symbol: symbol(turn),
                                   thenSymbol: then.map(symbol),
                                   distance: units.countdown(distanceMeters),
                                   turnAt: turnAt,
                                   road: turn == .arrive ? "" : road)
    }

    static let offRoute = RideActivityContent(phase: .offRoute)
    static let arrived = RideActivityContent(phase: .arrived, symbol: symbol(.arrive))

    /// Whether `next` should replace `shown`, `sinceLast` seconds after the
    /// previous update. A new phase, turn or road goes at once; a distance or
    /// time-to-turn change waits out `minUpdateInterval`.
    static func needsUpdate(from shown: RideActivityContent?,
                            to next: RideActivityContent,
                            sinceLast: TimeInterval) -> Bool {
        guard let shown else { return true }
        if shown == next { return false }
        if shown.phase != next.phase || shown.symbol != next.symbol
            || shown.thenSymbol != next.thenSymbol || shown.road != next.road {
            return true
        }
        guard sinceLast >= minUpdateInterval else { return false }
        if shown.distance != next.distance { return true }
        switch (shown.turnAt, next.turnAt) {
        case (nil, nil): return false
        case let (a?, b?): return abs(a.timeIntervalSince(b)) >= turnAtTolerance
        default: return true
        }
    }

    /// The SF Symbol for a turn, for the in-app banner and the card.
    /// `arrow.turn.*` rather than the `arrowtriangle` family: these are the
    /// shapes a rider already reads on a road sign.
    static func symbol(_ turn: GuidanceNoticeEvent.Turn) -> String {
        switch turn {
        case .depart: return "location.north.line"
        case .straight: return "arrow.up"
        case .slightLeft: return "arrow.up.left"
        case .left: return "arrow.turn.up.left"
        case .sharpLeft: return "arrow.uturn.left"
        case .slightRight: return "arrow.up.right"
        case .right: return "arrow.turn.up.right"
        case .sharpRight: return "arrow.uturn.right"
        case .uTurn: return "arrow.uturn.down"
        case .arrive: return "flag.checkered"
        }
    }
}
