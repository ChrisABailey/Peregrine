// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for WindText, the wind's wording on the tide card and route sheet.

import Foundation

var failures = 0

func expect<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    if got != want {
        FileHandle.standardError.write("FAIL \(what): got \"\(got)\", want \"\(want)\"\n".data(using: .utf8)!)
        failures += 1
    }
}

/// ICU puts a narrow no-break space before "am"/"pm"; compare it as a space.
func expectText(_ got: String, _ want: String, _ what: String) {
    expect(got.replacingOccurrences(of: "\u{202F}", with: " "), want, what)
}

let mph = 1 / WindText.metresPerSecondToMph  // metres per second in one mph

// Speeds and names.
expect(WindText.speed(12 * mph, units: .miles), "12 mph", "mph")
expect(WindText.speed(5.3, units: .kilometres), "19 km/h", "km/h")
expect(WindText.compass(45), "NE", "NE")
expect(WindText.compass(350), "N", "350 is N")
expect(WindText.compass(-10), "N", "negative wraps")
expect(WindText.compass(210), "SW", "210 is SW")
expect(WindText.cardinal(257), "west", "Kiawah west")
expect(WindText.cardinal(77), "east", "Kiawah east")
expect(WindText.speedAndGust(12 * mph, gust: 20 * mph, units: .miles), "12 mph, gusts 20", "gusts")
expect(WindText.speedAndGust(12 * mph, gust: .nan, units: .miles), "12 mph", "no gust forecast")
expect(WindText.speedAndGust(12 * mph, gust: 12.2 * mph, units: .miles), "12 mph", "gust rounds to speed")
expect(WindText.speedAndGust(0.2, gust: 1, units: .miles), "Calm", "calm")

// The card: a NE wind on Kiawah's beach, axis 077 with 8.48 m/s against it.
expect(WindText.card(speed: 12 * mph, gust: 20 * mph, fromDegrees: 45,
                     axis: (heading: 77, tailwind: -0.85 * 12 * mph), units: .miles),
       "Wind 12 mph, gusts 20, from the NE, a tailwind heading west", "the plan's example")
expect(WindText.card(speed: 12 * mph, gust: .nan, fromDegrees: 225,
                     axis: (heading: 77, tailwind: 0.85 * 12 * mph), units: .miles),
       "Wind 12 mph, from the SW, a tailwind heading east", "tailwind east")
expect(WindText.card(speed: 12 * mph, gust: .nan, fromDegrees: 167,
                     axis: (heading: 77, tailwind: 0), units: .miles),
       "Wind 12 mph, from the S, across the beach", "square to the beach")
expect(WindText.card(speed: 12 * mph, gust: .nan, fromDegrees: 45, axis: nil, units: .miles),
       "Wind 12 mph, from the NE", "no facing in the pack")
expect(WindText.card(speed: 0.1, gust: .nan, fromDegrees: 45, axis: nil, units: .miles),
       "Wind calm", "calm card")

// Warnings.
expect(WindText.onshore(7.5, warnAt: 7), "Strong onshore wind: the water may run higher than shown.",
       "onshore at the limit")
expect(WindText.onshore(6.9, warnAt: 7), nil, "onshore under the limit")
expect(WindText.beachRide(speed: 15 * mph, gust: 22 * mph, tailwind: -6, warnAt: 5, units: .miles),
       "Headwind on the beach, 15 mph, gusts 22.", "headwind")
expect(WindText.beachRide(speed: 15 * mph, gust: .nan, tailwind: 6, warnAt: 5, units: .miles),
       "Tailwind on the beach, 15 mph.", "tailwind")
expect(WindText.beachRide(speed: 15 * mph, gust: .nan, tailwind: -4, warnAt: 5, units: .miles),
       nil, "light headwind says nothing")

// Source line. 2026-09-26 14:50 EDT = 18:50 UTC.
let zone = TimeZone(identifier: "America/New_York")!
let updated = 1_790_448_600.0
expectText(WindText.source(updated: updated, now: updated + 600, zone: zone,
                           locale: Locale(identifier: "en_US")),
           "Wind: National Weather Service forecast, updated 2:50 pm.", "source")
expect(WindText.source(updated: 0, now: updated, zone: zone, locale: Locale(identifier: "en_US")),
       "Wind: National Weather Service forecast.", "no update time")

if failures > 0 {
    print("wind_text_test: \(failures) failure(s)")
    exit(1)
}
print("wind_text_test: all passed")
