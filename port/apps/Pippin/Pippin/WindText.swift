// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Foundation

/// Wording for the wind on the tide card and in the route sheet. Speeds are
/// metres per second, directions degrees true the wind blows from; the
/// components along the beach are computed by PippinKit and passed in.
enum WindText {
    static let metresPerSecondToMph = 2.236_936
    static let metresPerSecondToKmh = 3.6

    /// A speed rounded to a whole unit: "12 mph" or "19 km/h".
    static func speed(_ mps: Double, units: DistanceUnits) -> String {
        switch units {
        case .miles: return "\(Int((mps * metresPerSecondToMph).rounded())) mph"
        case .kilometres: return "\(Int((mps * metresPerSecondToKmh).rounded())) km/h"
        }
    }

    /// The eight-point name of a direction: "NE".
    static func compass(_ degrees: Double) -> String {
        let names = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        let index = Int(((degrees.truncatingRemainder(dividingBy: 360) + 360 + 22.5) / 45)
            .rounded(.down)) % 8
        return names[index]
    }

    /// The cardinal word nearest a heading: "west".
    static func cardinal(_ degrees: Double) -> String {
        let names = ["north", "east", "south", "west"]
        let index = Int(((degrees.truncatingRemainder(dividingBy: 360) + 360 + 45) / 90)
            .rounded(.down)) % 4
        return names[index]
    }

    /// "12 mph, gusts 20", the gust left out when it rounds to the speed or
    /// is not forecast. "Calm" below half a unit.
    static func speedAndGust(_ mps: Double, gust: Double, units: DistanceUnits) -> String {
        let s = speed(mps, units: units)
        if s.hasPrefix("0 ") { return "Calm" }
        guard gust.isFinite else { return s }
        let g = speed(gust, units: units)
        guard g != s else { return s }
        return "\(s), gusts \(g.split(separator: " ").first ?? "")"
    }

    /// The tide card's line: "Wind 12 mph, gusts 20, from the NE, a tailwind
    /// heading west". `axis` is one direction along the beach and the
    /// tailwind component on it; nil when the pack does not say which way the
    /// beach runs. A wind within 30 degrees of square to the beach (its
    /// component along it under half its speed) is "across the beach".
    static func card(speed mps: Double, gust: Double, fromDegrees: Double,
                     axis: (heading: Double, tailwind: Double)?,
                     units: DistanceUnits) -> String {
        let head = speedAndGust(mps, gust: gust, units: units)
        if head == "Calm" { return "Wind calm" }
        var line = "Wind \(head), from the \(compass(fromDegrees))"
        if let axis, mps > 0 {
            if abs(axis.tailwind) < 0.5 * mps {
                line += ", across the beach"
            } else {
                let downwind = axis.tailwind > 0 ? axis.heading : axis.heading + 180
                line += ", a tailwind heading \(cardinal(downwind))"
            }
        }
        return line
    }

    /// The warning when the onshore component reaches `warnAt`, else nil.
    static func onshore(_ component: Double, warnAt: Double) -> String? {
        component >= warnAt
            ? "Strong onshore wind: the water may run higher than shown." : nil
    }

    /// The wind on one beach stretch at the time it is reached. `along` is the
    /// component in the direction of travel, positive with the rider.
    struct BeachWind {
        var heading: Double
        var along: Double
        var speed: Double
        var gust: Double
        var fromDegrees: Double
    }

    /// The route sheet's beach lines: the strongest headwind stretch and the
    /// strongest tailwind stretch whose component along the stretch exceeds
    /// `warnAt`, headwind first. Empty when none does.
    static func beachRide(_ stretches: [BeachWind], warnAt: Double,
                          units: DistanceUnits) -> [String] {
        let named = stretches.filter { $0.heading.isFinite && abs($0.along) > warnAt }
        let head = named.filter { $0.along < 0 }.min { $0.along < $1.along }
        let tail = named.filter { $0.along > 0 }.max { $0.along < $1.along }
        return [head, tail].compactMap { $0.map { beachRideLine($0, units: units) } }
    }

    /// "Headwind heading east, 9 mph (NE wind 15 mph, gusts 22)." The wind
    /// itself is left out when it rounds to the component.
    static func beachRideLine(_ w: BeachWind, units: DistanceUnits) -> String {
        let kind = w.along < 0 ? "Headwind" : "Tailwind"
        let component = speed(abs(w.along), units: units)
        let wind = speedAndGust(w.speed, gust: w.gust, units: units)
        let lead = "\(kind) heading \(cardinal(w.heading)), "
        if wind.hasPrefix(component + ",") || wind == component { return lead + wind + "." }
        return lead + "\(component) (\(compass(w.fromDegrees)) wind \(wind))."
    }

    // MARK: - Map badge

    /// How long after a fetch the map badge keeps showing the forecast.
    static let badgeFreshFor: TimeInterval = 6 * 3600

    /// Whether a forecast fetched at `fetchedAt` is recent enough for the
    /// badge. A fetch time ahead of `now` (clock moved back) counts as fresh.
    static func badgeShown(fetchedAt: TimeInterval?, now: TimeInterval) -> Bool {
        guard let fetchedAt else { return false }
        return now - fetchedAt < badgeFreshFor
    }

    /// The badge's text: "9 mph", or "Calm" below half a unit.
    static func badge(speed mps: Double, units: DistanceUnits) -> String {
        let s = speed(mps, units: units)
        return s.hasPrefix("0 ") ? "Calm" : s
    }

    /// The eight-point direction as spoken words: "south-west".
    static func spokenCompass(_ degrees: Double) -> String {
        let names = ["north", "north-east", "east", "south-east",
                     "south", "south-west", "west", "north-west"]
        let index = Int(((degrees.truncatingRemainder(dividingBy: 360) + 360 + 22.5) / 45)
            .rounded(.down)) % 8
        return names[index]
    }

    /// VoiceOver label: "Wind from the south-west, 9 miles per hour."
    static func badgeLabel(speed mps: Double, fromDegrees: Double,
                           units: DistanceUnits) -> String {
        let text = badge(speed: mps, units: units)
        if text == "Calm" { return "Wind calm." }
        let number = text.split(separator: " ").first ?? ""
        let unit: String
        switch units {
        case .miles: unit = number == "1" ? "mile per hour" : "miles per hour"
        case .kilometres: unit = number == "1" ? "kilometre per hour" : "kilometres per hour"
        }
        return "Wind from the \(spokenCompass(fromDegrees)), \(number) \(unit)."
    }

    /// The card's footnote: "Wind: National Weather Service forecast, updated
    /// 2:50 pm."
    static func source(updated: TimeInterval, now: TimeInterval,
                       zone: TimeZone, locale: Locale) -> String {
        guard updated > 0 else { return "Wind: National Weather Service forecast." }
        let when = TideText.clock(updated, now: now, zone: zone, locale: locale)
        return "Wind: National Weather Service forecast, updated \(when)."
    }
}
