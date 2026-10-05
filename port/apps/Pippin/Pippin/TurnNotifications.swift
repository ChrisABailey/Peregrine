// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// TurnNotifications.swift — guidance events as local notifications, for a
// rider whose phone is in a pocket.
//
// Background only: `TurnAlerts` plays the foreground's chimes and haptics,
// and with no notification-centre delegate iOS does not present a
// notification while the app is frontmost anyway. The sound and vibration
// are the rider's per-app notification settings. A paired Apple Watch gets
// the notification from the system when the phone is locked.
//
// Turn and off-route notices are time sensitive, which needs the
// `com.apple.developer.usernotifications.time-sensitive` entitlement;
// without it a Focus mode holds them.

import PippinKit
import UserNotifications

/// Posts and withdraws the guidance notifications. One per app, driven from
/// `MapModel`.
@MainActor
final class TurnNotifications {
    private let center = UNUserNotificationCenter.current()

    /// Identifiers posted and not yet removed. Removal goes by this list
    /// rather than by querying the centre, whose answer arrives later than
    /// the post that follows a clear.
    private var posted: Set<String> = []

    /// Asks for permission. iOS shows the prompt once, the first time; every
    /// later call returns the stored answer without asking.
    func requestPermission() {
        center.requestAuthorization(options: [.alert, .sound]) { _, _ in }
    }

    /// Applies what each event says to the delivered notifications.
    func post(_ events: [PPGuidanceEvent], units: DistanceUnits) {
        for event in events {
            for action in GuidanceNotice.actions(for: Self.notice(event), units: units) {
                apply(action)
            }
        }
    }

    /// Withdraws every guidance notification: the app is back on screen, or
    /// the ride is over, and they name corners already reached.
    func clear() {
        apply(.removeAll)
    }

    private func apply(_ action: GuidanceNoticeAction) {
        switch action {
        case let .post(id, title, body, timeSensitive):
            let content = UNMutableNotificationContent()
            content.title = title
            content.body = body
            content.sound = .default
            content.interruptionLevel = timeSensitive ? .timeSensitive : .active
            // A nil trigger delivers now. Reusing an identifier replaces the
            // delivered notification and alerts again.
            center.add(UNNotificationRequest(identifier: id, content: content, trigger: nil))
            posted.insert(id)
        case let .remove(ids):
            center.removeDeliveredNotifications(withIdentifiers: ids)
            posted.subtract(ids)
        case .removeAll:
            guard !posted.isEmpty else { return }
            center.removeDeliveredNotifications(withIdentifiers: Array(posted))
            posted.removeAll()
        }
    }

    private static func notice(_ event: PPGuidanceEvent) -> GuidanceNoticeEvent {
        GuidanceNoticeEvent(kind: kind(event.kind),
                            ring: ring(event.ring),
                            maneuverIndex: event.maneuverIndex,
                            turn: turn(event.maneuver),
                            distanceMeters: event.distanceMeters,
                            road: event.road)
    }

    private static func kind(_ k: PPGuidanceEventKind) -> GuidanceNoticeEvent.Kind {
        switch k {
        case .approach: return .approach
        case .passed: return .passed
        case .offRoute: return .offRoute
        case .rejoined: return .rejoined
        case .arrived: return .arrived
        @unknown default: return .passed
        }
    }

    private static func ring(_ r: PPGuidanceRing) -> GuidanceNoticeEvent.Ring {
        switch r {
        case .none: return .none
        case .headsUp: return .headsUp
        case .actNow: return .actNow
        case .at: return .at
        @unknown default: return .none
        }
    }

    /// The Foundation-only turn for a PippinKit maneuver.
    static func turn(_ m: PPManeuver) -> GuidanceNoticeEvent.Turn {
        switch m {
        case .depart: return .depart
        case .straight: return .straight
        case .slightLeft: return .slightLeft
        case .left: return .left
        case .sharpLeft: return .sharpLeft
        case .slightRight: return .slightRight
        case .right: return .right
        case .sharpRight: return .sharpRight
        case .uTurn: return .uTurn
        case .arrive: return .arrive
        @unknown default: return .straight
        }
    }
}
