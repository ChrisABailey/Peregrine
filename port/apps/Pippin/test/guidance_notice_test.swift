// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// guidance_notice_test.swift — guidance events to notification changes, off
// the phone.
//
//   ctest --test-dir build -R guidance_notice

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

func actions(_ e: GuidanceNoticeEvent, _ units: DistanceUnits = .kilometres)
    -> [GuidanceNoticeAction] {
    GuidanceNotice.actions(for: e, units: units)
}

// --- one notification per maneuver -----------------------------------------

do {
    let heads = GuidanceNoticeEvent(kind: .approach, ring: .headsUp, maneuverIndex: 3,
                                    turn: .left, distanceMeters: 240, road: "Folly Rd")
    var act = heads
    act.ring = .actNow
    act.distanceMeters = 62
    let a = actions(heads), b = actions(act)
    check(a == [.post(id: "guidance.turn.3", title: "Left in 250 m", body: "Onto Folly Rd",
                      timeSensitive: true)], "heads-up posts the turn: \(a)")
    check(b == [.post(id: "guidance.turn.3", title: "Left in 60 m", body: "Onto Folly Rd",
                      timeSensitive: true)], "act-now replaces it under the same id: \(b)")

    var other = heads
    other.maneuverIndex = 4
    if case .post(let id, _, _, _)? = actions(other).first {
        check(id != "guidance.turn.3", "another maneuver gets its own id")
    } else {
        check(false, "another maneuver posts")
    }
}

// --- the junction ring is not a notification --------------------------------

do {
    let at = GuidanceNoticeEvent(kind: .approach, ring: .at, maneuverIndex: 3,
                                 turn: .right, distanceMeters: 12)
    check(actions(at).isEmpty, "the at ring posts nothing")
    let none = GuidanceNoticeEvent(kind: .approach, ring: .none, maneuverIndex: 3)
    check(actions(none).isEmpty, "an approach with no ring posts nothing")
}

// --- passing, leaving and rejoining -----------------------------------------

do {
    let passed = GuidanceNoticeEvent(kind: .passed, maneuverIndex: 3)
    check(actions(passed) == [.remove(ids: ["guidance.turn.3"])], "passing removes that turn")

    let off = actions(GuidanceNoticeEvent(kind: .offRoute))
    check(off.count == 1, "off route is one action")
    if case .post(let id, let title, _, let ts)? = off.first {
        check(id == GuidanceNotice.offRouteID, "off route has its own id")
        check(title == "Off route", "off route title")
        check(ts, "off route is time sensitive")
    } else {
        check(false, "off route posts")
    }
    check(actions(GuidanceNoticeEvent(kind: .rejoined)) == [.remove(ids: [GuidanceNotice.offRouteID])],
          "rejoining removes the off-route notice and posts nothing")
}

// --- arrival clears the rest ------------------------------------------------

do {
    let arrived = actions(GuidanceNoticeEvent(kind: .arrived))
    check(arrived.first == .removeAll, "arrival clears first")
    check(arrived.count == 2, "then posts one notice")
    if case .post(let id, _, _, let ts)? = arrived.last {
        check(id == GuidanceNotice.arrivedID, "arrival id")
        check(!ts, "arrival is not time sensitive")
    }
}

// --- wording ----------------------------------------------------------------

do {
    var e = GuidanceNoticeEvent(kind: .approach, ring: .headsUp, maneuverIndex: 0,
                                turn: .slightRight, distanceMeters: 1150)
    check(GuidanceNotice.title(e, units: .miles) == "Bear right in 1250 yd",
          "yards under a mile: \(GuidanceNotice.title(e, units: .miles))")
    check(GuidanceNotice.title(e, units: .kilometres) == "Bear right in 1.1 km",
          "kilometres over a thousand metres")
    check(GuidanceNotice.body(e) == "", "an unnamed way has no body")
    e.distanceMeters = 3
    check(GuidanceNotice.title(e, units: .kilometres) == "Bear right now", "inside the junction")
    e.turn = .arrive
    e.distanceMeters = 240
    check(GuidanceNotice.title(e, units: .kilometres) == "Arriving in 250 m", "approach to the end")
    check(GuidanceNotice.body(e) == "End of the planned route.", "approach to the end body")
}

// --- identifiers share the prefix -------------------------------------------

for id in [GuidanceNotice.turnID(0), GuidanceNotice.offRouteID, GuidanceNotice.arrivedID] {
    check(id.hasPrefix(GuidanceNotice.prefix), "\(id) carries the prefix")
}

print("\(checks - failures)/\(checks) checks passed")
exit(failures == 0 ? 0 : 1)
