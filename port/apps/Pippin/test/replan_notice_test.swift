// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for ReplanNotice.text.

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

expect(ReplanNotice.text(previousMeters: 5000, meters: 5400, avoidedRoad: "Kiawah Island Parkway",
                         lineChanged: true, units: km),
       "Route re-planned with your preferences: 400 m longer, now avoids Kiawah Island Parkway.",
       "longer with a road")
expect(ReplanNotice.text(previousMeters: 8000, meters: 6000, avoidedRoad: "",
                         lineChanged: true, units: mi),
       "Route re-planned with your preferences: 1.2 mi shorter.",
       "shorter, no road")
expect(ReplanNotice.text(previousMeters: 5000, meters: 5004, avoidedRoad: "Flyaway Drive",
                         lineChanged: true, units: km),
       "Route re-planned with your preferences: about the same length, now avoids Flyaway Drive.",
       "same length")
expect(ReplanNotice.text(previousMeters: 5000, meters: 5000, avoidedRoad: "",
                         lineChanged: false, units: km),
       "Your preferences leave this route as it was.",
       "unchanged line")

if failures > 0 { exit(1) }
print("replan_notice: all passed")
