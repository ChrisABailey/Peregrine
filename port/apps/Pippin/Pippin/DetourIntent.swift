// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// DetourIntent.swift — the Live Activity's detour button.
//
// Compiled into the app and the PippinActivity extension, which draws the
// button. A `LiveActivityIntent` runs in the app's process, so `perform`
// reaches the handler `MapModel` installs; in the extension it is never run.

import AppIntents

/// Applies the detour on offer to an off-route rider, from the lock screen.
struct ApplyDetourIntent: LiveActivityIntent {
    static let title: LocalizedStringResource = "Apply Detour"
    static let isDiscoverable = false

    func perform() async throws -> some IntentResult {
        await DetourAction.run()
        return .result()
    }
}

/// Where the lock-screen detour actions (Live Activity button and
/// notification action) reach the ride.
@MainActor
enum DetourAction {
    /// Applies the offer on the current ride. Set by `MapModel`; nil in the
    /// widget extension.
    static var apply: (() -> Void)?

    static func run() { apply?() }
}
