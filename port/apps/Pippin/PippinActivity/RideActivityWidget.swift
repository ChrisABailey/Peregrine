// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RideActivityWidget.swift — the ride's Live Activity on the lock screen, the
// Dynamic Island and a paired watch's Smart Stack.
//
// Draws `RideActivityContent` as the app formatted it. The watch uses the
// compact leading and trailing views.

import ActivityKit
import SwiftUI
import WidgetKit

@main
struct PippinActivityBundle: WidgetBundle {
    var body: some Widget {
        RideActivityWidget()
    }
}

struct RideActivityWidget: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: RideActivityAttributes.self) { context in
            LockScreenCard(content: context.state)
                .activityBackgroundTint(Color(red: 0.11, green: 0.15, blue: 0.20))
                .activitySystemActionForegroundColor(.white)
        } dynamicIsland: { context in
            let content = context.state
            return DynamicIsland {
                DynamicIslandExpandedRegion(.leading) {
                    TurnGlyph(content: content, size: 38)
                }
                DynamicIslandExpandedRegion(.center) {
                    CardText(content: content, large: 24)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            } compactLeading: {
                TurnGlyph(content: content, size: 18, showsThen: false)
            } compactTrailing: {
                CompactTrailing(content: content)
            } minimal: {
                TurnGlyph(content: content, size: 16, showsThen: false)
            }
        }
    }
}

/// The lock screen and Always-On card.
private struct LockScreenCard: View {
    let content: RideActivityContent

    var body: some View {
        HStack(spacing: 14) {
            TurnGlyph(content: content, size: 40)
            CardText(content: content, large: 30)
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .foregroundStyle(.white)
    }
}

/// The turn arrow, with the staggered pair's second glyph beside it.
private struct TurnGlyph: View {
    let content: RideActivityContent
    let size: CGFloat
    var showsThen = true

    var body: some View {
        HStack(alignment: .center, spacing: 2) {
            Image(systemName: glyph)
                .font(.system(size: size, weight: .semibold))
                .foregroundStyle(content.phase == .offRoute ? .secondary : .primary)
            if showsThen, content.phase == .approaching, let then = content.thenSymbol {
                Image(systemName: then)
                    .font(.system(size: size * 0.55, weight: .semibold))
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var glyph: String {
        switch content.phase {
        case .approaching: return content.symbol
        case .offRoute: return "point.topleft.down.to.point.bottomright.curvepath"
        case .arrived: return "flag.checkered"
        }
    }
}

/// Distance and time to the turn over the road being joined; or the off-route
/// and arrived messages.
private struct CardText: View {
    let content: RideActivityContent
    let large: CGFloat

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            switch content.phase {
            case .approaching:
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    Text(content.distance)
                        .font(.system(size: large, weight: .semibold, design: .rounded))
                    if let turnAt = content.turnAt {
                        TurnTimer(turnAt: turnAt)
                            .font(.system(size: large * 0.72, weight: .medium, design: .rounded))
                            .foregroundStyle(.secondary)
                    }
                }
                .monospacedDigit()
                if !content.road.isEmpty {
                    Text("Onto \(content.road)")
                        .font(.system(size: 15, weight: .medium, design: .rounded))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            case .offRoute:
                Text("Off route")
                    .font(.system(size: large * 0.75, weight: .semibold, design: .rounded))
                Text("Rejoin to resume")
                    .font(.system(size: 15, weight: .medium, design: .rounded))
                    .foregroundStyle(.secondary)
            case .arrived:
                Text("Arrived")
                    .font(.system(size: large * 0.75, weight: .semibold, design: .rounded))
                Text("GPS and recording stop in the background")
                    .font(.system(size: 15, weight: .medium, design: .rounded))
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
        }
    }
}

/// The compact Dynamic Island's right side: distance only, there is no room
/// for the time.
private struct CompactTrailing: View {
    let content: RideActivityContent

    var body: some View {
        switch content.phase {
        case .approaching:
            Text(content.distance).monospacedDigit().fontWeight(.semibold)
        case .offRoute:
            Text("Off").foregroundStyle(.secondary)
        case .arrived:
            Text("Arrived")
        }
    }
}

/// The time to the turn in whole minutes, recomputed on each content update.
private struct TurnTimer: View {
    let turnAt: Date

    var body: some View {
        Text(RideActivityContent.minutesText(until: turnAt, now: Date()))
    }
}
