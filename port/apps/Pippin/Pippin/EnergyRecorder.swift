// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// EnergyRecorder.swift — the per-ride energy log and the MetricKit archive,
// the two measurements every battery change is judged against. Debug builds
// only: a shipping build writes neither.

#if DEBUG
import MetricKit
import UIKit

/// Writes `Documents/trips/energy-<date>.csv` once a minute while the map is
/// following or recording. A session starts when either begins and ends when
/// both stop; a mode change mid-session closes the current row early so each
/// row has one mode. Battery monitoring is on only during a session.
///
/// Main thread only. Each row is appended and the file closed, so a session
/// cut short by the app being killed keeps every row written so far.
@MainActor
final class EnergyRecorder {
    /// Seconds between rows.
    static let interval: TimeInterval = 60

    private let counters: () -> EnergyCounters
    private var meter = EnergyMeter()
    private var mode: EnergyMode?
    private var fileURL: URL?
    private var timer: Timer?

    /// `counters` returns the app's cumulative counters; it is called once
    /// per row.
    init(counters: @escaping () -> EnergyCounters) {
        self.counters = counters
    }

    /// Starts, re-labels or ends the session. Called whenever following or
    /// recording changes.
    func update(following: Bool, recording: Bool) {
        let next = EnergyMode(following: following, recording: recording)
        guard next != mode else { return }
        if let current = mode {
            appendRow(mode: current)
        }
        if let next {
            if mode == nil { begin(next) }
        } else {
            end()
        }
        mode = next
    }

    private func begin(_ mode: EnergyMode) {
        UIDevice.current.isBatteryMonitoringEnabled = true
        meter = EnergyMeter()
        guard let directory = RideLibrary.directory() else { return }
        let url = DocumentFolder.uniqueURL(
            in: directory,
            stem: "energy-" + DocumentFolder.fileNameFormatter.string(from: Date()),
            ext: "csv")
        guard (try? (EnergySample.header + "\n").write(
            to: url, atomically: false, encoding: .utf8)) != nil else { return }
        fileURL = url
        // The zero row: the battery level the session starts from.
        appendRow(mode: mode)
        let timer = Timer(timeInterval: Self.interval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self, let mode = self.mode else { return }
                self.appendRow(mode: mode)
            }
        }
        // `.common` so a gesture in progress does not delay the row.
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    private func end() {
        timer?.invalidate()
        timer = nil
        fileURL = nil
        UIDevice.current.isBatteryMonitoringEnabled = false
    }

    private func appendRow(mode: EnergyMode) {
        let device = UIDevice.current
        var now = counters()
        now.cpuSeconds = EnergyCounters.processCPUSeconds()
        let sample = EnergySample(
            time: Date(),
            mode: mode,
            batteryLevel: device.batteryLevel,
            batteryState: Self.name(of: device.batteryState),
            thermalState: ProcessInfo.processInfo.thermalState,
            interval: meter.take(now))
        guard let fileURL, let handle = try? FileHandle(forWritingTo: fileURL) else { return }
        defer { try? handle.close() }
        _ = try? handle.seekToEnd()
        try? handle.write(contentsOf: Data((sample.csvLine + "\n").utf8))
    }

    private static func name(of state: UIDevice.BatteryState) -> String {
        switch state {
        case .unplugged: return "unplugged"
        case .charging: return "charging"
        case .full: return "full"
        case .unknown: return "unknown"
        @unknown default: return "unknown"
        }
    }
}

/// Saves every MetricKit payload as JSON under `Documents/metrics/`. The
/// payloads arrive about once a day and carry cumulative CPU and GPU time,
/// location time by accuracy, and the display's average pixel luminance.
/// File names come from the payload's own time range, so a payload seen twice
/// (delivered, then again in `pastPayloads`) is written once.
final class MetricsArchive: NSObject, MXMetricManagerSubscriber {
    static let shared = MetricsArchive()

    /// Subscribes and saves anything already delivered. Call once at launch.
    func start() {
        let manager = MXMetricManager.shared
        manager.add(self)
        didReceive(manager.pastPayloads)
        didReceive(manager.pastDiagnosticPayloads)
    }

    func didReceive(_ payloads: [MXMetricPayload]) {
        for payload in payloads {
            Self.save(payload.jsonRepresentation(), kind: "metrics",
                      from: payload.timeStampBegin, to: payload.timeStampEnd)
        }
    }

    func didReceive(_ payloads: [MXDiagnosticPayload]) {
        for payload in payloads {
            Self.save(payload.jsonRepresentation(), kind: "diagnostics",
                      from: payload.timeStampBegin, to: payload.timeStampEnd)
        }
    }

    /// `metrics-<begin>--<end>.json`; an existing file is left alone.
    private static func save(_ json: Data, kind: String, from begin: Date, to end: Date) {
        guard let directory = DocumentFolder.directory(named: "metrics") else { return }
        let stamp = { DocumentFolder.fileNameFormatter.string(from: $0) }
        let url = directory.appendingPathComponent(
            "\(kind)-\(stamp(begin))--\(stamp(end)).json")
        guard !FileManager.default.fileExists(atPath: url.path) else { return }
        try? json.write(to: url, options: .atomic)
    }
}
#endif
