// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Foundation
import PippinKit
import os

/// Live AIS traffic from the Open Waters stream for the box around the chart
/// on screen.
///
/// Streams with the install's personal token (`OpenWatersIdentity`), or
/// anonymously while there is none. The box follows the view: a view the
/// current box no longer serves reconnects with a new box and the box's
/// snapshot. A dropped stream reconnects with backoff; a refused token is
/// dropped and the next connect mints another. Frames are batched and handed to
/// `deliver` at most twice a second; the map parses nothing here.
@MainActor
final class AisFeed: ObservableObject {
    enum State: Equatable {
        case off
        case connecting
        case live
        /// Waiting to reconnect after a failure, with the reason.
        case retrying(String)
    }

    @Published private(set) var state: State = .off
    /// The credit lines of the sources heard in the last half hour, split at
    /// sentence ends and without repeats ("Open Waters AIS (…)", "AISHub (…)").
    @Published private(set) var credits: [String] = []

    /// Called on the main actor with each batch of frames.
    var deliver: (([PPOpenWatersFrame]) -> Void)?

    private static let log = Logger(subsystem: "org.peregrine.Pippin", category: "ais")
    /// Fraction of the view's span added on each side of the subscribed box.
    private static let pad = 0.25
    private static let batchInterval: Duration = .milliseconds(500)
    private static let viewCheckInterval: Duration = .seconds(1)
    /// A source unheard this long drops out of `credits`.
    private static let creditLifetime: TimeInterval = 1800

    private var limits = PPOpenWatersLimits.anonymous()
    private var view: PPGeoBounds?
    private var box: PPGeoBounds?
    private var stream: Task<Void, Never>?
    private var viewCheck: Task<Void, Never>?
    private var flush: Task<Void, Never>?
    private var pending: [PPOpenWatersFrame] = []
    private var lastConnect: Date?
    private var retryDelay: TimeInterval = 2
    private var creditSeen: [String: Date] = [:]

    var isOn: Bool { state != .off }

    /// Connects for `view`.
    func start(view: PPGeoBounds) {
        self.view = view
        if state == .off { state = .connecting }
        connect()
    }

    /// Closes the stream and forgets the box and the credits.
    func stop() {
        stream?.cancel()
        viewCheck?.cancel()
        flush?.cancel()
        stream = nil
        viewCheck = nil
        flush = nil
        pending = []
        box = nil
        creditSeen = [:]
        credits = []
        state = .off
    }

    /// Records the visible chart; at most once a second it is compared with
    /// the subscribed box, and a box that no longer serves it is replaced.
    func viewChanged(_ view: PPGeoBounds) {
        guard isOn else { return }
        self.view = view
        guard viewCheck == nil else { return }
        viewCheck = Task { [weak self] in
            try? await Task.sleep(for: Self.viewCheckInterval)
            guard let self, !Task.isCancelled else { return }
            self.viewCheck = nil
            guard let view = self.view, let box = self.box,
                  !self.limits.box(box, serves: view) else { return }
            self.connect()
        }
    }

    // MARK: - Stream

    /// Opens a stream for the current view, replacing any open one. Waits
    /// out the tier's connect rate first, then fetches the token; the box is
    /// sized for the token's tier.
    private func connect() {
        guard view != nil else { return }
        stream?.cancel()
        box = nil
        let spacing = 60.0 / Double(max(limits.connectsPerMinute, 1))
        let wait = lastConnect.map { max(0, spacing - Date().timeIntervalSince($0)) } ?? 0
        stream = Task { [weak self] in
            if wait > 0 { try? await Task.sleep(for: .seconds(wait)) }
            let token = await OpenWatersIdentity.shared.credential()
            guard let self, !Task.isCancelled, let view = self.view else { return }
            if let token, token.limits.areaSquareDegrees > self.limits.areaSquareDegrees {
                self.limits = token.limits
            }
            let box = self.limits.subscriptionBox(for: view, pad: Self.pad)
            self.box = box
            let url = openWatersStreamURL(box: box, snapshot: true)
            self.lastConnect = Date()
            let end = await Self.read(url, token: token?.token) { [weak self] frame in
                self?.receive(frame)
            }
            guard !Task.isCancelled else { return }
            if end.refused, let token {
                await OpenWatersIdentity.shared.forget(token.token)
                self.limits = PPOpenWatersLimits.anonymous()
            }
            self.retry(after: end.reason)
        }
    }

    /// Reads `url` line by line until it ends, fails or is cancelled. Returns
    /// why it stopped, for the retry notice, and whether the server refused
    /// the token.
    nonisolated private static func read(
        _ url: URL, token: String?, onFrame: (PPOpenWatersFrame) async -> Void
    ) async -> (reason: String, refused: Bool) {
        var request = URLRequest(url: url)
        request.setValue("text/event-stream", forHTTPHeaderField: "Accept")
        request.setValue(OpenWatersIdentity.userAgent, forHTTPHeaderField: "User-Agent")
        if let token { request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization") }
        do {
            let (bytes, response) = try await URLSession.shared.bytes(for: request)
            if let http = response as? HTTPURLResponse, http.statusCode != 200 {
                let refused = token != nil && (http.statusCode == 401 || http.statusCode == 403)
                return ("HTTP \(http.statusCode)", refused)
            }
            for try await line in bytes.lines {
                if let frame = PPOpenWatersFrame(line: line) { await onFrame(frame) }
            }
            return ("stream ended", false)
        } catch {
            return (error.localizedDescription, false)
        }
    }

    private func retry(after reason: String) {
        Self.log.notice("AIS stream stopped: \(reason, privacy: .public)")
        state = .retrying(reason)
        let delay = retryDelay
        retryDelay = min(retryDelay * 2, 60)
        stream = Task { [weak self] in
            try? await Task.sleep(for: .seconds(delay))
            guard let self, !Task.isCancelled else { return }
            self.connect()
        }
    }

    private func receive(_ frame: PPOpenWatersFrame) {
        guard isOn else { return }
        switch frame.kind {
        case .welcome:
            if let limits = frame.limits {
                self.limits = limits
                Self.log.notice("AIS stream live, area \(limits.areaSquareDegrees) sq deg")
            }
            retryDelay = 2
            state = .live
        case .error:
            Self.log.notice("AIS stream error: \(frame.errorMessage, privacy: .public)")
        case .event:
            if !frame.attribution.isEmpty { creditSeen[frame.attribution] = Date() }
            pending.append(frame)
            scheduleFlush()
        default:
            break
        }
    }

    private func scheduleFlush() {
        guard flush == nil else { return }
        flush = Task { [weak self] in
            try? await Task.sleep(for: Self.batchInterval)
            guard let self, !Task.isCancelled else { return }
            self.flush = nil
            let batch = self.pending
            self.pending = []
            self.refreshCredits()
            if !batch.isEmpty { self.deliver?(batch) }
        }
    }

    private func refreshCredits() {
        let now = Date()
        creditSeen = creditSeen.filter { now.timeIntervalSince($0.value) < Self.creditLifetime }
        let next = AisCredits.lines(creditSeen.keys.sorted())
        if next != credits { credits = next }
    }
}

/// How the sources' credit lines are listed in the AIS sources card.
enum AisCredits {
    /// Splits each attribution at sentence ends and keeps the first copy of
    /// each sentence, so the "Open Waters AIS (…)" every source repeats
    /// appears once.
    static func lines(_ attributions: [String]) -> [String] {
        var seen = Set<String>()
        var out: [String] = []
        for attribution in attributions {
            for part in attribution.components(separatedBy: ". ") {
                var sentence = part.trimmingCharacters(in: .whitespaces)
                if sentence.hasSuffix(".") { sentence.removeLast() }
                if !sentence.isEmpty, seen.insert(sentence).inserted { out.append(sentence) }
            }
        }
        // The network's own credit leads.
        if let i = out.firstIndex(where: { $0.hasPrefix("Open Waters") }), i > 0 {
            out.insert(out.remove(at: i), at: 0)
        }
        return out
    }
}
