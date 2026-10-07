// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ride_activity_test.swift — Live Activity content and update throttling, off
// the phone.
//
//   ctest --test-dir build -R ride_activity

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

let t0 = Date(timeIntervalSince1970: 1_800_000_000)

func turn(_ m: Double, speed: Double? = 8, turn: GuidanceNoticeEvent.Turn = .right,
          then: GuidanceNoticeEvent.Turn? = nil, road: String = "Flyway Dr",
          at now: Date = t0, units: DistanceUnits = .miles) -> RideActivityContent {
    RideActivityPolicy.approaching(turn: turn, then: then, distanceMeters: m, road: road,
                                   speedMetersPerSecond: speed, now: now, units: units)
}

// --- content -----------------------------------------------------------------

do {
    let c = turn(365)
    check(c.phase == .approaching, "approaching")
    check(c.symbol == "arrow.turn.up.right", "right turn glyph: \(c.symbol)")
    check(c.thenSymbol == nil, "no staggered pair")
    check(c.distance == "400 yd", "distance in the app's units: \(c.distance)")
    check(c.road == "Flyway Dr", "road carried")
    let secs = c.turnAt.map { $0.timeIntervalSince(t0) } ?? -1
    check(abs(secs - 365.0 / 8) < 0.001, "turnAt is distance over speed: \(secs)")
}

do {
    check(turn(400, units: .kilometres).distance == "400 m", "metric distance")
    check(turn(400, then: .left).thenSymbol == "arrow.turn.up.left", "staggered pair glyph")
    check(turn(400, road: "").road == "", "unnamed way stays empty")
    check(turn(120, turn: .arrive).road == "", "approach to the destination names no road")
    check(turn(120, turn: .arrive).symbol == "flag.checkered", "destination glyph")
}

do {
    // Too slow for a time to mean anything: distance alone.
    check(turn(400, speed: nil).turnAt == nil, "no speed: no time")
    check(turn(400, speed: 0).turnAt == nil, "stopped: no time")
    check(turn(400, speed: 0.9).turnAt == nil, "below a walk: no time")
    check(turn(400, speed: 1.0).turnAt != nil, "at the threshold: time shown")
    check(turn(-5).turnAt == t0, "past the junction: due now, never in the past")
}

do {
    check(RideActivityPolicy.offRoute.phase == .offRoute, "off route phase")
    check(RideActivityPolicy.arrived.phase == .arrived, "arrived phase")
    check(RideActivityPolicy.arrived.symbol == "flag.checkered", "arrived glyph")
}

// --- when to send --------------------------------------------------------------

func needs(_ a: RideActivityContent?, _ b: RideActivityContent, _ dt: TimeInterval) -> Bool {
    RideActivityPolicy.needsUpdate(from: a, to: b, sinceLast: dt)
}

do {
    let a = turn(365)
    check(needs(nil, a, 0), "first content always goes")
    check(!needs(a, a, 60), "unchanged never goes")
    check(needs(a, turn(365, turn: .left), 0.1), "a new turn goes at once")
    check(needs(a, turn(365, then: .left), 0.1), "a new staggered pair goes at once")
    check(needs(a, turn(365, road: "Governors Dr"), 0.1), "a new road goes at once")
    check(needs(a, RideActivityPolicy.offRoute, 0.1), "off route goes at once")
    check(needs(RideActivityPolicy.offRoute, a, 0.1), "rejoining goes at once")
    check(needs(a, RideActivityPolicy.arrived, 0.1), "arrival goes at once")
}

do {
    // Distance and timer changes wait out the interval.
    let a = turn(365)
    let closer = turn(300, at: t0.addingTimeInterval(8))
    check(a.distance != closer.distance, "fixture: the distance text moved")
    check(!needs(a, closer, 4.9), "inside the interval: held")
    check(needs(a, closer, 5), "after the interval: sent")
}

do {
    // Same distance text: only a timer that has drifted is worth sending.
    let a = turn(365)
    let sameOnTime = turn(366, at: t0.addingTimeInterval(0.1))
    check(a.distance == sameOnTime.distance, "fixture: same text")
    check(!needs(a, sameOnTime, 30), "timer still right: held")
    let slowed = turn(366, speed: 4)
    check(needs(a, slowed, 5), "rider slowed, timer off by 45 s: sent")
    check(!needs(a, slowed, 1), "but not inside the interval")
    check(needs(a, turn(366, speed: 0), 5), "stopped: the timer is withdrawn")
    check(needs(turn(366, speed: 0), a, 5), "moving again: the timer returns")
}

do {
    func text(_ seconds: TimeInterval) -> String {
        RideActivityContent.minutesText(until: t0.addingTimeInterval(seconds), now: t0)
    }
    check(text(6 * 60 + 40) == "7 min", "6:40 rounds to 7 min: \(text(400))")
    check(text(6 * 60 + 20) == "6 min", "6:20 rounds to 6 min")
    check(text(60) == "1 min", "a minute out")
    check(text(59) == "<1 min", "inside the last minute")
    check(text(-5) == "<1 min", "past due")
}

print("ride_activity: \(checks - failures)/\(checks) checks passed")
if failures > 0 {
    print("ride_activity: \(failures) FAILED")
    exit(1)
}
