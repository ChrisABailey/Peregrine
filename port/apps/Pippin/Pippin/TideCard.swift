// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Charts
import PippinKit
import SwiftUI

/// The tide card: the predicted curve from three hours back to a day ahead
/// with the nights shaded, the next two high and low waters, the next sunset
/// and sunrise, and when the beach is rideable and when it is easy walking.
///
/// Heights come from the pack's NOAA table through `PPTide`; times are shown
/// in the device's zone and heights in the rider's units.
struct TideCard: View {
    let tide: PPTide
    let onClose: () -> Void

    @ObservedObject private var display = DisplayUnits.shared
    @ObservedObject private var windFeed = WindFeed.shared

    /// How far back and ahead the curve runs, and its sample spacing.
    private static let before: TimeInterval = 3 * 3600
    private static let ahead: TimeInterval = 24 * 3600
    private static let step: TimeInterval = 10 * 60

    var body: some View {
        NavigationStack {
            // Once a minute is enough to move the "now" mark on a 27-hour axis.
            TimelineView(.periodic(from: .now, by: 60)) { context in
                content(now: TideClock.override ?? context.date.timeIntervalSince1970)
            }
            .onAppear { windFeed.refresh() }
            .navigationTitle("Tides")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }

    @ViewBuilder
    private func content(now: TimeInterval) -> some View {
        let units = display.units
        let zone = TimeZone.current
        let locale = Locale.current
        let coverage = TideText.coverage(now: now, validUntil: tide.validUntil,
                                         zone: zone, locale: locale)
        let heightNow = tide.height(at: now)
        List {
            if let notice = TideText.coverageNotice(coverage) {
                Section {
                    Label(notice, systemImage: "calendar.badge.exclamationmark")
                        .font(.footnote)
                }
            }
            if !heightNow.isNaN {
                Section {
                    Text(TideText.now(heightMeters: heightNow,
                                      rising: tide.isRising(at: now),
                                      units: units))
                        .font(.title2.weight(.semibold))
                        .monospacedDigit()
                    chart(now: now, units: units)
                        .frame(height: 132)
                        .padding(.vertical, 6)
                }
                Section {
                    ForEach(nextExtremes(now: now), id: \.time) { e in
                        Text(TideText.extreme(isHigh: e.isHigh, time: e.time,
                                              heightMeters: e.heightMeters, now: now,
                                              units: units, zone: zone, locale: locale))
                            .monospacedDigit()
                    }
                    ForEach(windLines(now: now, units: units), id: \.self) { line in
                        Label(line, systemImage: "wind")
                    }
                    if let sun = TideText.sun(sunEvents(from: now, to: now + 2 * 86_400),
                                              now: now, zone: zone, locale: locale) {
                        Label(sun, systemImage: "sunrise")
                            .monospacedDigit()
                    }
                    Text(beachLine(now: now, below: tide.rideableBelowMeters, activity: .ride,
                                   zone: zone, locale: locale))
                        .monospacedDigit()
                    if tide.walkEasyBelowMeters.isFinite {
                        Text(beachLine(now: now, below: tide.walkEasyBelowMeters, activity: .walk,
                                       zone: zone, locale: locale))
                            .monospacedDigit()
                    }
                }
            }
            Section {
                Text("\(tide.stationName), NOAA station \(tide.stationID). "
                     + "Predicted tide only: onshore wind and storms raise the water above it. "
                     + "Heights above \(tide.datum).")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                if let forecast = windFeed.forecast, forecast.sample(at: now) != nil {
                    Text(WindText.source(updated: forecast.updateTime, now: now,
                                         zone: zone, locale: locale))
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }

    /// The curve, the rideable band under the threshold, the hatched easy-walking
    /// band, and the "now" mark.
    private func chart(now: TimeInterval, units: DistanceUnits) -> some View {
        let samples = curve(now: now)
        // An infinite limit (passable at any tide) has no line or band to draw.
        let threshold = tide.rideableBelowMeters.isFinite
            ? TideText.chartValue(tide.rideableBelowMeters, units: units) : nil
        let walk = tide.walkEasyBelowMeters.isFinite
            ? TideText.chartValue(tide.walkEasyBelowMeters, units: units) : nil
        let values = samples.map { TideText.chartValue($0.meters, units: units) }
        let limits = [threshold, walk].compactMap { $0 }
        let low = min(values.min() ?? 0, 0, limits.min() ?? 0)
        let high = max(values.max() ?? 1, limits.max() ?? 0)
        let start = Date(timeIntervalSince1970: now - Self.before)
        let end = Date(timeIntervalSince1970: now + Self.ahead)
        let extremes = tide.extremes(from: now - Self.before, to: now + Self.ahead)
        let nights = TideText.nights(sunEvents(from: now - Self.before, to: now + Self.ahead),
                                     from: now - Self.before, to: now + Self.ahead)
        return Chart {
            ForEach(nights, id: \.begin) { n in
                RectangleMark(xStart: .value("Dusk", Date(timeIntervalSince1970: n.begin)),
                              xEnd: .value("Dawn", Date(timeIntervalSince1970: n.end)),
                              yStart: .value("Floor", low), yEnd: .value("Top", high))
                    .foregroundStyle(.gray.opacity(0.5))
            }
            if let threshold {
                RectangleMark(xStart: .value("Start", start), xEnd: .value("End", end),
                              yStart: .value("Floor", low), yEnd: .value("Rideable", threshold))
                    .foregroundStyle(.green.opacity(0.15))
                RuleMark(y: .value("Rideable", threshold))
                    .foregroundStyle(.green.opacity(0.6))
                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 3]))
                    .annotation(position: .bottom, alignment: .leading) {
                        Text("ride").font(.caption2).foregroundStyle(.green)
                    }
            }
            if let walk {
                RuleMark(y: .value("Easy walking", walk))
                    .foregroundStyle(.orange.opacity(0.7))
                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 3]))
                    .annotation(position: .top, alignment: .leading) {
                        Text("walk").font(.caption2).foregroundStyle(.orange)
                    }
            }
            ForEach(samples, id: \.time) { s in
                LineMark(x: .value("Time", Date(timeIntervalSince1970: s.time)),
                         y: .value("Height", TideText.chartValue(s.meters, units: units)))
                    .interpolationMethod(.monotone)
                    .foregroundStyle(.blue)
            }
            ForEach(extremes, id: \.time) { e in
                PointMark(x: .value("Time", Date(timeIntervalSince1970: e.time)),
                          y: .value("Height", TideText.chartValue(e.heightMeters, units: units)))
                    .foregroundStyle(.blue)
                    .symbolSize(24)
            }
            RuleMark(x: .value("Now", Date(timeIntervalSince1970: now)))
                .foregroundStyle(.red)
                .annotation(position: .top, alignment: .center) {
                    Text("Now").font(.caption2).foregroundStyle(.red)
                }
        }
        .chartOverlay { proxy in
            GeometryReader { geo in
                if let walk, let frame = proxy.plotFrame, let y = proxy.position(forY: walk) {
                    let plot = geo[frame]
                    let band = CGRect(x: plot.minX, y: plot.minY + y,
                                      width: plot.width, height: max(0, plot.height - y))
                    Self.hatch(band)
                        .stroke(.orange.opacity(0.45), lineWidth: 1)
                        .clipShape(Path(band))
                        .allowsHitTesting(false)
                }
            }
        }
        .chartXScale(domain: start...end)
        .chartYScale(domain: low...high)
        .chartYAxisLabel(TideText.unitName(units))
        .chartXAxis {
            AxisMarks(values: .stride(by: .hour, count: 6)) {
                AxisGridLine()
                AxisTick()
                AxisValueLabel(format: .dateTime.hour())
            }
        }
    }

    /// Diagonal lines 6 pt apart covering `band`; the caller clips them to it.
    private static func hatch(_ band: CGRect) -> Path {
        Path { p in
            for x in stride(from: band.minX - band.height, through: band.maxX, by: 6) {
                p.move(to: CGPoint(x: x, y: band.maxY))
                p.addLine(to: CGPoint(x: x + band.height, y: band.minY))
            }
        }
    }

    /// Heights every `step` across the chart's span, skipping any time the
    /// table does not cover.
    private func curve(now: TimeInterval) -> [(time: TimeInterval, meters: Double)] {
        stride(from: now - Self.before, through: now + Self.ahead, by: Self.step)
            .compactMap { t in
                let h = tide.height(at: t)
                return h.isNaN ? nil : (t, h)
            }
    }

    /// The wind now along the beach, and the onshore warning when it applies.
    /// Empty without a forecast covering `now`.
    private func windLines(now: TimeInterval, units: DistanceUnits) -> [String] {
        guard let settings = windFeed.settings,
              let sample = windFeed.forecast?.sample(at: now) else { return [] }
        let faces = settings.beachFacesDegrees
        let axis: (heading: Double, tailwind: Double)? = faces.isFinite
            ? (faces - 90, sample.tailwind(heading: faces - 90)) : nil
        var lines = [WindText.card(speed: sample.speedMetersPerSecond,
                                   gust: sample.gustMetersPerSecond,
                                   fromDegrees: sample.fromDegrees, axis: axis, units: units)]
        if faces.isFinite, let warning = WindText.onshore(
            sample.onshore(seaward: faces), warnAt: settings.onshoreWarnMetersPerSecond) {
            lines.append(warning)
        }
        return lines
    }

    /// Sunrises and sunsets at the tide station in [from, to].
    private func sunEvents(from: TimeInterval, to: TimeInterval)
        -> [(time: TimeInterval, isRise: Bool)] {
        tide.sunEvents(from: from, to: to).map { ($0.time, $0.isRise) }
    }

    /// The next high and low water after `now`. Two days is always enough to
    /// find two on a semidiurnal or diurnal table.
    private func nextExtremes(now: TimeInterval) -> [PPTideExtreme] {
        Array(tide.extremes(from: now, to: now + 2 * 86_400).prefix(2))
    }

    /// When over the next day the water is at or below `below` metres.
    private func beachLine(now: TimeInterval, below: Double, activity: TideText.BeachActivity,
                           zone: TimeZone, locale: Locale) -> String {
        Self.beachLine(tide, now: now, below: below, activity: activity,
                       zone: zone, locale: locale)
    }

    /// When over the next day the water in `tide` is at or below `below` metres.
    /// Shared with the route sheet so both say the same thing near the table's end.
    static func beachLine(_ tide: PPTide, now: TimeInterval, below: Double,
                          activity: TideText.BeachActivity,
                          zone: TimeZone, locale: Locale) -> String {
        // A window the table's end cuts short is open-ended, not over.
        let horizon = min(now + ahead, tide.validUntil)
        let windows = tide.windows(below: below, from: now, to: horizon)
            .map { (begin: $0.begin, end: $0.end) }
        return TideText.beach(now: now, windows: windows, horizon: horizon,
                              zone: zone, locale: locale, activity: activity)
    }
}
