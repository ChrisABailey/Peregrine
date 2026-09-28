// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// EnergyLog.swift — the rows of the per-ride energy log, and the arithmetic
// that turns cumulative counters into per-interval ones.
//
// Foundation-only so the mac can test it; `EnergyRecorder` in the app reads
// the battery and writes the file. Each row covers the interval since the
// previous row: counters are differences, the mode is the one in force over
// that interval, and battery and thermal state are read at the row's time.

import Foundation

/// What the ride was doing over an interval. Nil (no log) when neither.
enum EnergyMode: String {
    case following, recording, both

    init?(following: Bool, recording: Bool) {
        switch (following, recording) {
        case (true, true): self = .both
        case (true, false): self = .following
        case (false, true): self = .recording
        case (false, false): return nil
        }
    }
}

/// Monotonic counters kept since launch. The log records their differences.
struct EnergyCounters: Equatable {
    /// Frames finished by the render loop; `baseDraws + cacheHits`.
    var frames = 0
    /// Frames that redrew the base map.
    var baseDraws = 0
    /// Frames served from the cached base (an overlay pass only).
    var cacheHits = 0
    /// Fixes delivered by the feed.
    var fixes = 0
    /// User plus system CPU time of the whole process, seconds.
    var cpuSeconds = 0.0

    static func - (lhs: EnergyCounters, rhs: EnergyCounters) -> EnergyCounters {
        EnergyCounters(frames: lhs.frames - rhs.frames,
                       baseDraws: lhs.baseDraws - rhs.baseDraws,
                       cacheHits: lhs.cacheHits - rhs.cacheHits,
                       fixes: lhs.fixes - rhs.fixes,
                       cpuSeconds: lhs.cpuSeconds - rhs.cpuSeconds)
    }

    /// User plus system CPU time consumed by this process so far, or zero if
    /// `getrusage` fails.
    static func processCPUSeconds() -> Double {
        var usage = rusage()
        guard getrusage(RUSAGE_SELF, &usage) == 0 else { return 0 }
        func seconds(_ t: timeval) -> Double {
            Double(t.tv_sec) + Double(t.tv_usec) / 1_000_000
        }
        return seconds(usage.ru_utime) + seconds(usage.ru_stime)
    }
}

/// Turns successive counter readings into interval differences.
struct EnergyMeter {
    private var last: EnergyCounters?

    /// Returns the change since the previous reading; zero for the first.
    mutating func take(_ now: EnergyCounters) -> EnergyCounters {
        defer { last = now }
        guard let last else { return EnergyCounters() }
        return now - last
    }
}

/// One line of `energy-<date>.csv`.
struct EnergySample {
    var time: Date
    var mode: EnergyMode
    /// 0...1 as `UIDevice` reports it (in 5 % steps), or negative when unknown.
    var batteryLevel: Float
    /// `unplugged`, `charging`, `full` or `unknown`.
    var batteryState: String
    var thermalState: ProcessInfo.ThermalState
    /// The change over the interval this row closes.
    var interval: EnergyCounters

    static let header =
        "time,mode,battery_level,battery_state,thermal_state,"
        + "frames,base_draws,cache_hits,fixes,cpu_s"

    /// The CSV line, without a terminator. An unknown battery level is an
    /// empty field rather than -1, so a spreadsheet does not average it in.
    var csvLine: String {
        let level = batteryLevel < 0 ? "" : String(format: "%.2f", batteryLevel)
        return [
            Self.timeFormatter.string(from: time),
            mode.rawValue,
            level,
            batteryState,
            Self.name(of: thermalState),
            String(interval.frames),
            String(interval.baseDraws),
            String(interval.cacheHits),
            String(interval.fixes),
            String(format: "%.3f", interval.cpuSeconds),
        ].joined(separator: ",")
    }

    static func name(of state: ProcessInfo.ThermalState) -> String {
        switch state {
        case .nominal: return "nominal"
        case .fair: return "fair"
        case .serious: return "serious"
        case .critical: return "critical"
        @unknown default: return "unknown"
        }
    }

    /// UTC ISO 8601 to the second, which every spreadsheet parses.
    static let timeFormatter: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        return formatter
    }()
}
