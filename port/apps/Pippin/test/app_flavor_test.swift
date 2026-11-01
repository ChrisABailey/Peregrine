// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// app_flavor_test.swift — what each app flavor shows, tested on the mac
// (`ctest -R pippin_app_flavor`).

import Foundation

var failures = 0

func expect<T: Equatable>(_ got: T, _ want: T, _ what: String) {
    if got != want {
        failures += 1
        print("FAIL \(what): got \(got), want \(want)")
    }
}

// The Info.plist value parses leniently; anything else is Pippin.
expect(AppFlavor.from("osprey"), .osprey, "osprey")
expect(AppFlavor.from(" Osprey "), .osprey, "case and spaces ignored")
expect(AppFlavor.from(nil), .pippin, "absent key is Pippin")
expect(AppFlavor.from("$(PP_APP_FLAVOR)"), .pippin, "unexpanded setting is Pippin")
expect(AppFlavor.from(""), .pippin, "empty is Pippin")

// Pippin keeps the cycling features and its two land units.
let pippin = AppFlavor.pippin
expect(pippin.offeredUnits, [.kilometres, .miles], "Pippin units")
expect(pippin.defaultUnits, .miles, "Pippin default")
expect(pippin.showsBeach, true, "Pippin beach")
expect(pippin.showsHills, true, "Pippin hills")
expect(pippin.needsNavigationNotice, false, "Pippin notice")
expect(pippin.showsCourseOverGround, false, "Pippin COG")
expect(pippin.tiltsWhileFollowing, true, "Pippin tilts")
expect(pippin.trackNoun, "ride", "Pippin noun")
expect(pippin.trackTitle, "Ride", "Pippin title")

// Osprey defaults to nautical miles and drops the cycling features.
let osprey = AppFlavor.osprey
expect(osprey.offeredUnits.first, .nauticalMiles, "Osprey lists nm first")
expect(osprey.offeredUnits.contains(osprey.defaultUnits), true, "default is offered")
expect(osprey.defaultUnits, .nauticalMiles, "Osprey default")
expect(osprey.showsBeach, false, "Osprey beach")
expect(osprey.showsHills, false, "Osprey hills")
expect(osprey.showsCourseOverGround, true, "Osprey COG")
expect(osprey.tiltsWhileFollowing, false, "Osprey stays flat")
expect(osprey.trackNoun, "track", "Osprey noun")
expect(osprey.trackTitle, "Track", "Osprey title")
expect(osprey.routeGpxSubtitle, "The waypoints, as a route", "Osprey route export")
expect(pippin.routeGpxSubtitle, "The planned line, along the roads", "Pippin route export")
expect(osprey.gpxDevicesSymbol, "ferry", "Osprey GPX note symbol")
expect(pippin.gpxDevicesSymbol, "applewatch", "Pippin GPX note symbol")
expect(osprey.intermediateStopPrefix, "WP", "Osprey waypoint label")
expect(pippin.intermediateStopPrefix, "Via", "Pippin via label")
expect(osprey.searchScopeNote.contains("roads"), false, "Osprey search note names no roads")
expect(pippin.searchScopeNote.contains("roads"), true, "Pippin search note keeps roads")

// The notice shows until acknowledged, and only in Osprey.
let suite = "app_flavor_test.\(ProcessInfo.processInfo.processIdentifier)"
let defaults = UserDefaults(suiteName: suite)!
defaults.removePersistentDomain(forName: suite)
expect(NavigationNotice.isNeeded(flavor: .osprey, defaults: defaults), true, "first launch")
expect(NavigationNotice.isNeeded(flavor: .pippin, defaults: defaults), false, "never in Pippin")
defaults.set(true, forKey: NavigationNotice.acknowledgedKey)
expect(NavigationNotice.isNeeded(flavor: .osprey, defaults: defaults), false, "acknowledged")
defaults.removePersistentDomain(forName: suite)

if failures > 0 {
    FileHandle.standardError.write("\(failures) failure(s)\n".data(using: .utf8)!)
    exit(1)
}
print("app_flavor_test: ok")
