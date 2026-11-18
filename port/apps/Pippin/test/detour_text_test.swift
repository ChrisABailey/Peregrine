// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for DetourText.button.

import Foundation

var failures = 0

func expect(_ got: String, _ want: String, _ what: String) {
    if got != want {
        FileHandle.standardError.write("FAIL \(what): got \"\(got)\", want \"\(want)\"\n".data(using: .utf8)!)
        failures += 1
    }
}

let km = DistanceUnits.kilometres
let mi = DistanceUnits.miles

expect(DetourText.button(turnAround: false, extraMeters: 480, backMeters: 0, units: mi),
       "Re-route: +0.3 mi", "longer, miles")
expect(DetourText.button(turnAround: false, extraMeters: 58, backMeters: 143, units: km),
       "Re-route: +58 m", "a little longer, metric")
expect(DetourText.button(turnAround: false, extraMeters: -425, backMeters: 125, units: km),
       "Re-route: 425 m shorter", "shorter")
expect(DetourText.button(turnAround: false, extraMeters: 12, backMeters: 0, units: mi),
       "Re-route: same distance", "within the margin")
expect(DetourText.button(turnAround: true, extraMeters: 41, backMeters: 76.2, units: mi),
       "Turn around: 250 ft back", "turn around, feet")
expect(DetourText.button(turnAround: true, extraMeters: 0, backMeters: 177, units: km),
       "Turn around: 177 m back", "turn around, metres")

if failures > 0 { exit(1) }
print("detour_text: all passed")
