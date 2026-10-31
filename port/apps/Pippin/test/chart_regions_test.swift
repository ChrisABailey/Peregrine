// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// chart_regions_test.swift — the NOAA region table and download notices,
// tested on the mac (`ctest -R pippin_chart_regions`).

import Foundation

var failures = 0

func expect<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    if got != want {
        failures += 1
        print("FAIL \(what): got \(got), want \(want)")
    }
}

// URLs follow NOAA's naming, checked against the live site 2026-10-25.
expect(ChartRegion.named("FL")?.url.absoluteString,
       "https://www.charts.noaa.gov/ENCs/FL_ENCs.zip", "state URL")
expect(ChartRegion.named("07CGD")?.url.absoluteString,
       "https://www.charts.noaa.gov/ENCs/07CGD_ENCs.zip", "district URL")
expect(ChartRegion.named("XX") == nil, true, "unknown id")

// Table shape: unique ids, states sorted by name, nine districts.
let ids = ChartRegion.all.map(\.id)
expect(Set(ids).count, ids.count, "ids unique")
let stateNames = ChartRegion.states.map(\.name)
expect(stateNames, stateNames.sorted(), "states sorted by name")
expect(ChartRegion.districts.count, 9, "nine Coast Guard districts")
for r in ChartRegion.districts { expect(r.id.hasSuffix("CGD"), true, "\(r.id) is a district zip") }
for r in ChartRegion.states { expect(r.id.count, 2, "\(r.id) is a two-letter code") }

// Size text.
expect(ChartRegion.named("FL")?.sizeText, "about 62 MB", "size")
expect(ChartRegion.named("VT")?.sizeText, "under 1 MB", "small size")

// Notices.
let fl = ChartRegion.named("FL")!
expect(ChartDownloadText.progress(fl, fraction: nil), "Downloading Florida charts…", "unknown size")
expect(ChartDownloadText.progress(fl, fraction: 0.345), "Downloading Florida charts… 34%", "progress")
expect(ChartDownloadText.progress(fl, fraction: 1.2), "Downloading Florida charts… 100%", "clamped")
expect(ChartDownloadText.failed(fl, reason: "The network connection was lost.", resumable: true),
       "Florida charts not downloaded: The network connection was lost. Choose it again in Download Charts to resume.",
       "resumable failure")
expect(ChartDownloadText.failed(fl, reason: ChartDownloadText.httpFailure(404), resumable: false),
       "Florida charts not downloaded: NOAA no longer offers this file.", "404")
expect(ChartDownloadText.httpFailure(503), "the server answered 503", "other status")

// Update check notices.
expect(ChartUpdateText.result(regionID: "FL", editions: 0, updated: 0, withdrawn: 0, added: 0),
       "Florida charts are up to date.", "up to date")
expect(ChartUpdateText.result(regionID: "FL", editions: 3, updated: 9, withdrawn: 1, added: 0),
       "NOAA has changed 13 Florida chart cells: 3 new editions, 9 updated, 1 withdrawn."
           + " Download Florida again to install them.", "changes")
expect(ChartUpdateText.result(regionID: "07CGD", editions: 0, updated: 0, withdrawn: 0, added: 1),
       "NOAA has changed 1 District 7, Southeast chart cell: 1 new."
           + " Download District 7, Southeast again to install it.", "one cell")
expect(ChartUpdateText.result(regionID: nil, editions: 0, updated: 0, withdrawn: 0, added: 0),
       "The installed charts are not in NOAA's catalog.", "unknown region")
expect(ChartUpdateText.failed("The Internet connection appears to be offline."),
       "Chart update check failed: The Internet connection appears to be offline.", "failed")

if failures > 0 {
    print("\(failures) failure(s)")
    exit(1)
}
print("chart regions: all checks passed")
