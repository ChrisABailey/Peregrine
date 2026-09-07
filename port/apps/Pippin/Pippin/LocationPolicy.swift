// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// LocationPolicy.swift — how hard the receiver should be working.
//
// The feed is started once from `MapScreen.onAppear` and `stopFeed()` has no
// call sites, so without this policy `CLLocationManager` would run at
// `kCLLocationAccuracyBestForNavigation` with `distanceFilter =
// kCLDistanceFilterNone` and `pausesLocationUpdatesAutomatically = NO` — the
// most expensive configuration CoreLocation offers — for as long as the app
// is in the foreground, whether or not anybody is following or recording.
//
// Lowering `desiredAccuracy` a notch is not the lever: it is a hint, and the
// dominant cost is the GNSS chip being powered at all, which every tier from
// `Best` down through `HundredMeters` keeps. The step change is at
// `Kilometer`/`ThreeKilometers`, where iOS can answer from cell and wifi. So
// the idle state is `PPLocationAccuracyModeCoarse`.
//
// A full stop would almost be safe, because in this state the ship is off
// screen through the user panning away rather than travelling away, and a
// camera move is caught by `MapModel`'s `viewport` hook off the last fix with
// no new fix needed. Coarse rather than stopped covers the rarer case of
// physically travelling back into view, which three kilometres is about the
// right resolution to notice.
//
// This file is the decision and nothing else, for `RenderGate`'s reason: a
// hysteresis rule is a state machine, and one wired into CoreLocation could
// only be tested by riding a bicycle. It is CoreGraphics-only so
// `ctest -R location_policy` can run it.

import CoreGraphics

/// What the receiver should be doing.
enum LocationAccuracy {
    /// `BestForNavigation`, every fix. Somebody is watching the ship.
    case navigation
    /// `ThreeKilometers` and a large distance filter. Nobody is.
    case coarse
}

/// Decides which accuracy the receiver should be in, given what the map is
/// doing. Owned by `MapModel` and touched only from the main thread. A struct
/// so the caller's `mutating` call site is where the state visibly changes.
struct LocationPolicy {
    /// Two thresholds, not one. A ship parked near the viewport edge would
    /// cross a single threshold on every fix, and each crossing restarts the
    /// receiver at a warm-up cost that can exceed what leaving it alone would
    /// have spent. Full accuracy is restored as soon as the ship is near the
    /// screen and dropped only once it is far past it; between them nothing
    /// happens.
    ///
    /// Both are fractions of the viewport's own size applied to each edge, so
    /// they mean the same thing at every zoom.
    static let returnFraction: CGFloat = 0.25
    static let leaveFraction: CGFloat = 1.0

    /// The current mode. Starts at `.navigation`, which matches
    /// `PPLocationSource`'s own initial state and is the safe way round: a
    /// launch is the one moment somebody is certainly looking for the ship.
    private var mode: LocationAccuracy = .navigation

    var current: LocationAccuracy { mode }

    /// Re-takes the decision for an already-projected ship position.
    /// `PPViewport.point(forGeo:)` is the one projection this app trusts, and
    /// keeping it on the model's side is what lets this file compile against
    /// nothing but CoreGraphics.
    ///
    /// A nil `screenPoint` means the question could not be asked: no fix, no
    /// surface, or a non-finite projection. All of those answer `.navigation`,
    /// because a guess here would cost a rider their position.
    @discardableResult
    mutating func accuracy(shipAt screenPoint: CGPoint?,
                           inSurface surface: CGSize,
                           following: Bool,
                           recording: Bool) -> LocationAccuracy {
        // Following and recording are separate jobs, and either one ends the
        // question: following needs the best fix because the camera is on it,
        // recording because a track logged at three kilometres is not a track.
        if following || recording {
            mode = .navigation
            return mode
        }
        guard let screenPoint,
              screenPoint.x.isFinite, screenPoint.y.isFinite,
              surface.width > 0, surface.height > 0 else {
            mode = .navigation
            return mode
        }
        switch mode {
        case .navigation:
            if !Self.isInside(screenPoint, surface, Self.leaveFraction) {
                mode = .coarse
            }
        case .coarse:
            if Self.isInside(screenPoint, surface, Self.returnFraction) {
                mode = .navigation
            }
        }
        return mode
    }

    /// Forces navigation accuracy. A GNSS warm start is seconds and a cold
    /// one can be thirty, so entering GPS mode or starting a recording after
    /// a coarse spell must restore accuracy before the mode rather than on
    /// the first fix after it. `setGpsMode(true)` and `startRecording()` are
    /// the two callers.
    ///
    /// It also resets the state, so a ship still far off screen has to cross
    /// the leave threshold again before anything drops it back.
    mutating func demandNavigation() {
        mode = .navigation
    }

    /// Whether `p` is inside the surface grown by `fraction` of its own size
    /// on each edge. No symbol radius here, unlike `RenderGate`: that gate
    /// asks whether a chevron's nose lands on screen, and this asks whether
    /// anybody could plausibly care.
    private static func isInside(_ p: CGPoint, _ surface: CGSize,
                                 _ fraction: CGFloat) -> Bool {
        let dx = surface.width * fraction
        let dy = surface.height * fraction
        return p.x >= -dx && p.y >= -dy
            && p.x <= surface.width + dx && p.y <= surface.height + dy
    }
}
