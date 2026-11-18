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
    if case .post(let id, _, _, _, _, _)? = actions(other).first {
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
    if case .post(let id, let title, _, let ts, let category, let alerts)? = off.first {
        check(id == GuidanceNotice.offRouteID, "off route has its own id")
        check(title == "Off route", "off route title")
        check(ts, "off route is time sensitive")
        check(category.isEmpty, "plain off route carries no action")
        check(alerts, "off route alerts")
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
    if case .post(let id, _, _, let ts, _, _)? = arrived.last {
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

// --- course events ------------------------------------------------------

do {
    var reached = GuidanceNoticeEvent(kind: .waypoint, maneuverIndex: 2)
    reached.road = "G \"5\""
    reached.nextName = "Fort Sumter"
    check(actions(reached) == [.post(id: GuidanceNotice.waypointID, title: "Reached G \"5\"",
                                     body: "Now steering for Fort Sumter.", timeSensitive: true)],
          "waypoint names the point reached and the next")
    let unnamed = GuidanceNoticeEvent(kind: .waypoint, maneuverIndex: 2)
    check(actions(unnamed) == [.post(id: GuidanceNotice.waypointID, title: "Reached route point 2",
                                     body: "", timeSensitive: true)],
          "an unlabelled point is numbered")

    var off = GuidanceNoticeEvent(kind: .offCourse)
    off.crossTrackMeters = -400
    check(actions(off, .nauticalMiles) == [.post(id: GuidanceNotice.offCourseID, title: "Off course",
                                                        body: "0.2 nm left of the leg. Steer right.",
                                                        timeSensitive: true)],
          "off course gives the side and the steer")
    check(actions(GuidanceNoticeEvent(kind: .backOnCourse)) == [.remove(ids: [GuidanceNotice.offCourseID])],
          "back on course withdraws the off-course notice")
}

// --- a junction turn where the road does not change -------------------------

do {
    var e = GuidanceNoticeEvent(kind: .approach, ring: .headsUp, maneuverIndex: 5,
                                turn: .right, distanceMeters: 385, road: "Marietta Rd")
    e.continues = true
    check(actions(e) == [.post(id: "guidance.turn.5", title: "Right in 400 m",
                               body: "To stay on Marietta Rd", timeSensitive: true)],
          "named road that continues: \(actions(e))")
    e.road = ""
    e.roadClass = "cycleway"
    check(GuidanceNotice.body(e) == "On the path", "unnamed path: \(GuidanceNotice.body(e))")
    e.roadClass = "service"
    check(GuidanceNotice.body(e) == "On the road", "unnamed road: \(GuidanceNotice.body(e))")
    e.continues = false
    check(GuidanceNotice.body(e) == "", "unnamed way joined from another names nothing")
}

// --- detour offers -----------------------------------------------------------

do {
    let reroute = DetourNoticeOffer(turnAround: false, label: "Re-route: +0.3 mi")
    let farther = DetourNoticeOffer(turnAround: false, label: "Re-route: +0.4 mi")
    let back = DetourNoticeOffer(turnAround: true, label: "Turn around: 410 ft back")
    let id = GuidanceNotice.offRouteID

    check(GuidanceNotice.detourActions(from: nil, to: nil, onRoute: false).isEmpty,
          "no offer either side changes nothing")
    check(GuidanceNotice.detourActions(from: reroute, to: reroute, onRoute: false).isEmpty,
          "the same offer is not posted again")
    check(GuidanceNotice.detourActions(from: nil, to: reroute, onRoute: false)
          == [.post(id: id, title: "Off route", body: "Re-route: +0.3 mi", timeSensitive: true,
                    category: GuidanceNotice.detourRerouteCategory, alerts: true)],
          "a new offer replaces the off-route notice, with its action, and alerts")
    check(GuidanceNotice.detourActions(from: reroute, to: farther, onRoute: false)
          == [.post(id: id, title: "Off route", body: "Re-route: +0.4 mi", timeSensitive: true,
                    category: GuidanceNotice.detourRerouteCategory, alerts: false)],
          "a new distance on the same kind is silent")
    check(GuidanceNotice.detourActions(from: reroute, to: back, onRoute: false)
          == [.post(id: id, title: "Off route", body: "Turn around: 410 ft back",
                    timeSensitive: true, category: GuidanceNotice.detourTurnAroundCategory,
                    alerts: true)],
          "re-route to turn around alerts")
    check(GuidanceNotice.detourActions(from: back, to: nil, onRoute: false)
          == [.post(id: id, title: "Off route", body: GuidanceNotice.offRouteBody,
                    timeSensitive: true, alerts: false)],
          "an offer withdrawn off route reverts to plain off route, silently")
    check(GuidanceNotice.detourActions(from: back, to: nil, onRoute: true)
          == [.remove(ids: [id])], "rejoining removes the notice")
}

for id in [GuidanceNotice.detourRerouteCategory, GuidanceNotice.detourTurnAroundCategory,
           GuidanceNotice.detourActionID] {
    check(id.hasPrefix(GuidanceNotice.prefix), "\(id) carries the prefix")
}

for id in [GuidanceNotice.turnID(0), GuidanceNotice.offRouteID, GuidanceNotice.arrivedID,
           GuidanceNotice.waypointID, GuidanceNotice.offCourseID] {
    check(id.hasPrefix(GuidanceNotice.prefix), "\(id) carries the prefix")
}

print("\(checks - failures)/\(checks) checks passed")
exit(failures == 0 ? 0 : 1)
