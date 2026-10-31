// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ChartDownloader.swift — downloads a NOAA regional ENC zip on a background
// URLSession, so a download continues while the app is suspended and resumes
// after a dropped connection. The finished zip goes to the chart import.

import Foundation

/// One background chart download at a time. Callbacks arrive on the main actor.
final class ChartDownloader: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    static let shared = ChartDownloader()

    /// Session identifier; `PippinApp` hands this session's background events back.
    static let sessionIdentifier = (Bundle.main.bundleIdentifier ?? "Pippin") + ".charts"

    /// Fraction downloaded, or nil while the size is unknown.
    var onProgress: (@MainActor (ChartRegion, Double?) -> Void)?
    /// The zip is in place at the URL; the receiver owns and deletes it.
    var onFinished: (@MainActor (ChartRegion, URL) -> Void)?
    /// The download failed; the Bool says whether resume data was kept.
    var onFailed: (@MainActor (ChartRegion, String, Bool) -> Void)?

    private let lock = NSLock()
    private var activeTask: URLSessionDownloadTask?
    private var eventsDone: CheckedContinuation<Void, Never>?
    /// The session finished its background events before anyone waited.
    private var eventsFinishedEarly = false
    /// A zip that finished before anyone listened (a relaunch into background events).
    private var unclaimed: (ChartRegion, URL)?

    private lazy var session: URLSession = {
        let config = URLSessionConfiguration.background(withIdentifier: Self.sessionIdentifier)
        config.sessionSendsLaunchEvents = true
        return URLSession(configuration: config, delegate: self, delegateQueue: nil)
    }()

    /// `Library/Application Support/Charts/Downloads`, beside the chart store.
    static func downloadsURL() -> URL? {
        guard let url = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
            .appendingPathComponent("Charts/Downloads", isDirectory: true) else { return nil }
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private static func resumeDataURL(_ region: ChartRegion) -> URL? {
        downloadsURL()?.appendingPathComponent("\(region.id).resume")
    }

    /// Reconnects to a download left running by an earlier launch and reports
    /// it as in progress. Returns a zip that finished while nobody listened.
    func attach() async -> (ChartRegion, URL)? {
        let tasks = await session.allTasks
        if let task = tasks.compactMap({ $0 as? URLSessionDownloadTask })
            .first(where: { $0.state == .running || $0.state == .suspended }),
           let region = task.taskDescription.flatMap(ChartRegion.named) {
            lock.withLock { activeTask = task }
            let expected = task.countOfBytesExpectedToReceive
            let fraction = expected > 0 ? Double(task.countOfBytesReceived) / Double(expected) : nil
            await MainActor.run { onProgress?(region, fraction) }
        }
        return lock.withLock {
            defer { unclaimed = nil }
            return unclaimed
        }
    }

    var isDownloading: Bool { lock.withLock { activeTask != nil } }

    /// Regions with resume data from a failed download.
    func resumableRegionIDs() -> Set<String> {
        guard let dir = Self.downloadsURL(),
              let names = try? FileManager.default.contentsOfDirectory(atPath: dir.path)
        else { return [] }
        return Set(names.filter { $0.hasSuffix(".resume") }.map { String($0.dropLast(7)) })
    }

    /// Starts `region`, from its resume data when a failed attempt left some.
    func start(_ region: ChartRegion) {
        lock.lock()
        defer { lock.unlock() }
        guard activeTask == nil else { return }
        var task: URLSessionDownloadTask
        if let resumeURL = Self.resumeDataURL(region),
           let data = try? Data(contentsOf: resumeURL) {
            try? FileManager.default.removeItem(at: resumeURL)
            task = session.downloadTask(withResumeData: data)
        } else {
            task = session.downloadTask(with: region.url)
        }
        task.taskDescription = region.id
        activeTask = task
        task.resume()
    }

    /// Stops the download and discards what it fetched.
    func cancel() {
        let task = lock.withLock { () -> URLSessionDownloadTask? in
            defer { activeTask = nil }
            return activeTask
        }
        task?.cancel()
    }

    /// Waits until the session has delivered the events iOS woke the app for.
    func handleBackgroundEvents() async {
        _ = session
        await withCheckedContinuation { continuation in
            let early = lock.withLock { () -> Bool in
                defer { eventsFinishedEarly = false }
                if !eventsFinishedEarly { eventsDone = continuation }
                return eventsFinishedEarly
            }
            if early { continuation.resume() }
        }
    }

    // MARK: - URLSessionDownloadDelegate

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didWriteData bytesWritten: Int64, totalBytesWritten: Int64,
                    totalBytesExpectedToWrite: Int64) {
        guard let region = downloadTask.taskDescription.flatMap(ChartRegion.named) else { return }
        let fraction = totalBytesExpectedToWrite > 0
            ? Double(totalBytesWritten) / Double(totalBytesExpectedToWrite) : nil
        Task { @MainActor in self.onProgress?(region, fraction) }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didFinishDownloadingTo location: URL) {
        guard let region = downloadTask.taskDescription.flatMap(ChartRegion.named) else { return }
        let status = (downloadTask.response as? HTTPURLResponse)?.statusCode ?? 0
        guard status == 200 || status == 206 else {
            finish(downloadTask)
            report(region, ChartDownloadText.httpFailure(status), resumable: false)
            return
        }
        // `location` is deleted when this returns, so the move happens here.
        guard let dir = Self.downloadsURL() else { return }
        let zip = dir.appendingPathComponent("\(region.id)_ENCs.zip")
        try? FileManager.default.removeItem(at: zip)
        do {
            try FileManager.default.moveItem(at: location, to: zip)
        } catch {
            finish(downloadTask)
            report(region, error.localizedDescription, resumable: false)
            return
        }
        finish(downloadTask)
        let listening = lock.withLock { () -> Bool in
            if onFinished == nil { unclaimed = (region, zip) }
            return onFinished != nil
        }
        if listening { Task { @MainActor in self.onFinished?(region, zip) } }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask,
                    didCompleteWithError error: Error?) {
        guard let error, let region = task.taskDescription.flatMap(ChartRegion.named) else { return }
        finish(task)
        let nsError = error as NSError
        // A cancel from the app has no resume data and needs no notice.
        if nsError.domain == NSURLErrorDomain, nsError.code == NSURLErrorCancelled,
           nsError.userInfo[NSURLSessionDownloadTaskResumeData] == nil { return }
        var resumable = false
        if let data = nsError.userInfo[NSURLSessionDownloadTaskResumeData] as? Data,
           let url = Self.resumeDataURL(region) {
            resumable = (try? data.write(to: url, options: .atomic)) != nil
        }
        report(region, error.localizedDescription, resumable: resumable)
    }

    func urlSessionDidFinishEvents(forBackgroundURLSession session: URLSession) {
        let continuation = lock.withLock { () -> CheckedContinuation<Void, Never>? in
            defer { eventsDone = nil }
            if eventsDone == nil { eventsFinishedEarly = true }
            return eventsDone
        }
        continuation?.resume()
    }

    private func finish(_ task: URLSessionTask) {
        lock.withLock { if activeTask === task { activeTask = nil } }
    }

    private func report(_ region: ChartRegion, _ reason: String, resumable: Bool) {
        Task { @MainActor in self.onFailed?(region, reason, resumable) }
    }
}

/// NOAA's ENC product catalog (about 10 MB of XML), cached between checks.
enum ChartCatalog {
    static let url = URL(string: "https://www.charts.noaa.gov/ENCs/ENCProdCat.xml")!
    private static let etagKey = "chartCatalogETag"

    /// `Library/Caches/ENCProdCat.xml`.
    static func cacheURL() -> URL? {
        FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first?
            .appendingPathComponent("ENCProdCat.xml")
    }

    /// The current catalog on disk. The cached copy is reused when NOAA
    /// answers 304 to its ETag. `allowsExpensive` false keeps the fetch off
    /// cellular and Low Data Mode, for a check nobody asked for.
    static func fetch(allowsExpensive: Bool) async throws -> URL {
        guard let cache = cacheURL() else { throw URLError(.cannotCreateFile) }
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData)
        request.allowsExpensiveNetworkAccess = allowsExpensive
        request.allowsConstrainedNetworkAccess = allowsExpensive
        let defaults = UserDefaults.standard
        let cached = FileManager.default.fileExists(atPath: cache.path)
        if cached, let etag = defaults.string(forKey: etagKey) {
            request.setValue(etag, forHTTPHeaderField: "If-None-Match")
        }
        let (file, response) = try await URLSession.shared.download(for: request)
        defer { try? FileManager.default.removeItem(at: file) }
        let http = response as? HTTPURLResponse
        switch http?.statusCode ?? 0 {
        case 304 where cached:
            return cache
        case 200:
            try? FileManager.default.removeItem(at: cache)
            try FileManager.default.moveItem(at: file, to: cache)
            defaults.set(http?.value(forHTTPHeaderField: "ETag"), forKey: etagKey)
            return cache
        case let status:
            throw URLError(.badServerResponse, userInfo: [
                NSLocalizedDescriptionKey: ChartDownloadText.httpFailure(status)])
        }
    }
}
