// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// GuidanceNotice.swift — what a guidance event does to the notification list
// while the app is in the background.
//
// One notification per maneuver: its identifier is the maneuver's index, so
// act-now replaces heads-up rather than stacking under it, and passing the
// corner removes it. Off route has an identifier of its own that rejoining
// removes. Arrival clears everything and leaves only itself.
//
// Foundation-only, so the table is tested on the mac
// (`ctest -R guidance_notice`). `TurnNotifications` applies the result.

/// A guidance event with the PippinKit types replaced by Swift ones.
struct GuidanceNoticeEvent: Equatable {
    enum Kind { case approach, passed, offRoute, rejoined, arrived }
    enum Ring { case none, headsUp, actNow, at }
    enum Turn {
        case depart, straight, slightLeft, left, sharpLeft
        case slightRight, right, sharpRight, uTurn, arrive
    }

    var kind: Kind
    var ring: Ring = .none
    var maneuverIndex: Int = 0
    var turn: Turn = .straight
    /// Along-route metres to the maneuver when the event fired.
    var distanceMeters: Double = 0
    /// The road being joined; empty for an unnamed way.
    var road: String = ""
}

/// One change to the delivered notifications.
enum GuidanceNoticeAction: Equatable {
    /// Posts, or replaces the notification with the same identifier.
    case post(id: String, title: String, body: String, timeSensitive: Bool)
    case remove(ids: [String])
    /// Removes every guidance notification.
    case removeAll
}

enum GuidanceNotice {
    /// Every guidance identifier starts with this, which is how `removeAll`
    /// leaves the app's other notifications alone.
    static let prefix = "guidance."
    static let offRouteID = prefix + "offroute"
    static let arrivedID = prefix + "arrived"

    static func turnID(_ index: Int) -> String { prefix + "turn.\(index)" }

    /// The changes one event makes, in order.
    static func actions(for event: GuidanceNoticeEvent,
                        units: DistanceUnits) -> [GuidanceNoticeAction] {
        switch event.kind {
        case .approach:
            switch event.ring {
            case .headsUp, .actNow:
                return [.post(id: turnID(event.maneuverIndex),
                              title: title(event, units: units),
                              body: body(event),
                              timeSensitive: true)]
            case .at, .none:
                // The junction ring is felt and not heard in the foreground;
                // a third notification for one corner would be the same nag
                // on a wrist.
                return []
            }
        case .passed:
            return [.remove(ids: [turnID(event.maneuverIndex)])]
        case .offRoute:
            return [.post(id: offRouteID, title: "Off route",
                          body: "Turn alerts resume when you rejoin the route.",
                          timeSensitive: true)]
        case .rejoined:
            return [.remove(ids: [offRouteID])]
        case .arrived:
            return [.removeAll,
                    .post(id: arrivedID, title: "Arrived",
                          body: "End of the planned route.",
                          timeSensitive: false)]
        }
    }

    /// "Left in 250 m". An approach to the destination reads "Arriving in".
    static func title(_ event: GuidanceNoticeEvent, units: DistanceUnits) -> String {
        let countdown = units.countdown(event.distanceMeters)
        if countdown == "Now" { return "\(phrase(event.turn)) now" }
        return "\(phrase(event.turn)) in \(countdown)"
    }

    static func body(_ event: GuidanceNoticeEvent) -> String {
        if event.turn == .arrive { return "End of the planned route." }
        return event.road.isEmpty ? "" : "Onto \(event.road)"
    }

    static func phrase(_ turn: GuidanceNoticeEvent.Turn) -> String {
        switch turn {
        case .depart: return "Start"
        case .straight: return "Continue straight"
        case .slightLeft: return "Bear left"
        case .left: return "Left"
        case .sharpLeft: return "Sharp left"
        case .slightRight: return "Bear right"
        case .right: return "Right"
        case .sharpRight: return "Sharp right"
        case .uTurn: return "U-turn"
        case .arrive: return "Arriving"
        }
    }
}
