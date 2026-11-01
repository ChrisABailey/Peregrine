// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Foundation
import PippinKit
import SwiftUI
import os

/// Where the boat's AIS receiver or NMEA network is, and what to take from it.
/// Persisted as JSON in user defaults.
struct AisReceiverSettings: Codable, Equatable {
    enum Transport: String, Codable, CaseIterable, Identifiable {
        case tcp, udp
        var id: String { rawValue }
        var menuName: String { self == .tcp ? "TCP" : "UDP" }
    }

    var enabled = false
    var transport = Transport.tcp
    /// TCP only: the receiver's address. UDP listens on every interface.
    var host = "192.168.1.1"
    var port: UInt16 = 10110
    /// The receiver's position sentences drive the ownship in place of the
    /// phone's GPS while they keep arriving.
    var usePosition = false

    /// IANA's NMEA-0183 port, and the common Wi-Fi multiplexer default.
    static let presetPorts: [UInt16] = [10110, 2000]

    private static let key = "aisReceiver"

    static func load() -> AisReceiverSettings {
        guard let data = UserDefaults.standard.data(forKey: key),
              let s = try? JSONDecoder().decode(AisReceiverSettings.self, from: data)
        else { return AisReceiverSettings() }
        return s
    }

    func save() {
        if let data = try? JSONEncoder().encode(self) {
            UserDefaults.standard.set(data, forKey: Self.key)
        }
    }
}

/// A local AIS receiver over TCP or UDP: polls a `PPAisReceiver` on its own
/// queue and hands AIS sentences and position fixes to the main actor.
@MainActor
final class AisReceiver: ObservableObject {
    enum Status: Equatable {
        case off
        case connecting
        case live
        case silent
        /// Waiting to reconnect, with the reason.
        case retrying(String)
    }

    @Published private(set) var settings = AisReceiverSettings.load()
    @Published private(set) var status = Status.off
    /// Sentences heard since the connection was set up.
    @Published private(set) var linesReceived = 0
    @Published private(set) var fixCount = 0
    /// When the last position fix arrived.
    @Published private(set) var lastFixDate: Date?

    /// AIS sentences and the time they were heard.
    var deliverLines: (([String], Date) -> Void)?
    var deliverFix: ((PPFix) -> Void)?

    private static let log = Logger(subsystem: "org.peregrine.Pippin", category: "ais-receiver")
    private static let pollInterval: DispatchTimeInterval = .milliseconds(250)
    /// Receiver fixes older than this hand the ownship back to the phone.
    static let positionLifetime: TimeInterval = 10

    private let queue = DispatchQueue(label: "org.peregrine.ais-receiver")
    private var poller: Poller?

    var isRunning: Bool { poller != nil }

    /// The receiver's fixes own the ownship: the setting is on and a fix
    /// arrived within `positionLifetime`.
    var suppliesPosition: Bool {
        guard settings.enabled, settings.usePosition, let lastFixDate else { return false }
        return Date().timeIntervalSince(lastFixDate) < Self.positionLifetime
    }

    /// Stores new settings. A changed connection closes the running one; the
    /// caller starts it again.
    func update(_ next: AisReceiverSettings) {
        guard next != settings else { return }
        let reconnect = next.enabled != settings.enabled || next.transport != settings.transport
            || next.host != settings.host || next.port != settings.port
        settings = next
        settings.save()
        if !settings.usePosition { lastFixDate = nil }
        if reconnect { stop() }
    }

    /// Opens the connection from the settings. Nothing when they are off.
    func start() {
        guard AppFlavor.current.offersAisTraffic, settings.enabled, poller == nil else { return }
        let s = settings
        let poller = Poller(queue: queue, settings: s) { [weak self] batch, totals in
            Task { @MainActor in self?.receive(batch, totals: totals) }
        }
        self.poller = poller
        linesReceived = 0
        fixCount = 0
        status = .connecting
        Self.log.notice("AIS receiver \(s.transport.rawValue, privacy: .public) \(s.host, privacy: .public):\(s.port)")
        poller.start()
    }

    func stop() {
        poller?.stop()
        poller = nil
        status = .off
        lastFixDate = nil
    }

    private func receive(_ batch: PPAisReceiverBatch, totals: (lines: Int, fixes: Int, error: String)) {
        guard isRunning else { return }
        if !batch.aisLines.isEmpty { deliverLines?(batch.aisLines, Date()) }
        if settings.usePosition, !batch.fixes.isEmpty {
            lastFixDate = Date()
            for fix in batch.fixes { deliverFix?(fix) }
        }
        if totals.lines != linesReceived { linesReceived = totals.lines }
        if totals.fixes != fixCount { fixCount = totals.fixes }
        let next: Status
        switch batch.state {
        case .connecting: next = .connecting
        case .live: next = .live
        case .silent: next = .silent
        case .retrying: next = .retrying(totals.error)
        default: next = .off
        }
        if next != status {
            if case .retrying(let reason) = next {
                Self.log.notice("AIS receiver closed: \(reason, privacy: .public)")
            }
            status = next
        }
    }
}

/// Owns the `PPAisReceiver` and its timer; touched only on `queue`.
private final class Poller: @unchecked Sendable {
    typealias Totals = (lines: Int, fixes: Int, error: String)

    private let queue: DispatchQueue
    private let receiver: PPAisReceiver
    private let onBatch: (PPAisReceiverBatch, Totals) -> Void
    private var timer: DispatchSourceTimer?
    private var reported: Totals = (0, 0, "")

    init(queue: DispatchQueue, settings: AisReceiverSettings,
         onBatch: @escaping (PPAisReceiverBatch, Totals) -> Void) {
        self.queue = queue
        self.onBatch = onBatch
        receiver = PPAisReceiver(
            with: settings.transport == .udp ? .UDP : .TCP,
            host: settings.host.trimmingCharacters(in: .whitespaces),
            port: settings.port)
    }

    func start() {
        queue.async { [self] in
            receiver.start(at: Date().timeIntervalSince1970)
            let timer = DispatchSource.makeTimerSource(queue: queue)
            timer.schedule(deadline: .now(), repeating: .milliseconds(250))
            timer.setEventHandler { [weak self] in self?.poll() }
            self.timer = timer
            timer.resume()
        }
    }

    func stop() {
        queue.async { [self] in
            timer?.cancel()
            timer = nil
            receiver.stop()
        }
    }

    private func poll() {
        let batch = receiver.poll(at: Date().timeIntervalSince1970)
        let totals: Totals = (receiver.linesReceived, receiver.fixCount, receiver.lastError)
        // Main-actor hops only when there is something to show.
        guard batch.stateChanged || !batch.aisLines.isEmpty || !batch.fixes.isEmpty
                || totals != reported else { return }
        reported = totals
        onBatch(batch, totals)
    }
}

/// Where AIS traffic comes from: the Open Waters network and a receiver on
/// the boat's network, each with its live status. AIS Traffic in the map menu
/// shows or hides both.
struct AisSettingsSheet: View {
    @ObservedObject var receiver: AisReceiver
    @ObservedObject var feed: AisFeed
    /// The AIS Traffic menu toggle; the sources only run while it is on.
    let trafficOn: Bool
    @Binding var openWaters: Bool
    let onApply: (AisReceiverSettings) -> Void
    let onClose: () -> Void

    @State private var draft = AisReceiverSettings.load()
    @State private var portText = ""

    var body: some View {
        NavigationStack {
            Form {
                if !trafficOn {
                    Section {
                        Text("AIS Traffic is off in the map menu, so no targets are shown.")
                            .foregroundStyle(.secondary)
                    }
                }
                Section {
                    Toggle("Open Waters Network", isOn: $openWaters)
                    if openWaters && trafficOn {
                        openWatersStatusRow
                    }
                } footer: {
                    Text("Vessels near the chart on screen from the Open Waters AIS network, over the internet.")
                }
                Section {
                    Toggle("Local Receiver", isOn: $draft.enabled)
                    if draft.enabled && (trafficOn || draft.usePosition) {
                        statusRow
                    }
                } footer: {
                    Text("An AIS receiver, multiplexer or Wi-Fi gateway on the boat's network that sends NMEA 0183.")
                }
                Section {
                    Picker("Protocol", selection: $draft.transport) {
                        ForEach(AisReceiverSettings.Transport.allCases) { Text($0.menuName).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    if draft.transport == .tcp {
                        LabeledContent("Address") {
                            TextField("192.168.1.1", text: $draft.host)
                                .multilineTextAlignment(.trailing)
                                .keyboardType(.URL)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                        }
                    }
                    LabeledContent("Port") {
                        TextField("10110", text: $portText)
                            .multilineTextAlignment(.trailing)
                            .keyboardType(.numberPad)
                    }
                    Picker("Common Ports", selection: Binding(
                        get: { AisReceiverSettings.presetPorts.contains(draft.port) ? draft.port : 0 },
                        set: { if $0 != 0 { draft.port = $0; portText = String($0); apply() } })) {
                        Text("10110 (NMEA)").tag(UInt16(10110))
                        Text("2000").tag(UInt16(2000))
                        if !AisReceiverSettings.presetPorts.contains(draft.port) {
                            Text("Other").tag(UInt16(0))
                        }
                    }
                } header: {
                    Text("Connection")
                } footer: {
                    Text(draft.transport == .tcp
                         ? "Osprey connects to the receiver. Check its manual or app for the address and port."
                         : "Osprey listens on this port. Set the receiver to send UDP to this phone's address; broadcast is not received.")
                }
                Section {
                    Toggle("Use Receiver Position", isOn: $draft.usePosition)
                } footer: {
                    Text("Takes the boat's GPS (RMC, GGA, HDT) for your position in place of the phone's, or the AIS transponder's own position report when no GPS sentences arrive. The phone's GPS returns if the receiver sends no position for 10 seconds.")
                }
            }
            .navigationTitle("AIS Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        apply()
                        onClose()
                    }
                }
            }
            .onAppear {
                draft = receiver.settings
                portText = String(draft.port)
            }
            .onChange(of: portText) { _, text in
                if let p = UInt16(text), p > 0 { draft.port = p }
            }
            .onChange(of: draft.enabled) { _, _ in apply() }
            .onChange(of: draft.usePosition) { _, _ in apply() }
            .onChange(of: draft.transport) { _, _ in apply() }
            .onSubmit { apply() }
        }
    }

    private func apply() {
        var next = draft
        next.host = next.host.trimmingCharacters(in: .whitespaces)
        if next != receiver.settings { onApply(next) }
    }

    private var openWatersStatusRow: some View {
        LabeledContent("Status") {
            switch feed.state {
            case .off: Text("Off")
            case .connecting: Text("Connecting…")
            case .live: Text("Receiving").foregroundStyle(.green)
            case .retrying(let reason):
                Text("Retrying: \(reason)")
                    .foregroundStyle(.red)
                    .lineLimit(2)
            }
        }
    }

    @ViewBuilder private var statusRow: some View {
        LabeledContent("Status") {
            switch receiver.status {
            case .off: Text("Off")
            case .connecting: Text(draft.transport == .udp ? "Listening…" : "Connecting…")
            case .live: Text("Receiving").foregroundStyle(.green)
            case .silent: Text("No data").foregroundStyle(.orange)
            case .retrying(let reason):
                Text(reason.isEmpty ? "Retrying" : "Retrying: \(reason)")
                    .foregroundStyle(.red)
                    .lineLimit(2)
            }
        }
        if receiver.linesReceived > 0 {
            LabeledContent("Sentences", value: "\(receiver.linesReceived)")
        }
        if draft.usePosition {
            LabeledContent("Position Fixes", value: "\(receiver.fixCount)")
        }
    }
}
