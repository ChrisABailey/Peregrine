// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RideActivity.swift — starts, updates and ends the ride's Live Activity.
//
// One activity per guided ride. `RideActivityPolicy` decides what it shows and
// when an update is worth sending; this applies that through ActivityKit.
// Updates are local, from the app, which the background location mode keeps
// running for the length of the ride; there is no push token.

import ActivityKit
import Foundation

@MainActor
final class RideActivity {
    private var activity: Activity<RideActivityAttributes>?
    private var shown: RideActivityContent?
    private var shownAt = Date.distantPast

    /// Ends activities left running by an earlier process, which can no
    /// longer update them. An arrived card has already ended and is kept.
    func endStale() {
        for stale in Activity<RideActivityAttributes>.activities
            where stale.activityState == .active {
            Task { await stale.end(nil, dismissalPolicy: .immediate) }
        }
    }

    /// Shows `content`, starting the activity if there is none. A start only
    /// succeeds with the app in the foreground; a failed one is retried on
    /// the next call.
    func show(_ content: RideActivityContent, now: Date = Date()) {
        if activity == nil {
            guard ActivityAuthorizationInfo().areActivitiesEnabled else { return }
            dismissOthers()
            activity = try? Activity.request(
                attributes: RideActivityAttributes(),
                content: ActivityContent(state: content, staleDate: nil),
                pushType: nil)
            if activity != nil {
                shown = content
                shownAt = now
            }
            return
        }
        guard RideActivityPolicy.needsUpdate(from: shown, to: content,
                                             sinceLast: now.timeIntervalSince(shownAt)),
              let activity else { return }
        shown = content
        shownAt = now
        Task { await activity.update(ActivityContent(state: content, staleDate: nil)) }
    }

    /// Shows the arrived card and ends the activity, leaving the card on the
    /// lock screen for the system's default four hours or until dismissed.
    func arrive() {
        guard let activity else { return }
        self.activity = nil
        shown = nil
        let content = ActivityContent(state: RideActivityPolicy.arrived, staleDate: nil)
        Task { await activity.end(content, dismissalPolicy: .default) }
    }

    /// Removes the card at once: GPS mode left, or the route replaced.
    func end() {
        guard let activity else { return }
        self.activity = nil
        shown = nil
        Task { await activity.end(nil, dismissalPolicy: .immediate) }
    }

    /// A new ride replaces a previous ride's arrived card rather than
    /// stacking under it.
    private func dismissOthers() {
        for other in Activity<RideActivityAttributes>.activities {
            Task { await other.end(nil, dismissalPolicy: .immediate) }
        }
    }
}
