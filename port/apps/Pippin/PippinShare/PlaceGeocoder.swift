// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PlaceGeocoder.swift — a shared name turned into an approximate position.
//
// Extension-only. A Google Maps share of a named place can arrive with a name
// and an address and no coordinate (`PlaceLink.ResolveOutcome.noCoordinate`
// with a query). This asks Apple's search for it. Whether a result is
// accepted is `PlaceLink.searchResultMatches`, which is tested on the mac.

import Foundation
import MapKit

enum PlaceGeocoder {
    /// A place for `query`, marked approximate, or nil when Apple's search
    /// finds nothing whose name or address shares a word with it.
    ///
    /// A point-of-interest search runs first, so "Resort, 1 Street" lands on
    /// the resort rather than a street centreline; the address after the
    /// first comma is the fallback.
    static func place(for query: String) async -> SharedPlace? {
        guard PlaceLink.allowsNetworkLookup else { return nil }
        if let hit = await search(query, types: .pointOfInterest, for: query) {
            return hit
        }
        if let address = PlaceLink.addressPart(of: query),
           let hit = await search(address, types: .address, for: query) {
            return hit
        }
        PippinLog.share.error("geocoder: nothing matched \(query, privacy: .public)")
        return nil
    }

    /// The first result of one search that matches `query`.
    private static func search(_ text: String,
                               types: MKLocalSearch.ResultType,
                               for query: String) async -> SharedPlace? {
        let request = MKLocalSearch.Request()
        request.naturalLanguageQuery = text
        request.resultTypes = types
        let items: [MKMapItem]
        do {
            items = try await MKLocalSearch(request: request).start().mapItems
        } catch {
            PippinLog.share.notice(
                "geocoder: search \(text, privacy: .public) failed: \(String(describing: error), privacy: .public)")
            return nil
        }
        for item in items {
            let (coordinate, address) = location(of: item)
            // An address result's own name is its street line; the place is
            // still the one the query named.
            let name = types == .address ? PlaceLink.namePart(of: query) : (item.name ?? "")
            guard PlaceLink.searchResultMatches(query: query, name: name, address: address),
                  var place = SharedPlace(latitude: coordinate.latitude,
                                          longitude: coordinate.longitude,
                                          name: name, address: address) else { continue }
            place.searchQuery = query
            PippinLog.share.notice(
                "geocoder: \(text, privacy: .public) -> \(name, privacy: .public) \(coordinate.latitude, privacy: .public),\(coordinate.longitude, privacy: .public)")
            return place
        }
        PippinLog.share.notice(
            "geocoder: \(items.count, privacy: .public) results for \(text, privacy: .public), none matching")
        return nil
    }

    /// The item's coordinate and one-line postal address. iOS 26 moves both
    /// off `placemark`.
    private static func location(of item: MKMapItem) -> (CLLocationCoordinate2D, String) {
        if #available(iOS 26, *) {
            return (item.location.coordinate, oneLine(item.address?.fullAddress))
        }
        return (item.placemark.coordinate, oneLine(item.placemark.title))
    }

    /// A multi-line postal address joined with commas, for the remarks.
    private static func oneLine(_ address: String?) -> String {
        (address ?? "").split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
            .joined(separator: ", ")
    }
}
