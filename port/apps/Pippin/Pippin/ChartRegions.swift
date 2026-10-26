// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ChartRegions.swift — the NOAA ENC regional zips Osprey offers for download,
// and the text shown while one downloads. Foundation only, so it is tested on
// the mac (`ctest -R pippin_chart_regions`).

import Foundation

/// Whether a region is a state's zip or a Coast Guard district's.
enum ChartRegionKind: Sendable {
    case state
    case district
}

/// One of NOAA's regional ENC zips, as listed on
/// https://www.charts.noaa.gov/ENCs/ENCs.shtml.
struct ChartRegion: Identifiable, Hashable, Sendable {
    /// NOAA's file prefix: `FL` for `FL_ENCs.zip`, `07CGD` for `07CGD_ENCs.zip`.
    let id: String
    let name: String
    let kind: ChartRegionKind
    /// Zip size in MB as NOAA listed it on 2026-10-24; shown as "about".
    let megabytes: Int

    static let baseURL = URL(string: "https://www.charts.noaa.gov/ENCs/")!

    var url: URL { Self.baseURL.appendingPathComponent("\(id)_ENCs.zip") }

    /// "about 62 MB", or "under 1 MB" for the inland lake states.
    var sizeText: String { megabytes < 1 ? "under 1 MB" : "about \(megabytes) MB" }

    /// The region whose zip `id` names, or nil.
    static func named(_ id: String) -> ChartRegion? { all.first { $0.id == id } }

    static var states: [ChartRegion] { all.filter { $0.kind == .state } }
    static var districts: [ChartRegion] { all.filter { $0.kind == .district } }

    /// States and Puerto Rico, then the districts. NOAA's territory codes
    /// (AQ, BS, GT, HT, FM, MH, PO) are left out; District 14 covers them.
    static let all: [ChartRegion] = [
        ChartRegion(id: "AL", name: "Alabama", kind: .state, megabytes: 10),
        ChartRegion(id: "AK", name: "Alaska", kind: .state, megabytes: 204),
        ChartRegion(id: "CA", name: "California", kind: .state, megabytes: 52),
        ChartRegion(id: "CT", name: "Connecticut", kind: .state, megabytes: 8),
        ChartRegion(id: "DE", name: "Delaware", kind: .state, megabytes: 9),
        ChartRegion(id: "FL", name: "Florida", kind: .state, megabytes: 62),
        ChartRegion(id: "GA", name: "Georgia", kind: .state, megabytes: 15),
        ChartRegion(id: "HI", name: "Hawaii", kind: .state, megabytes: 20),
        ChartRegion(id: "ID", name: "Idaho", kind: .state, megabytes: 0),
        ChartRegion(id: "IL", name: "Illinois", kind: .state, megabytes: 2),
        ChartRegion(id: "IN", name: "Indiana", kind: .state, megabytes: 1),
        ChartRegion(id: "LA", name: "Louisiana", kind: .state, megabytes: 53),
        ChartRegion(id: "ME", name: "Maine", kind: .state, megabytes: 39),
        ChartRegion(id: "MD", name: "Maryland", kind: .state, megabytes: 31),
        ChartRegion(id: "MA", name: "Massachusetts", kind: .state, megabytes: 25),
        ChartRegion(id: "MI", name: "Michigan", kind: .state, megabytes: 32),
        ChartRegion(id: "MN", name: "Minnesota", kind: .state, megabytes: 3),
        ChartRegion(id: "MS", name: "Mississippi", kind: .state, megabytes: 8),
        ChartRegion(id: "NV", name: "Nevada", kind: .state, megabytes: 2),
        ChartRegion(id: "NH", name: "New Hampshire", kind: .state, megabytes: 4),
        ChartRegion(id: "NJ", name: "New Jersey", kind: .state, megabytes: 15),
        ChartRegion(id: "NY", name: "New York", kind: .state, megabytes: 31),
        ChartRegion(id: "NC", name: "North Carolina", kind: .state, megabytes: 22),
        ChartRegion(id: "OH", name: "Ohio", kind: .state, megabytes: 6),
        ChartRegion(id: "OR", name: "Oregon", kind: .state, megabytes: 21),
        ChartRegion(id: "PA", name: "Pennsylvania", kind: .state, megabytes: 2),
        ChartRegion(id: "PR", name: "Puerto Rico", kind: .state, megabytes: 12),
        ChartRegion(id: "RI", name: "Rhode Island", kind: .state, megabytes: 5),
        ChartRegion(id: "SC", name: "South Carolina", kind: .state, megabytes: 17),
        ChartRegion(id: "TX", name: "Texas", kind: .state, megabytes: 29),
        ChartRegion(id: "VT", name: "Vermont", kind: .state, megabytes: 0),
        ChartRegion(id: "VA", name: "Virginia", kind: .state, megabytes: 36),
        ChartRegion(id: "WA", name: "Washington", kind: .state, megabytes: 43),
        ChartRegion(id: "WI", name: "Wisconsin", kind: .state, megabytes: 9),
        ChartRegion(id: "01CGD", name: "District 1, Northeast", kind: .district, megabytes: 100),
        ChartRegion(id: "05CGD", name: "District 5, Mid-Atlantic", kind: .district, megabytes: 95),
        ChartRegion(id: "07CGD", name: "District 7, Southeast", kind: .district, megabytes: 92),
        ChartRegion(id: "08CGD", name: "District 8, Gulf and Western Rivers", kind: .district, megabytes: 97),
        ChartRegion(id: "09CGD", name: "District 9, Great Lakes", kind: .district, megabytes: 54),
        ChartRegion(id: "11CGD", name: "District 11, Pacific Southwest", kind: .district, megabytes: 54),
        ChartRegion(id: "13CGD", name: "District 13, Pacific Northwest", kind: .district, megabytes: 61),
        ChartRegion(id: "14CGD", name: "District 14, Hawaii and Pacific Islands", kind: .district, megabytes: 45),
        ChartRegion(id: "17CGD", name: "District 17, Alaska", kind: .district, megabytes: 204),
    ]
}

/// Notice text for a chart download.
enum ChartDownloadText {
    /// "Downloading Florida charts… 34%"; no percentage until the size is known.
    static func progress(_ region: ChartRegion, fraction: Double?) -> String {
        let head = "Downloading \(region.name) charts…"
        guard let fraction else { return head }
        return "\(head) \(Int(min(max(fraction, 0), 1) * 100))%"
    }

    /// "Florida charts not downloaded: <reason>." with how to resume when it can.
    static func failed(_ region: ChartRegion, reason: String, resumable: Bool) -> String {
        var text = "\(region.name) charts not downloaded: \(reason)"
        if !text.hasSuffix(".") { text += "." }
        if resumable { text += " Choose it again in Download Charts to resume." }
        return text
    }

    /// The reason for an HTTP status other than 200.
    static func httpFailure(_ status: Int) -> String {
        status == 404 ? "NOAA no longer offers this file" : "the server answered \(status)"
    }
}

/// Notice text for a chart update check.
enum ChartUpdateText {
    /// "Florida charts are up to date.", or what NOAA has changed and how to
    /// install it. `regionID` nil: the catalog shares no cell with the set.
    static func result(regionID: String?, editions: Int, updated: Int,
                       withdrawn: Int, added: Int) -> String {
        guard let regionID else { return "The installed charts are not in NOAA's catalog." }
        let name = ChartRegion.named(regionID)?.name ?? regionID
        let total = editions + updated + withdrawn + added
        guard total > 0 else { return "\(name) charts are up to date." }
        let parts = [(editions, "new edition", "new editions"),
                     (updated, "updated", "updated"),
                     (withdrawn, "withdrawn", "withdrawn"),
                     (added, "new", "new")]
            .filter { $0.0 > 0 }
            .map { "\($0.0) \($0.0 == 1 ? $0.1 : $0.2)" }
        let cells = total == 1 ? "1 \(name) chart cell" : "\(total) \(name) chart cells"
        return "NOAA has changed \(cells): \(parts.joined(separator: ", "))."
            + " Download \(name) again to install \(total == 1 ? "it" : "them")."
    }

    /// The check itself failed.
    static func failed(_ reason: String) -> String {
        var text = "Chart update check failed: \(reason)"
        if !text.hasSuffix(".") { text += "." }
        return text
    }
}
