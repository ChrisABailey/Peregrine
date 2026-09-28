// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// TideText.swift — the tide card's words and numbers.
//
// Foundation only, so it compiles and runs on the mac
// (`test/run_tide_text_test.sh`). The time zone and locale are parameters
// for the same reason; the card passes the device's.

import Foundation

/// The time the tide is judged at: now, or `-PPDepartAt <epoch>` in a
/// DEBUG build.
enum TideClock {
    static let override: TimeInterval? = {
        #if DEBUG
        let t = UserDefaults.standard.double(forKey: "PPDepartAt")
        return t > 0 ? t : nil
        #else
        return nil
        #endif
    }()

    static var now: TimeInterval { override ?? Date().timeIntervalSince1970 }
}

/// Formatting and wording for the tide card. Times are epoch seconds, heights
/// metres above the table's datum.
enum TideText {
    /// A tide height in the rider's units: feet to a tenth, or metres to a
    /// hundredth. A tide table's own precision is about a tenth of a foot.
    static func height(_ meters: Double, units: DistanceUnits) -> String {
        switch units {
        case .miles: return String(format: "%.1f ft", meters / metersPerFoot)
        case .kilometres: return String(format: "%.2f m", meters)
        }
    }

    /// A height converted for the chart's y axis, in feet or metres.
    static func chartValue(_ meters: Double, units: DistanceUnits) -> Double {
        units == .miles ? meters / metersPerFoot : meters
    }

    /// The chart's y-axis unit label.
    static func unitName(_ units: DistanceUnits) -> String {
        units == .miles ? "ft" : "m"
    }

    /// A clock time, "2:14 pm", or "14:14" where the locale uses a 24-hour
    /// clock. A time on another calendar day than `now` gets its weekday.
    static func clock(_ time: TimeInterval, now: TimeInterval,
                      zone: TimeZone, locale: Locale) -> String {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = zone
        let date = Date(timeIntervalSince1970: time)
        let formatter = DateFormatter()
        formatter.locale = locale
        formatter.timeZone = zone
        formatter.amSymbol = "am"
        formatter.pmSymbol = "pm"
        let sameDay = calendar.isDate(date, inSameDayAs: Date(timeIntervalSince1970: now))
        formatter.setLocalizedDateFormatFromTemplate(sameDay ? "jmm" : "EEEjmm")
        return formatter.string(from: date)
    }

    /// One extreme as a line of text: "Low 2:14 pm, 0.2 ft".
    static func extreme(isHigh: Bool, time: TimeInterval, heightMeters: Double,
                        now: TimeInterval, units: DistanceUnits,
                        zone: TimeZone, locale: Locale) -> String {
        let kind = isHigh ? "High" : "Low"
        let when = clock(time, now: now, zone: zone, locale: locale)
        return "\(kind) \(when), \(height(heightMeters, units: units))"
    }

    /// The water now and which way it is going: "1.2 ft and rising".
    static func now(heightMeters: Double, rising: Bool,
                    units: DistanceUnits) -> String {
        "\(height(heightMeters, units: units)) and \(rising ? "rising" : "falling")"
    }

    /// What a beach line is about: riding, below the rideable height, or
    /// easy walking, below the walking one.
    enum BeachActivity {
        case ride
        case walk

        fileprivate var yes: String {
            self == .ride ? "The beach should be rideable" : "The beach has the best walking"
        }
        fileprivate var no: String {
            self == .ride ? "The beach is unlikely to be rideable" : "The beach has no easy walking"
        }
        /// Riding thresholds are estimates, so riding times are hedged.
        fileprivate var about: String { self == .ride ? "about " : "" }
    }

    /// Whether the beach rides (or walks easily) now, and until or from when,
    /// given the below-threshold windows over the next day in time order.
    static func beach(now: TimeInterval, windows: [(begin: TimeInterval, end: TimeInterval)],
                      horizon: TimeInterval, zone: TimeZone, locale: Locale,
                      activity: BeachActivity = .ride) -> String {
        let t = { clock($0, now: now, zone: zone, locale: locale) }
        let yes = activity.yes
        let about = activity.about
        if let w = windows.first(where: { $0.begin <= now && now <= $0.end }) {
            // A window cut by the card's horizon, not by the tide.
            if w.end >= horizon { return "\(yes) for the next day" }
            return "\(yes) until \(about)\(t(w.end))"
        }
        if let w = windows.first(where: { $0.begin > now }) {
            if w.end >= horizon { return "\(yes) from \(about)\(t(w.begin))" }
            return "\(yes) \(about)\(t(w.begin)) to \(t(w.end))"
        }
        return "\(activity.no) in the next day"
    }

    /// The next high or low water joined to a beach line: "High tide is
    /// 8:40 pm, the beach has the best walking until 5:54 pm."
    static func nextTide(isHigh: Bool, time: TimeInterval, beach: String,
                         now: TimeInterval, zone: TimeZone, locale: Locale) -> String {
        let when = clock(time, now: now, zone: zone, locale: locale)
        let tail = beach.prefix(1).lowercased() + beach.dropFirst()
        return "\(isHigh ? "High" : "Low") tide is \(when), \(tail)."
    }

    /// The next sunrise and sunset after `now`, in time order: "Sunset
    /// 7:10 pm · Sunrise Sun 7:13 am". Nil when there is neither, as in a
    /// polar day or night.
    static func sun(_ events: [(time: TimeInterval, isRise: Bool)], now: TimeInterval,
                    zone: TimeZone, locale: Locale) -> String? {
        let next = events.filter { $0.time > now }.prefix(2)
        guard !next.isEmpty else { return nil }
        return next.map { e in
            "\(e.isRise ? "Sunrise" : "Sunset") \(clock(e.time, now: now, zone: zone, locale: locale))"
        }.joined(separator: " · ")
    }

    /// The nights within [from, to], from the sunrises and sunsets in that
    /// span in time order. A span that opens on a sunrise starts in the dark.
    /// No events gives no nights: a polar day and a polar night look alike.
    static func nights(_ events: [(time: TimeInterval, isRise: Bool)],
                       from: TimeInterval, to: TimeInterval)
        -> [(begin: TimeInterval, end: TimeInterval)] {
        var out: [(begin: TimeInterval, end: TimeInterval)] = []
        var dark: TimeInterval? = events.first.map { $0.isRise ? from : nil } ?? nil
        for e in events {
            if e.isRise, let begin = dark {
                out.append((begin, e.time))
                dark = nil
            } else if !e.isRise {
                dark = e.time
            }
        }
        if let begin = dark, !events.isEmpty { out.append((begin, to)) }
        return out
    }

    /// How close the table is to its end.
    enum Coverage: Equatable {
        case current
        /// Within `warningDays` of the end; the last covered day, formatted.
        case endingSoon(String)
        /// Past the end; the last covered day, formatted.
        case ended(String)
    }

    /// Days before the table's end that the card starts to say so.
    static let warningDays = 60.0

    static func coverage(now: TimeInterval, validUntil: TimeInterval,
                         zone: TimeZone, locale: Locale) -> Coverage {
        let formatter = DateFormatter()
        formatter.locale = locale
        formatter.timeZone = zone
        formatter.dateStyle = .medium
        formatter.timeStyle = .none
        // `validUntil` is the first instant not covered; name the day before it.
        let last = formatter.string(from: Date(timeIntervalSince1970: validUntil - 1))
        if now >= validUntil { return .ended(last) }
        if validUntil - now <= warningDays * 86_400 { return .endingSoon(last) }
        return .current
    }

    /// The sentence the card shows for a coverage state, or nil when current.
    static func coverageNotice(_ coverage: Coverage) -> String? {
        switch coverage {
        case .current:
            return nil
        case .endingSoon(let day):
            return "The tide table runs through \(day). A newer app will carry the next one."
        case .ended(let day):
            return "The tide table ended on \(day). Tides after that are unknown; "
                + "beach routes are still offered, so check the water before riding the sand."
        }
    }

    // MARK: - The beach on a route

    /// Why the tide took the beach off a route.
    enum BeachDrop: Equatable {
        case high
        case rising
        case noTable
    }

    /// The route sheet's line when the setting admitted the beach and the tide
    /// dropped it. NaN times are left out of the sentence.
    static func beachNotUsed(_ reason: BeachDrop, enterHeightMeters: Double,
                             passableFrom: TimeInterval, coveredAt: TimeInterval,
                             now: TimeInterval, units: DistanceUnits,
                             zone: TimeZone, locale: Locale) -> String {
        let t = { clock($0, now: now, zone: zone, locale: locale) }
        switch reason {
        case .high:
            var line = "Beach not used — the tide is high"
            if enterHeightMeters.isFinite {
                line += " (\(height(enterHeightMeters, units: units)))"
            }
            line += "."
            if passableFrom.isFinite { line += " Rideable again from \(t(passableFrom))." }
            return line
        case .rising:
            if coveredAt.isFinite {
                return "Beach not used — the tide is rising and covers the beach by \(t(coveredAt))."
            }
            return "Beach not used — the tide is rising."
        case .noTable:
            return "Beach not used — the tide table has ended."
        }
    }

    /// The tide's verdict on a stretch the route rides.
    enum StretchVerdict: Equatable {
        case good
        case marginal
        /// No table, or past its end.
        case unknown
    }

    /// The route sheet's line on the beach stretch the route rides, from its
    /// highest water. `hasTable` distinguishes a pack with no table from one
    /// whose table has ended. A marginal stretch on foot is harder going
    /// rather than soft riding.
    static func beachVerdict(_ verdict: StretchVerdict, peakMeters: Double,
                             peakAt: TimeInterval, hasTable: Bool,
                             now: TimeInterval, units: DistanceUnits,
                             zone: TimeZone, locale: Locale,
                             walking: Bool = false) -> String {
        switch verdict {
        case .unknown:
            return hasTable
                ? "The tide table has ended, so the tide on the beach is unknown. "
                    + "Check the water before riding the sand."
                : "No tide table for this area. Check the water before riding the sand."
        case .good, .marginal:
            let at = clock(peakAt, now: now, zone: zone, locale: locale)
            let head = "The tide will be \(height(peakMeters, units: units)) at \(at)"
            if verdict == .good { return head + " — firm sand." }
            return head + (walking ? " — softer sand and harder going." : " — soft sand likely.")
        }
    }

    /// The route summary's tail: "includes 4.2 km of beach".
    static func beachShare(_ meters: Double, units: DistanceUnits) -> String {
        "includes \(units.distance(meters)) of beach"
    }

    private static let metersPerFoot = 0.3048
}
