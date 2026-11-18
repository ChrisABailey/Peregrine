// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// chart_settings_test.swift — chart settings defaults, depth choices and
// persistence, tested on the mac (`ctest -R pippin_chart_settings`).

import Foundation

var failures = 0

func expect<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    if got != want {
        failures += 1
        print("FAIL \(what): got \(got), want \(want)")
    }
}

// Defaults: a 15 ft contour and safety depth, everything drawn, automatic palette.
let d = ChartSettings()
expect(DepthUnit.feet.label(metres: d.safetyContour), "15 ft", "default contour")
expect(DepthUnit.feet.nearestChoice(to: d.safetyContour), d.safetyContour, "default contour is a choice")
expect(d.safetyDepth, d.safetyContour, "default safety depth equals the contour")
expect(d.category, .all, "default category draws the chart as before")
expect(d.palette, .automatic, "default palette follows dark mode")
expect(d.hideNavaidNames, true, "buoy and beacon names hidden by default")

// Labels and choices.
expect(DepthUnit.metres.label(metres: 4.6), "5 m", "metres round")
expect(DepthUnit.feet.label(metres: 3.048), "10 ft", "feet round")
expect(DepthUnit.metres.nearestChoice(to: 4.2), 5, "nearest metre choice")
expect(DepthUnit.feet.depthChoices.count, 7, "seven feet choices")
for unit in DepthUnit.allCases {
    let c = unit.depthChoices
    expect(c, c.sorted(), "\(unit) choices ascend")
    for m in c { expect(unit.nearestChoice(to: m), m, "\(unit) choice is its own nearest") }
}

// Changing units snaps both depths into the new list.
var s = ChartSettings()
s.depthUnit = .metres
s = s.converted(to: .metres)
expect(s.safetyContour, 5, "15 ft becomes 5 m")
expect(s.safetyDepth, 5, "15 ft safety depth becomes 5 m")
s.safetyDepth = 5
let f = s.converted(to: .feet)
expect(DepthUnit.feet.label(metres: f.safetyDepth), "15 ft", "5 m becomes 15 ft")

// Persistence round trip, and an unreadable store falls back to defaults.
let suite = "pippin.chart_settings_test.\(getpid())"
let defaults = UserDefaults(suiteName: suite)!
expect(ChartSettings.load(from: defaults), ChartSettings(), "empty store loads defaults")
var saved = ChartSettings().converted(to: .metres)
saved.safetyContour = 10
saved.safetyDepth = 3
saved.category = .base
saved.palette = .dusk
saved.hideNavaidNames = false
saved.save(to: defaults)
expect(ChartSettings.load(from: defaults), saved, "round trip")
defaults.set(99, forKey: "PPChartCategory")
defaults.set("fathoms", forKey: "PPChartDepthUnit")
let odd = ChartSettings.load(from: defaults)
expect(odd.category, .all, "unknown category falls back")
expect(odd.depthUnit, .feet, "unknown unit falls back")
expect(DepthUnit.feet.label(metres: odd.safetyContour), "30 ft", "stored 10 m snaps into feet")
defaults.removePersistentDomain(forName: suite)

// Interface luminance follows the palette; automatic follows Dark Mode.
expect(ChartLuminance.resolve(palette: .automatic, darkMode: false), .day, "automatic, light")
expect(ChartLuminance.resolve(palette: .automatic, darkMode: true), .night, "automatic, dark")
expect(ChartLuminance.resolve(palette: .day, darkMode: true), .day, "day stays day in Dark Mode")
expect(ChartLuminance.resolve(palette: .dusk, darkMode: false), .dusk, "dusk in Light Mode")
expect(ChartLuminance.resolve(palette: .night, darkMode: false), .night, "night in Light Mode")
expect(ChartLuminance.day.ceiling == nil, true, "day is not dimmed")
expect(ChartPalette.allCases.filter(\.forcesDarkAppearance), [.dusk, .night],
       "only an explicit dusk or night overrides the system appearance")
for level in [ChartLuminance.dusk, .night] {
    let c = level.ceiling!
    expect(max(c.red, c.green, c.blue) <= 80, true, "\(level) ceiling is low")
}
expect(ChartLuminance.night.ceiling!.green < ChartLuminance.dusk.ceiling!.green, true,
       "night is dimmer than dusk")

if failures > 0 {
    print("\(failures) failure(s)")
    exit(1)
}
print("chart settings: all checks passed")
