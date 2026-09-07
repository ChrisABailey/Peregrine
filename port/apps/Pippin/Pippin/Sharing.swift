// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Sharing.swift — everything that leaves the app: the share sheet wrapper,
// the library of recorded rides, a single point on its way out, and the
// problem-report library.
//
// A ride is a file that already exists and is shared by URL; a point is a
// document row that has to be written to a temporary file first. Both go out
// as GPX, which a watch, a bike computer, Strava, Garmin Connect, Gaia and
// Organic Maps all read. A shared point also carries an Apple Maps link, so a
// recipient with no mapping app still gets somewhere to tap.

import PippinKit
import SwiftUI
import UIKit

// MARK: - Share sheet

/// `UIActivityViewController`, wrapped. Not `ShareLink`, which shares one
/// item: a shared point is deliberately two, a `.gpx` and a link.
struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]
    /// The mail subject line, where the target has one.
    var subject: String = ""

    func makeUIViewController(context: Context) -> UIActivityViewController {
        // The subject travels via `UIActivityItemSource`, not
        // `setValue(_:forKey: "subject")`: that is KVC against a property
        // `UIActivityViewController` does not publish, and would raise
        // `NSUnknownKeyException` on any release that stops tolerating it.
        let wrapped = subject.isEmpty
            ? items
            : items.map { TitledItem(item: $0, subject: subject) as Any }
        return UIActivityViewController(activityItems: wrapped,
                                        applicationActivities: nil)
    }

    func updateUIViewController(_ controller: UIActivityViewController,
                                context: Context) {}
}

/// One shared item carrying a subject line.
private final class TitledItem: NSObject, UIActivityItemSource {
    private let item: Any
    private let subject: String

    init(item: Any, subject: String) {
        self.item = item
        self.subject = subject
    }

    /// Returns the real item, not an empty stand-in: the sheet types the
    /// share off the placeholder, so the wrong type here would offer the
    /// wrong targets.
    func activityViewControllerPlaceholderItem(_ controller: UIActivityViewController) -> Any {
        item
    }

    func activityViewController(_ controller: UIActivityViewController,
                                itemForActivityType type: UIActivity.ActivityType?) -> Any? {
        item
    }

    func activityViewController(_ controller: UIActivityViewController,
                                subjectForActivityType type: UIActivity.ActivityType?) -> String {
        subject
    }
}

/// A `.sheet(item:)` payload. A fresh id per presentation, so sharing the
/// same ride twice presents twice.
struct SharePayload: Identifiable {
    let id = UUID()
    let items: [Any]
    let subject: String
}

// MARK: - Recorded rides

/// One recorded ride on disk.
struct RecordedRide: Identifiable, Hashable {
    let url: URL
    /// The file's creation date, which is when the rider pressed record.
    /// Reading it costs a stat rather than parsing every file's stamps.
    let recordedAt: Date
    let byteCount: Int

    var id: URL { url }

    var title: String { RideLibrary.titleFormatter.string(from: recordedAt) }

    var subtitle: String {
        ByteCountFormatter.string(fromByteCount: Int64(byteCount),
                                  countStyle: .file)
    }
}

/// `Documents/trips/` and its contents. `Documents/` rather than `Caches/`
/// because it belongs to the user, survives an update, and is visible over
/// the USB cable.
enum RideLibrary {
    static let directoryName = "trips"

    /// The list row's title. The file name uses the same date so a rider sees
    /// one string in both places.
    static let titleFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter
    }()

    /// `ride-2026-08-21-1430.gpx`: sortable, readable, and free of the colons
    /// an ISO time would put in a file name.
    private static let fileNameFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd-HHmm"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        return formatter
    }()

    /// The rides directory, created if absent. Nil only if Foundation cannot
    /// name `Documents/`.
    static func directory() -> URL? {
        guard let documents = FileManager.default
            .urls(for: .documentDirectory, in: .userDomainMask).first else { return nil }
        let directory = documents.appendingPathComponent(directoryName, isDirectory: true)
        try? FileManager.default.createDirectory(at: directory,
                                                 withIntermediateDirectories: true)
        return directory
    }

    /// Where the ride starting now should be written. A ride begun in the
    /// same minute as an existing one gets a suffix rather than overwriting
    /// it, which is what a stop-and-restart does.
    static func newRideURL(at date: Date = Date()) -> URL? {
        guard let directory = directory() else { return nil }
        let stem = "ride-" + fileNameFormatter.string(from: date)
        var candidate = directory.appendingPathComponent(stem + ".gpx")
        var counter = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            candidate = directory.appendingPathComponent("\(stem)-\(counter).gpx")
            counter += 1
        }
        return candidate
    }

    /// Every recorded ride, newest first.
    static func rides() -> [RecordedRide] {
        guard let directory = directory(),
              let entries = try? FileManager.default.contentsOfDirectory(
                at: directory,
                includingPropertiesForKeys: [.creationDateKey, .fileSizeKey],
                options: [.skipsHiddenFiles]) else { return [] }
        return entries
            .filter { $0.pathExtension.lowercased() == "gpx" }
            .map { url in
                let values = try? url.resourceValues(forKeys: [.creationDateKey, .fileSizeKey])
                return RecordedRide(url: url,
                                    recordedAt: values?.creationDate ?? .distantPast,
                                    byteCount: values?.fileSize ?? 0)
            }
            .sorted { $0.recordedAt > $1.recordedAt }
    }

    static func delete(_ ride: RecordedRide) {
        try? FileManager.default.removeItem(at: ride.url)
    }
}

// MARK: - A single point, leaving the app

/// A point on its way out, two ways: opened in Apple Maps on this phone, or
/// shared as a `.gpx` plus a link. The shared link is the https form, not
/// `maps:`, so it means something to a recipient who is not on an Apple
/// device.
enum PointShare {
    /// The link that opens Apple Maps on this phone.
    static func mapsAppURL(for point: PPMapPoint) -> URL? {
        url(scheme: "maps://", for: point)
    }

    /// The link a recipient can tap anywhere.
    static func mapsWebURL(for point: PPMapPoint) -> URL? {
        url(scheme: "https://maps.apple.com/", for: point)
    }

    private static func url(scheme: String, for point: PPMapPoint) -> URL? {
        // `ll` is where to look and `q` is what to call the pin. Without `q`
        // Maps labels the pin with the coordinate.
        var components = URLComponents(string: scheme)
        var items = [URLQueryItem(name: "ll",
                                  value: String(format: "%.6f,%.6f",
                                                point.coordinate.latitude,
                                                point.coordinate.longitude))]
        let name = point.name.isEmpty ? "Dropped pin" : point.name
        items.append(URLQueryItem(name: "q", value: name))
        components?.queryItems = items
        return components?.url
    }

    /// Writes the point to a `.gpx` in the temporary directory and returns
    /// the share payload. The file is named after the point, since that name
    /// is what the recipient sees in Mail and Files; characters a file name
    /// cannot hold become dashes. A failed write still shares the link.
    static func payload(for point: PPMapPoint) -> SharePayload {
        let name = point.name.isEmpty ? "Pin" : point.name
        var items: [Any] = []

        let safe = name.map { $0.isLetter || $0.isNumber || $0 == " " || $0 == "-" ? $0 : "-" }
        let fileURL = FileManager.default.temporaryDirectory
            .appendingPathComponent(String(safe) + ".gpx")
        if (try? PPMap.writePoint(point, toGpxURL: fileURL)) != nil {
            items.append(fileURL)
        }
        if let web = mapsWebURL(for: point) { items.append(web) }

        return SharePayload(items: items, subject: name)
    }
}

// MARK: - Rides sheet

/// The list of recorded rides: start, stop, share, delete. Recording is
/// normally started from the map button in one tap; the stop control is here
/// too so a rider who opened the sheet mid-ride need not close it to finish.
struct RideSheet: View {
    let rides: [RecordedRide]
    let isRecording: Bool
    let recordingPointCount: Int
    let onToggleRecording: () -> Void
    /// Asks the model to re-read the recorder's count. Polled from this sheet
    /// rather than the render loop; see `MapModel.refreshRecordedCount()`.
    let onPollCount: () -> Void
    let onDelete: (RecordedRide) -> Void
    let onClose: () -> Void

    @State private var sharing: SharePayload?

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Button(action: onToggleRecording) {
                        Label {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(isRecording ? "Stop Recording" : "Record a Ride")
                                if isRecording {
                                    Text(recordingPointCount == 1
                                         ? "1 point so far"
                                         : "\(recordingPointCount) points so far")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                        } icon: {
                            Image(systemName: isRecording ? "stop.circle.fill" : "record.circle")
                                .foregroundStyle(.red)
                        }
                    }
                } footer: {
                    Text("A ride is written to the file as you go, so it "
                         + "survives the app being closed mid-ride.")
                }

                if rides.isEmpty {
                    Section {
                        Text("No saved rides yet.")
                            .foregroundStyle(.secondary)
                    }
                } else {
                    Section("Saved Rides") {
                        ForEach(rides) { ride in
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(ride.title)
                                    Text(ride.subtitle)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                // `.borderless`, or the row takes the tap and
                                // a tap anywhere in it shares.
                                Button {
                                    sharing = SharePayload(items: [ride.url],
                                                           subject: ride.title)
                                } label: {
                                    Image(systemName: "square.and.arrow.up")
                                }
                                .buttonStyle(.borderless)
                                .accessibilityLabel("Share \(ride.title)")
                            }
                        }
                        .onDelete { offsets in
                            offsets.map { rides[$0] }.forEach(onDelete)
                        }
                    }
                }
            }
            .navigationTitle("Rides")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
            .sheet(item: $sharing) { payload in
                ShareSheet(items: payload.items, subject: payload.subject)
            }
            // One second matches the receiver's cadence. The task is
            // cancelled with the sheet, which is why the poll lives here.
            .task(id: isRecording) {
                while isRecording && !Task.isCancelled {
                    onPollCount()
                    try? await Task.sleep(for: .seconds(1))
                }
            }
        }
    }
}

// MARK: - Problem reports

/// One saved report on disk.
struct SavedReport: Identifiable, Hashable {
    let url: URL
    let savedAt: Date
    /// The first line of what the rider typed, which is the list's title.
    let summary: String

    var id: URL { url }

    var subtitle: String { RideLibrary.titleFormatter.string(from: savedAt) }
}

/// `Documents/reports/` and its contents.
///
/// A report is a file before it is a message. The moment a rider notices a
/// path is missing is the moment they are standing where it should be, which
/// is not reliably a moment with signal. Save only writes to `Documents/`,
/// which cannot fail for want of a network; sending is a separate act from
/// the list. The queue is a directory: no reachability check, no retry queue,
/// no background task.
enum ReportLibrary {
    static let directoryName = "reports"

    private static let fileNameFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd-HHmm"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        return formatter
    }()

    private static let stampFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        return formatter
    }()

    static func directory() -> URL? {
        guard let documents = FileManager.default
            .urls(for: .documentDirectory, in: .userDomainMask).first else { return nil }
        let directory = documents.appendingPathComponent(directoryName, isDirectory: true)
        try? FileManager.default.createDirectory(at: directory,
                                                 withIntermediateDirectories: true)
        return directory
    }

    /// Writes one report and returns it, or nil if the write failed. The
    /// version and the map centre are captured rather than asked for: they
    /// are what the reader needs and the rider cannot supply.
    @discardableResult
    static func save(text: String, coordinateText: String,
                     at date: Date = Date()) -> SavedReport? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, let directory = directory() else { return nil }

        let stem = "report-" + fileNameFormatter.string(from: date)
        var url = directory.appendingPathComponent(stem + ".txt")
        var counter = 2
        while FileManager.default.fileExists(atPath: url.path) {
            url = directory.appendingPathComponent("\(stem)-\(counter).txt")
            counter += 1
        }

        let contents = """
            \(AppName.display) problem report
            Saved: \(stampFormatter.string(from: date))
            Version: \(AppVersion.marketing) (\(AppVersion.build))
            Map center: \(coordinateText)

            \(trimmed)

            """
        guard (try? contents.write(to: url, atomically: true,
                                   encoding: .utf8)) != nil else { return nil }
        return SavedReport(url: url, savedAt: date, summary: firstLine(of: trimmed))
    }

    /// Every saved report, newest first.
    static func reports() -> [SavedReport] {
        guard let directory = directory(),
              let entries = try? FileManager.default.contentsOfDirectory(
                at: directory,
                includingPropertiesForKeys: [.creationDateKey],
                options: [.skipsHiddenFiles]) else { return [] }
        return entries
            .filter { $0.pathExtension.lowercased() == "txt" }
            .map { url in
                let values = try? url.resourceValues(forKeys: [.creationDateKey])
                return SavedReport(url: url,
                                   savedAt: values?.creationDate ?? .distantPast,
                                   summary: summary(of: url))
            }
            .sorted { $0.savedAt > $1.savedAt }
    }

    /// The whole file, which is what goes in the mail body.
    static func body(of report: SavedReport) -> String {
        (try? String(contentsOf: report.url, encoding: .utf8)) ?? ""
    }

    static func delete(_ report: SavedReport) {
        try? FileManager.default.removeItem(at: report.url)
    }

    /// The rider's first line, recovered from a saved file. The header is
    /// four lines and a blank one. A file that does not match that shape
    /// falls back to its own first line.
    private static func summary(of url: URL) -> String {
        guard let contents = try? String(contentsOf: url, encoding: .utf8) else { return "" }
        let parts = contents.components(separatedBy: "\n\n")
        return firstLine(of: parts.count > 1 ? parts[1] : contents)
    }

    private static func firstLine(of text: String) -> String {
        text.split(separator: "\n", omittingEmptySubsequences: true)
            .first.map(String.init)?
            .trimmingCharacters(in: .whitespaces) ?? ""
    }
}

/// Where a report is sent. The address lives in `Info.plist`
/// (`PPReportAddress`) so it can be changed without rebuilding Swift, and is
/// expected to be an iCloud Hide My Email alias that can be rotated. The
/// transport is the rider's own mail app, so there is no credential in the
/// binary. An unset key is handled: the sheet says so and offers the share
/// sheet instead.
enum ReportMail {
    static var address: String? {
        let value = Bundle.main
            .object(forInfoDictionaryKey: "PPReportAddress") as? String
        let trimmed = value?.trimmingCharacters(in: .whitespaces) ?? ""
        return trimmed.isEmpty ? nil : trimmed
    }

    /// A `mailto:` with the subject and whole report already filled in.
    ///
    /// Built by hand rather than with `URLComponents`, whose `queryItems`
    /// setter leaves `+ ? , / :` literal. A literal `+` in a body is read as
    /// a space by mail clients following the form-encoding convention, so
    /// "a 2+ foot washout" would arrive as "a 2 foot washout": a report
    /// quietly altered rather than visibly broken. Encoding against the RFC
    /// 3986 unreserved set over-encodes slightly and cannot be misread.
    static func mailURL(for report: SavedReport) -> URL? {
        guard let address else { return nil }
        let subject = escape("\(AppName.display) report \(report.subtitle)")
        let body = escape(ReportLibrary.body(of: report))
        return URL(string: "mailto:\(address)?subject=\(subject)&body=\(body)")
    }

    private static let unreserved: CharacterSet = {
        var set = CharacterSet.alphanumerics
        set.insert(charactersIn: "-._~")
        return set
    }()

    private static func escape(_ text: String) -> String {
        text.addingPercentEncoding(withAllowedCharacters: unreserved) ?? ""
    }
}
