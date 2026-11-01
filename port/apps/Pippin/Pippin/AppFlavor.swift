// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// AppFlavor.swift — which app the shared Swift sources are built into.
//
// Pippin (cycling, OSM map) and Osprey (boating, S-57 charts) compile the same
// files. The app's Info.plist names its flavor in `PPAppFlavor`; everything
// that differs between the two apps asks this type rather than testing the
// bundle id. Foundation-only, so it is tested on the mac
// (`ctest -R pippin_app_flavor`).

import Foundation

enum AppFlavor: String {
    case pippin
    case osprey

    /// The flavor named by the running bundle; Pippin when the key is absent.
    static let current: AppFlavor = from(
        Bundle.main.object(forInfoDictionaryKey: "PPAppFlavor") as? String)

    /// Parses an Info.plist value; anything unrecognised is Pippin.
    static func from(_ value: String?) -> AppFlavor {
        let key = value?.trimmingCharacters(in: .whitespaces).lowercased() ?? ""
        return AppFlavor(rawValue: key) ?? .pippin
    }

    /// The units offered in the map menu, in menu order.
    var offeredUnits: [DistanceUnits] {
        switch self {
        case .pippin: return [.kilometres, .miles]
        case .osprey: return [.nauticalMiles, .miles, .kilometres]
        }
    }

    /// The units used until the user picks others.
    var defaultUnits: DistanceUnits {
        switch self {
        case .pippin: return .miles
        case .osprey: return .nauticalMiles
        }
    }

    /// Beach routing and the tide card's beach verdict.
    var showsBeach: Bool { self == .pippin }

    /// Avoid hills and slope colours on the route sheet.
    var showsHills: Bool { self == .pippin }

    /// A route stop is placed by tapping the map rather than by centring a
    /// crosshair on it.
    var placesStopsByTap: Bool { self == .osprey }

    /// New stops go on the end of the route, and a new route opens on the
    /// stops form rather than on a destination search.
    var appendsStops: Bool { self == .osprey }

    /// A course-over-ground vector ahead of the ship and COG in the trip bar.
    var showsCourseOverGround: Bool { self == .osprey }

    /// The trip readout as one card of labelled cells, styled like the course
    /// deviation card and stacked above it, instead of two large clusters.
    var compactTripCard: Bool { self == .osprey }

    /// The compass button drawn as a compass rose rather than a north arrow.
    var drawsCompassRose: Bool { self == .osprey }

    /// Live AIS traffic from Open Waters, offered in the map menu.
    var offersAisTraffic: Bool { self == .osprey }

    /// Route markers and AIS targets grow with the Symbol Size setting.
    var overlaysFollowSymbolSize: Bool { self == .osprey }

    /// Course-up following tilts the chart (`[display] follow_pitch_deg`).
    /// Osprey stays flat: at chart scales a tilted band covers hundreds of
    /// kilometres ahead and costs a full ENC draw for little to read.
    var tiltsWhileFollowing: Bool { self == .pippin }

    /// Osprey shows a "not for navigation" notice until it is acknowledged.
    var needsNavigationNotice: Bool { self == .osprey }

    /// The noun for a recorded GPS track, lower case singular ("ride").
    var trackNoun: String {
        switch self {
        case .pippin: return "ride"
        case .osprey: return "track"
        }
    }

    /// `trackNoun` with its first letter capitalised ("Ride").
    var trackTitle: String { trackNoun.prefix(1).uppercased() + trackNoun.dropFirst() }

    /// The label prefix for a route stop between Start and End ("Via 1",
    /// "WP 1"). The full label is also the stop's identity in the document.
    var intermediateStopPrefix: String {
        switch self {
        case .pippin: return "Via"
        case .osprey: return "WP"
        }
    }

    /// The note under "Nothing found." saying what a search covers.
    var searchScopeNote: String {
        switch self {
        case .pippin:
            return "Your pins and roads are searched everywhere. Other places on the map are found only where you can see them."
        case .osprey:
            return "Your pins and every named feature on the charts are searched, wherever they are."
        }
    }

    /// The placeholder in the problem report's description field.
    var reportPrompt: String {
        switch self {
        case .pippin: return "A path that is not there, a gate that is, a name that is wrong\u{2026}"
        case .osprey: return "Data displayed wrong, App crash, or Suggested Addition"
        }
    }

    /// What the planned route becomes in a GPX, in the export row.
    var routeGpxSubtitle: String {
        switch self {
        case .pippin: return "The planned line, along the roads"
        case .osprey: return "The waypoints, as a route"
        }
    }

    /// The export sheet's note on where a GPX can go. `appName` is the
    /// display name, passed in so this file stays free of the bundle.
    func gpxDevicesNote(appName: String) -> String {
        switch self {
        case .pippin:
            return "Exports the route as a GPX for use on many GPS watches and bike "
                + "computers. To send this route to another \(appName) user, use "
                + "Share Route in the route tool."
        case .osprey:
            return "Exports the route as a GPX for use on most chartplotters and marine "
                + "navigation apps. To send this route to another \(appName) user, use "
                + "Share Route in the route tool."
        }
    }

    /// The SF Symbol beside the export sheet's GPX note.
    var gpxDevicesSymbol: String {
        switch self {
        case .pippin: return "applewatch"
        case .osprey: return "ferry"
        }
    }
}

/// The first-launch notice Osprey shows before the chart.
enum NavigationNotice {
    static let title = "Not for Navigation"
    static let body = "Charts are NOAA electronic navigational charts drawn by "
        + "this app, which is not a certified ECDIS or chart plotter. Positions, "
        + "depths and routes may be wrong or out of date. Do not rely on this "
        + "app to navigate; use official charts and your own judgement."
    static let acknowledge = "I Understand"

    /// The UserDefaults key recording that the notice was acknowledged.
    static let acknowledgedKey = "PPNavigationNoticeAcknowledged"

    /// Whether `flavor` must show the notice, given what has been stored.
    static func isNeeded(flavor: AppFlavor, defaults: UserDefaults = .standard) -> Bool {
        flavor.needsNavigationNotice && !defaults.bool(forKey: acknowledgedKey)
    }
}
