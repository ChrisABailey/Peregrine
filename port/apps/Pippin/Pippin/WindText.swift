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

    /// The route sheet's line for the beach: "Headwind on the beach, 15 mph,
    /// gusts 22." when the headwind component reaches `warnAt`, "Tailwind on
    /// the beach, 15 mph." for as strong a tailwind, nil otherwise. `tailwind`
    /// is the component along the stretch; the speed named is the wind's.
    static func beachRide(speed mps: Double, gust: Double, tailwind: Double,
                          warnAt: Double, units: DistanceUnits) -> String? {
        guard abs(tailwind) >= warnAt else { return nil }
        let kind = tailwind < 0 ? "Headwind" : "Tailwind"
        return "\(kind) on the beach, \(speedAndGust(mps, gust: gust, units: units))."
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
