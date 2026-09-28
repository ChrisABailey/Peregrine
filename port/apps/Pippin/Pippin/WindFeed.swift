// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Foundation
import PippinKit
import os

/// The National Weather Service wind forecast for the pack's beach, fetched
/// on demand and kept for an hour.
///
/// The request names a fixed point from `[wind]`, never the rider's position.
/// The last forecast is cached on disk and used while offline; the card shows
/// when NWS updated it. Views observe `shared`, so a sheet sees a forecast
/// that arrives after it opened.
@MainActor
final class WindFeed: ObservableObject {
    static let shared = WindFeed()

    @Published private(set) var forecast: PPWindForecast?
    private(set) var settings: PPWindSettings?

    /// How long a fetched forecast is used before asking again.
    private static let freshFor: TimeInterval = 3600
    private static let log = Logger(subsystem: "org.peregrine.Pippin", category: "wind")

    private var fetchedAt: Date?
    private var fetching = false

    private init() {}

    /// Takes the pack's settings and loads any cached forecast for its point.
    /// Nil turns the wind off.
    func configure(_ settings: PPWindSettings?) {
        self.settings = settings
        forecast = nil
        fetchedAt = nil
        guard let url = cacheURL,
              let data = try? Data(contentsOf: url),
              let cached = try? PPWindForecast(json: data) else { return }
        forecast = cached
        fetchedAt = (try? url.resourceValues(forKeys: [.contentModificationDateKey]))?
            .contentModificationDate
    }

    /// Fetches unless the forecast is under an hour old or a fetch is running.
    func refresh() {
        guard let settings, !fetching else { return }
        if let fetchedAt, Date().timeIntervalSince(fetchedAt) < Self.freshFor { return }
        fetching = true
        Task {
            defer { fetching = false }
            do {
                let data = try await Self.fetch(settings)
                let fresh = try PPWindForecast(json: data)
                forecast = fresh
                fetchedAt = Date()
                if let url = cacheURL { try? data.write(to: url, options: .atomic) }
            } catch {
                Self.log.notice("wind fetch failed: \(error.localizedDescription, privacy: .public)")
            }
        }
    }

    // MARK: - api.weather.gov

    /// `/points/{lat},{lon}` names the forecast office and grid cell; the
    /// cell's URL is remembered, so later fetches are one request.
    private static func fetch(_ settings: PPWindSettings) async throws -> Data {
        let key = "PPWindGrid-\(pointText(settings))"
        if let saved = UserDefaults.standard.string(forKey: key), let url = URL(string: saved) {
            if let data = try? await get(url) { return data }
        }
        let points = URL(string: "https://api.weather.gov/points/\(pointText(settings))")!
        let meta = try JSONSerialization.jsonObject(with: try await get(points))
        guard let props = (meta as? [String: Any])?["properties"] as? [String: Any],
              let grid = props["forecastGridData"] as? String,
              let gridURL = URL(string: grid) else {
            throw URLError(.cannotParseResponse)
        }
        UserDefaults.standard.set(grid, forKey: key)
        return try await get(gridURL)
    }

    /// NWS refuses requests without a User-Agent that identifies the app.
    private static func get(_ url: URL) async throws -> Data {
        var request = URLRequest(url: url, timeoutInterval: 15)
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
            as? String ?? "0"
        request.setValue("Pippin/\(version) (github.com/ChrisABailey/Peregrine)",
                         forHTTPHeaderField: "User-Agent")
        request.setValue("application/geo+json", forHTTPHeaderField: "Accept")
        let (data, response) = try await URLSession.shared.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else {
            throw URLError(.badServerResponse)
        }
        return data
    }

    /// Four decimals, as NWS redirects anything finer.
    private static func pointText(_ s: PPWindSettings) -> String {
        String(format: "%.4f,%.4f", s.latitude, s.longitude)
    }

    private var cacheURL: URL? {
        guard let settings else { return nil }
        return FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first?
            .appendingPathComponent("wind-\(Self.pointText(settings)).json")
    }
}
