// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PlaceLink.swift — turning a share from another app into a point.
//
// `Sharing.swift` is the way out; this is the way in. The one question is
// whether what arrives carries a latitude and longitude. What actually
// arrives, measured:
//
//   * Apple Maps, iOS 18 and the iOS 26 simulator — a full URL with the
//     coordinate written out, plus the postal address:
//       https://maps.apple.com/place?address=…&coordinate=37.334859,-122.009040
//       &name=Apple%20Park&place-id=…
//   * Apple Maps, iOS 26 on a real device — often a short link with nothing
//     in it (https://maps.apple/p/U8rE9v8n8iVZjr). The coordinate is only
//     recoverable by following the redirects and reading an INTERMEDIATE hop;
//     the last hop is an "unsupported" page.
//   * Google Maps — always a short link, whose redirect lands on a
//     `/maps/place/…` URL carrying `!3d<lat>!4d<lng>`.
//   * A `geo:` URI, a vCard, or a pasted "32.60841, -80.07213".
//
// So the parse is offline and the lookup is the fallback: `place(in:)` never
// touches the network and answers nil rather than guessing, and `resolve(_:)`
// is the only networking anywhere in Pippin. See `allowsNetworkLookup`.
//
// Foundation only. This file is compiled into the app and into the share
// extension, and must not pull PippinKit (and the Peregrine core) into a
// process the system gives a few dozen megabytes. It also runs under a bare
// `swift` on the mac, which is how its table of real-world URLs is tested.

import Foundation
import os

// MARK: - Logging

/// The share path's log, written by both processes.
///
/// A share extension has no console: it is a second process, `devicectl` is
/// not attached to it, and a failure shows the user only a sheet that closes.
/// `.notice` rather than `.debug` because debug and info are not persisted,
/// and `privacy: .public` or the URL prints as `<private>`.
///
///   * Console.app: pick the phone, filter on the subsystem below.
///   * Terminal, for the app's half only:
///       xcrun devicectl device process launch --console --device "$IPHONE" \
///           org.peregrine.Pippin
enum PippinLog {
    static let share = Logger(subsystem: "org.peregrine.Pippin", category: "share")
}

// MARK: - The parsed place

/// A place that arrived from another app. The address is separate from the
/// name because they land in different columns: the name is the marker's, the
/// address goes in the remarks.
struct SharedPlace: Equatable {
    var latitude: Double
    var longitude: Double
    var name: String
    var address: String

    /// Rejects a coordinate that is out of range or at the origin. `q=0,0` is
    /// Apple's spelling for "no coordinate, search the text instead", and
    /// believing it would drop a marker in the Gulf of Guinea.
    init?(latitude: Double, longitude: Double, name: String = "", address: String = "") {
        guard latitude.isFinite, longitude.isFinite,
              latitude >= -90, latitude <= 90,
              longitude >= -180, longitude <= 180,
              abs(latitude) > 1e-9 || abs(longitude) > 1e-9 else { return nil }
        self.latitude = latitude
        self.longitude = longitude
        self.name = name
        self.address = address
    }

    /// The name for a share that carried none. Not "Dropped pin", which is
    /// what Maps calls its own and would read as though Pippin made it.
    static let fallbackName = "Shared place"

    /// `name`, falling back to the street line and then to `fallbackName`.
    var displayName: String {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty { return trimmed }
        let street = address.split(separator: ",").first.map(String.init) ?? ""
        let trimmedStreet = street.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmedStreet.isEmpty ? Self.fallbackName : trimmedStreet
    }
}

// MARK: - Parser

enum PlaceLink {
    /// Whether `resolve(_:)` may use the network.
    ///
    /// Pippin's location string promises that ride data never leaves the
    /// phone, and that promise holds: none of it passes through here. What
    /// goes out is one link the user just handed to Pippin, sent to the
    /// company that minted it. Set this false and every short link becomes an
    /// honest "that link does not carry a position"; nothing else changes.
    static let allowsNetworkLookup = true

    // MARK: Offline

    /// A place out of a URL, or nil. Never touches the network.
    static func place(in url: URL) -> SharedPlace? {
        let scheme = (url.scheme ?? "").lowercased()

        // `geo:` is its own grammar, not a query string: the coordinate is
        // the opaque part and the label rides in the `q` parameter.
        if scheme == "geo" { return placeInGeoURI(url) }

        let text = url.absoluteString
        let items = queryItems(of: url)

        var found: Coordinate?
        var label = ""

        // Order matters. `coordinate` is modern Apple Maps and is the place
        // itself; `ll` is the older spelling; `sll` and `center` are where
        // the camera was, so they come later; `q` is last because it is
        // usually a name.
        for key in ["coordinate", "ll", "q", "query", "daddr", "destination",
                    "point", "sll", "center", "cp"] {
            guard let raw = items[key] else { continue }
            if let parsed = coordinate(in: raw) {
                found = parsed.coordinate
                label = parsed.label
                break
            }
        }

        // OpenStreetMap splits the pair over two keys.
        if found == nil, let lat = items["mlat"], let lon = items["mlon"] {
            found = coordinate(latitude: lat, longitude: lon)
        }

        if found == nil { found = coordinateInPath(text) }

        guard let coordinate = found else { return nil }

        let name = firstNonEmpty([
            items["name"],                 // Apple's `place?name=`
            label,                         // the `(Label)` in a `q=lat,lon(…)`
            nonCoordinate(items["q"]),     // Apple's older `?ll=…&q=Name`
            nonCoordinate(items["query"]),
            googlePlaceName(text),         // `/maps/place/<Name>/@…`
        ])
        let address = firstNonEmpty([items["address"], items["daddr_name"]])

        return SharedPlace(latitude: coordinate.latitude,
                           longitude: coordinate.longitude,
                           name: name,
                           address: address)
    }

    /// A place out of shared text. Google Maps' share hands over a string
    /// (the place's name on one line, the short link on the next), so the URL
    /// is found and parsed and the surrounding prose becomes the name when
    /// the URL carried none.
    static func place(in text: String) -> SharedPlace? {
        if let card = placeInVCard(text) { return card }

        if let url = firstURL(in: text), var place = place(in: url) {
            if place.name.isEmpty {
                place.name = text.replacingOccurrences(of: url.absoluteString,
                                                       with: "")
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                    .components(separatedBy: .newlines)
                    .first?
                    .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            }
            return place
        }

        // A bare pair somebody typed or pasted.
        if let parsed = coordinate(in: text) {
            return SharedPlace(latitude: parsed.coordinate.latitude,
                               longitude: parsed.coordinate.longitude,
                               name: parsed.label)
        }
        return nil
    }

    /// The first URL in a string, with or without a scheme. `NSDataDetector`
    /// rather than a regex, so what counts as a link here is what iOS just
    /// underlined for the user.
    static func firstURL(in text: String) -> URL? {
        let detector = try? NSDataDetector(types: NSTextCheckingResult.CheckingType.link.rawValue)
        let range = NSRange(text.startIndex..., in: text)
        return detector?.firstMatch(in: text, range: range)?.url
    }

    /// Whether a link is worth a network lookup: http(s) and its offline
    /// parse found nothing. Deliberately not a host whitelist, which would
    /// have been wrong the day Apple minted the `maps.apple` domain.
    static func mayBeShortened(_ url: URL) -> Bool {
        let scheme = (url.scheme ?? "").lowercased()
        return (scheme == "http" || scheme == "https") && place(in: url) == nil
    }

    // MARK: Network

    /// Follows a short link until a hop admits a coordinate.
    ///
    /// The answer is in the middle of the chain, not at the end:
    /// `https://maps.apple/p/XXXX` redirects to a full `maps.apple.com/place?
    /// …&coordinate=…` and then on to an "unsupported" page, so a plain fetch
    /// returns the useless last hop. Every proposed hop is inspected instead
    /// and the first that parses wins.
    ///
    /// The shape is undocumented and will break. When it does the failure is
    /// a sentence on screen and a share that did nothing, never a marker in
    /// the wrong place.
    static func resolve(_ url: URL) async -> SharedPlace? {
        guard allowsNetworkLookup else {
            PippinLog.share.notice("resolve refused: network lookup is off")
            return nil
        }
        PippinLog.share.notice("resolve: following \(url.absoluteString, privacy: .public)")

        let sniffer = RedirectSniffer()
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 10
        configuration.timeoutIntervalForResource = 12
        configuration.httpCookieStorage = nil
        configuration.urlCache = nil
        let session = URLSession(configuration: configuration,
                                 delegate: sniffer,
                                 delegateQueue: nil)
        defer { session.invalidateAndCancel() }

        var request = URLRequest(url: url)
        // A desktop browser agent: Google serves an unrecognised agent a
        // consent interstitial with no coordinate in it, and Apple's
        // short-link host serves the redirect chain only to a browser.
        request.setValue(
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
                + "(KHTML, like Gecko) Version/17.0 Safari/605.1.15",
            forHTTPHeaderField: "User-Agent")

        let body: Data?
        do {
            (body, _) = try await session.data(for: request) as (Data, URLResponse)
        } catch {
            // A cancelled task is what success looks like: the sniffer stops
            // the chain as soon as it has an answer.
            body = nil
        }

        if let hit = sniffer.found {
            PippinLog.share.notice(
                "resolve: a redirect carried \(hit.latitude, privacy: .public),\(hit.longitude, privacy: .public)")
            return hit
        }

        // Last resort: the page body, which carries the same `!3d…!4d…`. The
        // bytes are already here.
        if let body, let text = String(data: body.prefix(512 * 1024), encoding: .utf8) {
            if let coordinate = coordinateInPath(text) {
                PippinLog.share.notice("resolve: the page body carried a coordinate")
                return SharedPlace(latitude: coordinate.latitude,
                                   longitude: coordinate.longitude,
                                   name: googlePlaceName(text))
            }
        }
        PippinLog.share.error(
            "resolve: no coordinate anywhere in the chain from \(url.absoluteString, privacy: .public)")
        return nil
    }

    /// Watches a redirect chain and keeps the first hop that is a place. A
    /// class because `URLSession` wants a delegate object, locked because the
    /// delegate queue is not the caller's.
    private final class RedirectSniffer: NSObject, URLSessionTaskDelegate {
        private let lock = NSLock()
        private var hit: SharedPlace?

        var found: SharedPlace? {
            lock.lock()
            defer { lock.unlock() }
            return hit
        }

        func urlSession(_ session: URLSession,
                        task: URLSessionTask,
                        willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest,
                        completionHandler: @escaping (URLRequest?) -> Void) {
            if let url = request.url, let place = PlaceLink.place(in: url) {
                lock.lock()
                hit = place
                lock.unlock()
                // Nil stops the chain: the next hop throws the answer away.
                completionHandler(nil)
                return
            }
            completionHandler(request)
        }
    }

    // MARK: - Handoff to the app

    /// Pippin's own scheme, used by the share extension to hand a parsed
    /// place to the app. Registered in `Pippin/Info.plist`.
    static let scheme = "pippin"

    /// `pippin://place?lat=…&lon=…&name=…&address=…`.
    ///
    /// The whole place travels in the URL. The alternative, an App Group
    /// container, costs an entitlement on both targets and a group id
    /// registered with Apple before a device build will sign. Four short
    /// fields fit in a URL, and a URL needs no entitlement.
    static func callbackURL(for place: SharedPlace) -> URL? {
        var components = URLComponents()
        components.scheme = scheme
        components.host = "place"
        components.queryItems = [
            URLQueryItem(name: "lat", value: String(format: "%.7f", place.latitude)),
            URLQueryItem(name: "lon", value: String(format: "%.7f", place.longitude)),
            URLQueryItem(name: "name", value: place.name),
            URLQueryItem(name: "address", value: place.address),
        ]
        return components.url
    }

    /// The other end of `callbackURL(for:)`. Anything not ours, or ours and
    /// malformed, is nil: a URL from outside is input, not instruction.
    static func place(inCallback url: URL) -> SharedPlace? {
        guard (url.scheme ?? "").lowercased() == scheme,
              (url.host ?? "").lowercased() == "place" else { return nil }
        let items = queryItems(of: url)
        guard let lat = Double(items["lat"] ?? ""),
              let lon = Double(items["lon"] ?? "") else { return nil }
        return SharedPlace(latitude: lat, longitude: lon,
                           name: items["name"] ?? "",
                           address: items["address"] ?? "")
    }

    // MARK: - Small parsers

    struct Coordinate: Equatable {
        var latitude: Double
        var longitude: Double
    }

    private struct ParsedCoordinate {
        var coordinate: Coordinate
        var label: String
    }

    /// Query parameters with lowercased keys, first spelling wins.
    private static func queryItems(of url: URL) -> [String: String] {
        guard let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              let items = components.queryItems else { return [:] }
        var result: [String: String] = [:]
        for item in items {
            let key = item.name.lowercased()
            guard result[key] == nil, let value = item.value, !value.isEmpty else { continue }
            result[key] = value
        }
        return result
    }

    /// `geo:32.60841,-80.07213?q=32.60841,-80.07213(Beachwalker%20Park)`.
    ///
    /// The pair before the `?` is authoritative and the `q` repeats it with a
    /// name attached, except in the Android idiom where the leading pair is
    /// `0,0` and everything is in the `q`. Both are read, and
    /// `SharedPlace`'s zero check decides.
    private static func placeInGeoURI(_ url: URL) -> SharedPlace? {
        let body = url.absoluteString.dropFirst("geo:".count)
        let head = body.split(separator: "?", maxSplits: 1,
                              omittingEmptySubsequences: false)
        let leading = coordinate(in: String(head.first ?? ""))
        var query: ParsedCoordinate?
        if head.count > 1 {
            for pair in head[1].split(separator: "&") {
                let kv = pair.split(separator: "=", maxSplits: 1)
                guard kv.count == 2, kv[0].lowercased() == "q" else { continue }
                let decoded = String(kv[1]).removingPercentEncoding ?? String(kv[1])
                query = coordinate(in: decoded)
            }
        }
        let chosen = query ?? leading
        guard let chosen else { return nil }
        let best = SharedPlace(latitude: chosen.coordinate.latitude,
                               longitude: chosen.coordinate.longitude,
                               name: chosen.label)
        if let best { return best }
        guard let leading else { return nil }
        return SharedPlace(latitude: leading.coordinate.latitude,
                           longitude: leading.coordinate.longitude,
                           name: query?.label ?? leading.label)
    }

    /// A vCard's `GEO`, which is what a shared Contact carries. Both
    /// spellings: 3.0 is `GEO:32.6;-80.07` and 4.0 is `GEO:geo:32.6,-80.07`.
    static func placeInVCard(_ text: String) -> SharedPlace? {
        guard text.uppercased().contains("BEGIN:VCARD") else { return nil }
        var coordinate: Coordinate?
        var name = ""
        var address = ""
        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.trimmingCharacters(in: .whitespacesAndNewlines)
            // A property may carry parameters (`ADR;TYPE=WORK:`), so the key
            // is everything before the first `;` or `:`.
            guard let colon = line.firstIndex(of: ":") else { continue }
            let head = String(line[line.startIndex..<colon])
            let key = head.split(separator: ";").first.map {
                $0.uppercased()
            } ?? head.uppercased()
            let value = String(line[line.index(after: colon)...])
            switch key {
            case "GEO":
                let cleaned = value
                    .replacingOccurrences(of: "geo:", with: "")
                    .replacingOccurrences(of: ";", with: ",")
                coordinate = self.coordinate(in: cleaned)?.coordinate
            case "FN" where name.isEmpty:
                name = value
            case "ADR":
                // The seven semicolon-separated fields as a human writes
                // them: everything after PO box and extended address.
                let parts = value.components(separatedBy: ";")
                address = parts.dropFirst(2)
                    .map { $0.trimmingCharacters(in: .whitespaces) }
                    .filter { !$0.isEmpty }
                    .joined(separator: ", ")
            default:
                break
            }
        }
        guard let coordinate else { return nil }
        return SharedPlace(latitude: coordinate.latitude,
                           longitude: coordinate.longitude,
                           name: name, address: address)
    }

    /// Parses `"32.60841, -80.07213"`, `"32.60841,-80.07213(Beachwalker
    /// Park)"` or `"32.60841° N, 80.07213° W"`, and returns nil for a name.
    private static func coordinate(in raw: String) -> ParsedCoordinate? {
        var text = (raw.removingPercentEncoding ?? raw)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        var label = ""

        // Strip the `(Label)` suffix first, so the number parse stays a plain
        // split on a comma.
        if let open = text.firstIndex(of: "("), text.hasSuffix(")") {
            label = String(text[text.index(after: open)..<text.index(before: text.endIndex)])
                .trimmingCharacters(in: .whitespacesAndNewlines)
            text = String(text[text.startIndex..<open])
                .trimmingCharacters(in: .whitespacesAndNewlines)
        }

        let parts = text.split(whereSeparator: { $0 == "," || $0 == ";" })
        guard parts.count == 2 || parts.count == 3 else { return nil }
        guard let latitude = degrees(String(parts[0]), positive: "N", negative: "S"),
              let longitude = degrees(String(parts[1]), positive: "E", negative: "W")
        else { return nil }
        // A third field is Google's zoom (`@lat,lng,17z`), but it must look
        // like one, or this is a list of three things and not a coordinate.
        if parts.count == 3 {
            let zoom = String(parts[2]).trimmingCharacters(in: .whitespaces).lowercased()
            guard zoom.hasSuffix("z") || zoom.hasSuffix("m") || zoom.hasSuffix("a") else {
                return nil
            }
        }
        return ParsedCoordinate(coordinate: Coordinate(latitude: latitude,
                                                       longitude: longitude),
                                label: label)
    }

    private static func coordinate(latitude: String, longitude: String) -> Coordinate? {
        guard let lat = degrees(latitude, positive: "N", negative: "S"),
              let lon = degrees(longitude, positive: "E", negative: "W") else { return nil }
        return Coordinate(latitude: lat, longitude: lon)
    }

    /// One signed degree value: a bare `-80.07213`, a `80.07213° W`, or a
    /// `W 80.07213`. Not degrees and minutes, which nothing shares and which
    /// a guessing parser would eventually misread as a decimal.
    private static func degrees(_ raw: String, positive: Character, negative: Character) -> Double? {
        var text = raw.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        text = text.replacingOccurrences(of: "°", with: "")
            .replacingOccurrences(of: "+", with: "")
            .trimmingCharacters(in: .whitespaces)
        var sign = 1.0
        for hemisphere in [positive, negative] {
            if text.hasPrefix(String(hemisphere)) {
                text.removeFirst()
            } else if text.hasSuffix(String(hemisphere)) {
                text.removeLast()
            } else {
                continue
            }
            if hemisphere == negative { sign = -1 }
            break
        }
        text = text.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty, let value = Double(text) else { return nil }
        return sign * value
    }

    /// The coordinate Google buries in a `/maps/…` path. Two pairs live
    /// there and they are different places: `@32.60,-80.07,17z` is the
    /// camera, and `!3d…!4d…` in the `data` blob is the place itself. The
    /// place is looked for first; the camera is the fallback for a link with
    /// no place in it.
    private static func coordinateInPath(_ text: String) -> Coordinate? {
        if let pair = firstMatch(#"!3d(-?\d+\.?\d*)!4d(-?\d+\.?\d*)"#, in: text),
           let lat = Double(pair.0), let lon = Double(pair.1) {
            return Coordinate(latitude: lat, longitude: lon)
        }
        if let pair = firstMatch(#"[@/](-?\d{1,2}\.\d{3,}),(-?\d{1,3}\.\d{3,})"#, in: text),
           let lat = Double(pair.0), let lon = Double(pair.1) {
            return Coordinate(latitude: lat, longitude: lon)
        }
        return nil
    }

    /// The segment after `/maps/place/`, which is the only name a Google link
    /// carries. A `/place/32.60841,-80.07213` is a dropped pin, not a name.
    private static func googlePlaceName(_ text: String) -> String {
        guard let range = text.range(of: "/maps/place/") else { return "" }
        let rest = text[range.upperBound...]
        let segment = rest.split(separator: "/", maxSplits: 1).first.map(String.init) ?? ""
        guard !segment.hasPrefix("@"), !segment.isEmpty else { return "" }
        let spaced = segment.replacingOccurrences(of: "+", with: " ")
        let decoded = spaced.removingPercentEncoding ?? spaced
        return coordinate(in: decoded) == nil ? decoded : ""
    }

    private static func nonCoordinate(_ value: String?) -> String? {
        guard let value, coordinate(in: value) == nil else { return nil }
        return value
    }

    private static func firstNonEmpty(_ candidates: [String?]) -> String {
        for candidate in candidates {
            let trimmed = (candidate ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            if !trimmed.isEmpty { return trimmed }
        }
        return ""
    }

    /// `NSRegularExpression` rather than Swift's regex literals: this file is
    /// built for iOS 17 and also run through a bare `swift` on the mac, and
    /// the old API behaves identically in both.
    private static func firstMatch(_ pattern: String, in text: String) -> (String, String)? {
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return nil }
        let range = NSRange(text.startIndex..., in: text)
        guard let match = regex.firstMatch(in: text, range: range),
              match.numberOfRanges >= 3,
              let first = Range(match.range(at: 1), in: text),
              let second = Range(match.range(at: 2), in: text) else { return nil }
        return (String(text[first]), String(text[second]))
    }
}
