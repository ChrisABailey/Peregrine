// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ChartSettings.swift — the mariner's chart settings, their persistence and
// the depth choices offered for them. Foundation-only, so it is tested on the
// mac (`ctest -R pippin_chart_settings`).

import Foundation

/// The unit depths are shown and chosen in. Chart data is always metres.
enum DepthUnit: String, CaseIterable, Identifiable {
    case feet
    case metres

    var id: String { rawValue }

    var menuName: String { self == .feet ? "Feet" : "Metres" }

    /// The depths offered for the safety contour and safety depth, in metres,
    /// shallowest first: round numbers in this unit.
    var depthChoices: [Double] {
        switch self {
        case .feet: return [6, 10, 15, 20, 30, 50, 100].map { $0 * 0.3048 }
        case .metres: return [2, 3, 5, 10, 15, 20, 30]
        }
    }

    /// "15 ft", "5 m" for a depth in metres, to the nearest whole unit.
    func label(metres: Double) -> String {
        switch self {
        case .feet: return "\(Int((metres / 0.3048).rounded())) ft"
        case .metres: return "\(Int(metres.rounded())) m"
        }
    }

    /// The offered depth nearest `metres`.
    func nearestChoice(to metres: Double) -> Double {
        depthChoices.min { abs($0 - metres) < abs($1 - metres) } ?? metres
    }
}

/// Which S-52 display categories are drawn. Raw values match `PPChartCategory`.
enum ChartCategory: Int, CaseIterable, Identifiable {
    case base = 0
    case standard = 1
    case all = 2

    var id: Int { rawValue }

    var menuName: String {
        switch self {
        case .base: return "Base"
        case .standard: return "Standard"
        case .all: return "All"
        }
    }
}

/// The chart's colour table. Raw values match `PPChartPalette`.
enum ChartPalette: Int, CaseIterable, Identifiable {
    case automatic = 0
    case day = 1
    case dusk = 2
    case night = 3

    var id: Int { rawValue }

    var menuName: String {
        switch self {
        case .automatic: return "Automatic"
        case .day: return "Day"
        case .dusk: return "Dusk"
        case .night: return "Night"
        }
    }
}

/// The settings the chart settings sheet edits. Depths are metres and are
/// always one of `depthUnit.depthChoices`.
struct ChartSettings: Equatable {
    var safetyContour: Double
    var safetyDepth: Double
    var category: ChartCategory = .all
    var palette: ChartPalette = .automatic
    var depthUnit: DepthUnit = .feet
    /// Hides the names of buoys and beacons, which crowd a busy harbour.
    var hideNavaidNames = true

    /// A 15 ft safety contour and safety depth, for small craft.
    init() {
        safetyContour = 15 * 0.3048
        safetyDepth = safetyContour
    }

    /// The settings with depths snapped to the offered choices for `unit`.
    func converted(to unit: DepthUnit) -> ChartSettings {
        var s = self
        s.depthUnit = unit
        s.safetyContour = unit.nearestChoice(to: safetyContour)
        s.safetyDepth = unit.nearestChoice(to: safetyDepth)
        return s
    }

    private enum Key {
        static let safetyContour = "PPChartSafetyContour"
        static let safetyDepth = "PPChartSafetyDepth"
        static let category = "PPChartCategory"
        static let palette = "PPChartPalette"
        static let depthUnit = "PPChartDepthUnit"
        static let hideNavaidNames = "PPChartHideNavaidNames"
    }

    /// The stored settings, with defaults for anything absent or unreadable.
    static func load(from defaults: UserDefaults) -> ChartSettings {
        var s = ChartSettings()
        if let unit = defaults.string(forKey: Key.depthUnit).flatMap(DepthUnit.init(rawValue:)) {
            s.depthUnit = unit
        }
        let contour = defaults.double(forKey: Key.safetyContour)
        if contour > 0 { s.safetyContour = contour }
        let depth = defaults.double(forKey: Key.safetyDepth)
        if depth > 0 { s.safetyDepth = depth }
        if let raw = defaults.object(forKey: Key.category) as? Int,
           let c = ChartCategory(rawValue: raw) { s.category = c }
        if let raw = defaults.object(forKey: Key.palette) as? Int,
           let p = ChartPalette(rawValue: raw) { s.palette = p }
        if let hide = defaults.object(forKey: Key.hideNavaidNames) as? Bool {
            s.hideNavaidNames = hide
        }
        return s.converted(to: s.depthUnit)
    }

    func save(to defaults: UserDefaults) {
        defaults.set(safetyContour, forKey: Key.safetyContour)
        defaults.set(safetyDepth, forKey: Key.safetyDepth)
        defaults.set(category.rawValue, forKey: Key.category)
        defaults.set(palette.rawValue, forKey: Key.palette)
        defaults.set(depthUnit.rawValue, forKey: Key.depthUnit)
        defaults.set(hideNavaidNames, forKey: Key.hideNavaidNames)
    }
}
