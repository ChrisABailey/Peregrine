// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for TideText, the tide card's formatting.

import Foundation

var failures = 0

func expect<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    if got != want {
        FileHandle.standardError.write("FAIL \(what): got \"\(got)\", want \"\(want)\"\n".data(using: .utf8)!)
        failures += 1
    }
}

/// ICU puts a narrow no-break space before "am"/"pm"; compare it as a space.
func expect(_ got: String, _ want: String, _ what: String) {
    expect(got.replacingOccurrences(of: "\u{202F}", with: " "), want as String?, what)
}

func expect(_ got: String, _ want: String?, _ what: String) {
    if got != want {
        FileHandle.standardError.write("FAIL \(what): got \"\(got)\", want \"\(want ?? "nil")\"\n".data(using: .utf8)!)
        failures += 1
    }
}

let zone = TimeZone(identifier: "America/New_York")!
let us = Locale(identifier: "en_US")
let gb = Locale(identifier: "en_GB")

// 2026-09-26 14:14 EDT = 18:14 UTC.
let t1414 = 1_790_446_440.0
let noon = t1414 - 2 * 3600 - 14 * 60

// Heights.
expect(TideText.height(0.06096, units: .miles), "0.2 ft", "feet to a tenth")
expect(TideText.height(0.456, units: .kilometres), "0.46 m", "metres to a hundredth")
expect(TideText.height(-0.1, units: .miles), "-0.3 ft", "below datum is negative")
expect(TideText.chartValue(0.3048, units: .miles), 1.0, "chart in feet")
expect(TideText.chartValue(0.3048, units: .kilometres), 0.3048, "chart in metres")

// Clock and extremes, in the device's zone.
expect(TideText.clock(t1414, now: noon, zone: zone, locale: us), "2:14 pm", "12-hour clock, lower case")
expect(TideText.clock(t1414, now: noon, zone: zone, locale: gb), "14:14", "24-hour locale")
expect(TideText.clock(t1414 + 86_400, now: noon, zone: zone, locale: us), "Sun 2:14 pm",
       "another day gets its weekday")
expect(TideText.extreme(isHigh: false, time: t1414, heightMeters: 0.06096, now: noon,
                        units: .miles, zone: zone, locale: us),
       "Low 2:14 pm, 0.2 ft", "the plan's example")
expect(TideText.now(heightMeters: 0.3658, rising: true, units: .miles),
       "1.2 ft and rising", "now line")

// The beach line.
let horizon = noon + 86_400
expect(TideText.beach(now: noon, windows: [(noon - 600, t1414)], horizon: horizon,
                      zone: zone, locale: us),
       "The beach should be rideable until about 2:14 pm", "inside a window")
expect(TideText.beach(now: noon, windows: [(t1414, t1414 + 3600)], horizon: horizon,
                      zone: zone, locale: us),
       "The beach should be rideable about 2:14 pm to 3:14 pm", "before a window")
expect(TideText.beach(now: noon, windows: [(t1414, horizon)], horizon: horizon,
                      zone: zone, locale: us),
       "The beach should be rideable from about 2:14 pm", "window cut by the horizon")
expect(TideText.beach(now: noon, windows: [], horizon: horizon, zone: zone, locale: us),
       "The beach is unlikely to be rideable in the next day", "no window")
expect(TideText.beach(now: noon, windows: [(noon - 600, t1414)], horizon: horizon,
                      zone: zone, locale: us, activity: .walk),
       "The beach has the best walking until 2:14 pm", "walking, inside a window")
expect(TideText.beach(now: noon, windows: [], horizon: horizon, zone: zone, locale: us,
                      activity: .walk),
       "The beach has no easy walking in the next day", "walking, no window")

// Coverage: the table ends at 2031-01-01T00:00Z, which is Dec 31 in New York
// at 19:00, so the last covered day is Dec 31 either way.
let end = 1_924_992_000.0
expect(TideText.coverage(now: end - 61 * 86_400, validUntil: end, zone: zone, locale: us),
       .current, "61 days out")
expect(TideText.coverage(now: end - 59 * 86_400, validUntil: end, zone: zone, locale: us),
       .endingSoon("Dec 31, 2030"), "59 days out")
expect(TideText.coverage(now: end, validUntil: end, zone: zone, locale: us),
       .ended("Dec 31, 2030"), "at the end")
expect(TideText.coverageNotice(.current) == nil, true, "no notice while current")

// The beach on a route. 1.4630 m is 4.8 ft.
expect(TideText.beachNotUsed(.high, enterHeightMeters: 1.4630, passableFrom: t1414,
                             coveredAt: .nan, now: noon, units: .miles,
                             zone: zone, locale: us),
       "Beach not used — the tide is high (4.8 ft). Rideable again from 2:14 pm.", "high")
expect(TideText.beachNotUsed(.high, enterHeightMeters: 1.4630, passableFrom: .nan,
                             coveredAt: .nan, now: noon, units: .miles,
                             zone: zone, locale: us),
       "Beach not used — the tide is high (4.8 ft).", "high, no window")
expect(TideText.beachNotUsed(.rising, enterHeightMeters: 0.3, passableFrom: .nan,
                             coveredAt: t1414, now: noon, units: .miles,
                             zone: zone, locale: us),
       "Beach not used — the tide is rising and covers the beach by 2:14 pm.", "rising")
expect(TideText.beachNotUsed(.noTable, enterHeightMeters: .nan, passableFrom: .nan,
                             coveredAt: .nan, now: noon, units: .miles,
                             zone: zone, locale: us),
       "Beach not used — the tide table has ended.", "no table")
expect(TideText.beachVerdict(.marginal, peakMeters: 0.9449, peakAt: t1414, hasTable: true,
                             now: noon, units: .miles, zone: zone, locale: us),
       "The tide will be 3.1 ft at 2:14 pm — soft sand likely.", "marginal")
expect(TideText.beachVerdict(.marginal, peakMeters: 1.5, peakAt: t1414, hasTable: true,
                             now: noon, units: .miles, zone: zone, locale: us, walking: true),
       "The tide will be 4.9 ft at 2:14 pm — softer sand and harder going.", "walking marginal")
expect(TideText.beachVerdict(.good, peakMeters: 0.2, peakAt: t1414, hasTable: true,
                             now: noon, units: .kilometres, zone: zone, locale: us),
       "The tide will be 0.20 m at 2:14 pm — firm sand.", "good")
expect(TideText.nextTide(isHigh: true, time: t1414, beach: "The beach has the best walking until 1:30 pm",
                         now: noon, zone: zone, locale: us),
       "High tide is 2:14 pm, the beach has the best walking until 1:30 pm.", "next tide")
expect(TideText.beachVerdict(.unknown, peakMeters: .nan, peakAt: .nan, hasTable: false,
                             now: noon, units: .miles, zone: zone, locale: us),
       "No tide table for this area. Check the water before riding the sand.", "no table")
expect(TideText.beachVerdict(.unknown, peakMeters: .nan, peakAt: .nan, hasTable: true,
                             now: noon, units: .miles, zone: zone, locale: us)
           .hasPrefix("The tide table has ended"), true, "table ended")
expect(TideText.beachShare(4200, units: .kilometres), "includes 4.2 km of beach", "share")

// Sunrise and sunset.
let sunEvents: [(time: TimeInterval, isRise: Bool)] =
    [(noon - 5 * 3600, true), (t1414 + 5 * 3600, false), (t1414 + 17 * 3600, true)]
expect(TideText.sun(sunEvents, now: noon, zone: zone, locale: us) ?? "",
       "Sunset 7:14 pm · Sunrise Sun 7:14 am", "next sunset, then tomorrow's sunrise")
expect(TideText.sun([], now: noon, zone: zone, locale: us) == nil, true, "polar: no sun line")
let spanStart = noon - 3 * 3600
let spanEnd = noon + 24 * 3600
let dark = TideText.nights([(spanStart + 3600, true), (spanStart + 13 * 3600, false),
                            (spanStart + 24 * 3600, true), (spanStart + 26 * 3600, false)],
                           from: spanStart, to: spanEnd)
expect(dark.count, 3, "three nights: before dawn, overnight, after dusk")
expect(dark.first.map { $0.begin == spanStart && $0.end == spanStart + 3600 }, true,
       "a span opening on a sunrise starts dark")
expect(dark.last.map { $0.begin == spanStart + 26 * 3600 && $0.end == spanEnd }, true,
       "a span closing after a sunset ends dark")
expect(TideText.nights([], from: spanStart, to: spanEnd).isEmpty, true, "no events, no nights")

if failures > 0 {
    print("tide_text_test: \(failures) failure(s)")
    exit(1)
}
print("tide_text_test: all passed")
