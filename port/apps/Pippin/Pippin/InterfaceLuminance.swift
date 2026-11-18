// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// InterfaceLuminance.swift — the chart's dusk and night colour tables applied
// to the controls, cards and sheets around the chart, so nothing on screen is
// brighter than the chart's own text.

import SwiftUI
import UIKit

/// The interface luminance in force. A shared object because sheets are
/// separate presentations and follow it from outside the map screen's tree.
@MainActor
final class InterfaceLuminance: ObservableObject {
    static let shared = InterfaceLuminance()

    @Published private(set) var level: ChartLuminance
    private var holdsDark: Bool

    /// Starts from the stored palette, so the first frame is already dimmed.
    private init() {
        let palette = ChartSettings.load(from: .standard).palette
        let dark = UITraitCollection.current.userInterfaceStyle == .dark
        level = Self.level(for: palette, darkMode: dark)
        holdsDark = Self.dims && palette.forcesDarkAppearance
    }

    /// Only the chart app has a palette to follow.
    private static let dims = AppFlavor.current == .osprey

    private static func level(for palette: ChartPalette, darkMode: Bool) -> ChartLuminance {
        dims ? ChartLuminance.resolve(palette: palette, darkMode: darkMode) : .day
    }

    /// Follows a palette or appearance change. `darkMode` is consulted only
    /// for the automatic palette, under which the appearance is not held.
    func follow(palette: ChartPalette, darkMode: Bool) {
        let next = Self.level(for: palette, darkMode: darkMode)
        if next != level { level = next }
        holdsDark = Self.dims && palette.forcesDarkAppearance
        applyToWindows()
    }

    /// Holds every window dark under dusk and night, which reaches what the
    /// system presents (alerts, menus, the share sheet, the keyboard), and
    /// gives those a dim tint in place of the system blue.
    func applyToWindows() {
        guard Self.dims else { return }
        for scene in UIApplication.shared.connectedScenes {
            guard let scene = scene as? UIWindowScene else { continue }
            for window in scene.windows {
                window.overrideUserInterfaceStyle = holdsDark ? .dark : .unspecified
                window.tintColor = level.systemTint
            }
        }
    }
}

extension ChartLuminance {
    /// The colour every drawn pixel is multiplied by; white for day.
    var multiplier: Color {
        guard let c = ceiling else { return .white }
        return Color(red: Double(c.red) / 255, green: Double(c.green) / 255,
                     blue: Double(c.blue) / 255)
    }

    /// The accent inside a dimmed surface, before `multiplier`: amber, which
    /// stays distinct from grey text where the system blue goes to black.
    var accent: Color? {
        self == .day ? nil : Color(red: 1.0, green: 0.62, blue: 0.2)
    }

    /// The tint for system-presented views, which are not multiplied.
    var systemTint: UIColor? {
        switch self {
        case .day: return nil
        case .dusk: return UIColor(red: 0.55, green: 0.36, blue: 0.14, alpha: 1)
        case .night: return UIColor(red: 0.37, green: 0.24, blue: 0.09, alpha: 1)
        }
    }
}

/// Scales a surface down to the chart palette's luminance and swaps its
/// accent. With `keepsIdentity` the view tree is the same at every level, so
/// a sheet keeps its state when the palette changes under it; without, day
/// carries no filter at all, for layers composited on every map frame.
private struct ChartDimmed: ViewModifier {
    let keepsIdentity: Bool
    @ObservedObject private var luminance = InterfaceLuminance.shared

    init(keepsIdentity: Bool) { self.keepsIdentity = keepsIdentity }

    func body(content: Content) -> some View {
        let level = luminance.level
        if keepsIdentity || level != .day {
            content
                .tint(level.accent)
                // `Color.accentColor` reads this, not `tint`.
                .accentColor(level.accent)
                .colorMultiply(level.multiplier)
        } else {
            content
        }
    }
}

/// `ChartDimmed` for a sheet, which also replaces the sheet's translucent
/// backing and drag indicator: both are the system's and are brighter than
/// the dimmed text over a night chart.
private struct ChartDimmedSheet: ViewModifier {
    @ObservedObject private var luminance = InterfaceLuminance.shared

    func body(content: Content) -> some View {
        let dim = luminance.level != .day
        content
            .background((dim ? Color.black : Color.clear).ignoresSafeArea())
            .modifier(ChartDimmed(keepsIdentity: true))
            .presentationDragIndicator(dim ? .hidden : .automatic)
    }
}

/// A form picker that opens a system pop-up by day and a pushed list under a
/// dusk or night palette. The pop-up is the system's and cannot be dimmed; the
/// pushed list is part of the sheet.
private struct ChartDimmedPicker: ViewModifier {
    @ObservedObject private var luminance = InterfaceLuminance.shared

    func body(content: Content) -> some View {
        if luminance.level == .day {
            content
        } else {
            content.pickerStyle(.navigationLink)
        }
    }
}

extension View {
    /// For a menu-style picker inside a navigation stack. See `ChartDimmedPicker`.
    func chartDimmedPicker() -> some View {
        modifier(ChartDimmedPicker())
    }

    /// Dims a sheet's content to the chart palette. Applied to the root of
    /// every sheet.
    func chartDimmedSheet() -> some View {
        modifier(ChartDimmedSheet())
    }

    /// Dims a layer drawn over the map: controls, cards, and the overlay
    /// pictures that are not part of the chart's own palette.
    func chartDimmed() -> some View {
        modifier(ChartDimmed(keepsIdentity: false))
    }
}
