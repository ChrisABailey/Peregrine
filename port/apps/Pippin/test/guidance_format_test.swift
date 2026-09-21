// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for DistanceUnits.countdown (guidance plan GD3) — and for the
// distance() it deliberately differs from, so the difference is pinned rather
// than assumed.

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

// Metric: metres below a kilometre, kilometres above.
expect(km.countdown(20), "20 m", "metric 20 m")
expect(km.countdown(56), "60 m", "metric rounds to 10 below a hundred")
expect(km.countdown(210), "200 m", "metric rounds to 50 above a hundred")
expect(km.countdown(224), "200 m", "metric rounds down to 50")
expect(km.countdown(226), "250 m", "metric rounds up to 50")
expect(km.countdown(950), "950 m", "metric stays in metres under a kilometre")
expect(km.countdown(1500), "1.5 km", "metric kilometres above one")

// Rounding happens before the unit is chosen, so nothing ever reads 1000 m.
expect(km.countdown(995), "1.0 km", "995 m rounds up into kilometres")
// Just under a mile is still yards, rounded down: 1757 yd is not a mile and
// must not be written as one.
expect(mi.countdown(1607), "1750 yd", "just under a mile stays in yards")

// Imperial: YARDS below a mile, not feet.
expect(mi.countdown(20), "20 yd", "imperial small unit is the yard")
expect(mi.countdown(183), "200 yd", "200 yards is 182.88 m")
expect(mi.countdown(91.44), "100 yd", "100 yards exactly")
expect(mi.countdown(1600), "1750 yd", "still yards just under a mile")
expect(mi.countdown(1609.344), "1.0 mi", "a statute mile exactly")
expect(mi.countdown(3218.688), "2.0 mi", "two miles")

// Inside the junction there is no distance left to give, in either unit.
expect(km.countdown(9), "Now", "metric at the junction")
expect(mi.countdown(9), "Now", "imperial at the junction")
expect(km.countdown(0), "Now", "metric on top of it")
expect(km.countdown(11), "10 m", "...and just outside it is a distance again")

// The ride bar's own formatter is unchanged: it still uses feet, and the
// countdown's yards are a second convention on purpose rather than by drift.
expect(mi.distance(20), "66 ft", "the ride bar still reads in feet")
expect(km.distance(950), "950 m", "the ride bar's metric is unchanged")
expect(mi.distance(1609.344), "1.0 mi", "the ride bar's miles are unchanged")

// Speed is untouched by any of this.
expect(km.speed(3.5), "13 km/h", "metric speed")
expect(mi.speed(3.5), "8 mph", "imperial speed")

if failures > 0 {
    FileHandle.standardError.write("\(failures) failure(s)\n".data(using: .utf8)!)
    exit(1)
}
print("guidance_format_test: ok")
