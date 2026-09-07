// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// location_policy_test.swift — the accuracy policy, tested off the phone.
//
// `LocationPolicy` is CoreGraphics-only so the mac can test it, like
// `RenderGate`, and here the hazard is the state machine itself:
//
//   * Flapping. A ship parked near the viewport edge — a rider stopped at a
//     junction with the map half moved away — must not restart the receiver
//     on every fix, because each restart has a warm-up cost that can exceed
//     leaving it alone. The two thresholds are the whole design, and an
//     oscillation between them is invisible in a screenshot.
//   * A receiver left coarse. Every "cannot answer" case must resolve to full
//     accuracy, or the untested case is a rider whose position stops
//     working.
//
//   swift port/apps/Pippin/test/location_policy_test.swift \
//         port/apps/Pippin/Pippin/LocationPolicy.swift
//
// or, since it is wired into ctest:  ctest --test-dir build -R location_policy

import CoreGraphics
import Foundation

var failures = 0
var checks = 0

func check(_ condition: Bool, _ what: String, line: UInt = #line) {
    checks += 1
    if !condition {
        failures += 1
        print("FAIL (line \(line)): \(what)")
    }
}

/// A phone-shaped surface, in points. The thresholds are fractions of it:
/// the return band ends 97.5 points off the left and right edges and 211 off
/// the top and bottom; the leave band ends 390 and 844 out.
let surface = CGSize(width: 390, height: 844)

/// One fix, browsing the map: not following, not recording.
@discardableResult
func browsing(_ p: inout LocationPolicy, _ x: CGFloat, _ y: CGFloat)
    -> LocationAccuracy {
    p.accuracy(shipAt: CGPoint(x: x, y: y), inSurface: surface,
               following: false, recording: false)
}

// --- the two modes that end the question ---------------------------------

do {
    var p = LocationPolicy()
    check(p.current == .navigation, "a fresh policy starts in navigation")
    // Following: the camera is on the ship, so where it is on screen is not
    // the question — and while following, it is on screen by construction.
    check(p.accuracy(shipAt: CGPoint(x: -9999, y: -9999), inSurface: surface,
                     following: true, recording: false) == .navigation,
          "following is full accuracy wherever the projection says the ship is")
    check(p.accuracy(shipAt: CGPoint(x: -9999, y: -9999), inSurface: surface,
                     following: false, recording: true) == .navigation,
          "recording is full accuracy wherever the ship is")
    // And they RESET the bit, not merely answer over the top of it: a ride
    // that ends with the ship off screen must still cross the leave threshold
    // again before anything drops the receiver.
    check(p.current == .navigation, "the mode is left in navigation, not shadowed")
}

// --- the cases that cannot be answered -----------------------------------

do {
    var p = LocationPolicy()
    // Get it into the coarse mode first, so "unknown resolves to navigation"
    // is a change of state and not a starting value being read back.
    check(browsing(&p, -2000, 400) == .coarse, "a ship a long way left goes coarse")
    check(p.accuracy(shipAt: nil, inSurface: surface, following: false,
                     recording: false) == .navigation,
          "no fix position at all goes back to full accuracy")

    check(browsing(&p, -2000, 400) == .coarse, "coarse again")
    check(p.accuracy(shipAt: CGPoint(x: 100, y: 100), inSurface: .zero,
                     following: false, recording: false) == .navigation,
          "no surface yet goes back to full accuracy")

    check(browsing(&p, -2000, 400) == .coarse, "coarse again")
    check(p.accuracy(shipAt: CGPoint(x: CGFloat.nan, y: 100), inSurface: surface,
                     following: false, recording: false) == .navigation,
          "a non-finite projection goes back to full accuracy")
    check(browsing(&p, -2000, 400) == .coarse, "coarse again")
    check(p.accuracy(shipAt: CGPoint(x: 100, y: CGFloat.infinity), inSurface: surface,
                     following: false, recording: false) == .navigation,
          "an infinite projection goes back to full accuracy")
}

// --- the band between the thresholds: where nothing happens ---------------

do {
    var p = LocationPolicy()
    check(browsing(&p, 195, 400) == .navigation, "a ship on screen stays sharp")
    // Just off the edge. Past the RETURN threshold in one case and not the
    // other, but nowhere near the LEAVE threshold, so neither matters: the
    // policy is in navigation and stays there.
    check(browsing(&p, -1, 400) == .navigation, "one point off the left edge")
    check(browsing(&p, -98, 400) == .navigation, "past the return band, still sharp")
    check(browsing(&p, -389, 400) == .navigation,
          "one point inside the leave band, still sharp")
    check(browsing(&p, 195, -843) == .navigation,
          "a point inside the leave band above the screen, still sharp")
}

// THE FLAP, and it is the case this file exists for. A ship sitting near the
// edge is walked back and forth across the RETURN threshold twenty times. A
// single-threshold policy restarts the receiver on every one of these.
do {
    var p = LocationPolicy()
    check(browsing(&p, 195, 400) == .navigation, "starts on screen")
    var changes = 0
    var previous = p.current
    for i in 0..<20 {
        // -50 is inside the return band, -150 is outside it — and both are a
        // long way inside the leave band.
        _ = browsing(&p, i % 2 == 0 ? -50 : -150, 400)
        if p.current != previous { changes += 1 }
        previous = p.current
    }
    check(changes == 0, "a ship oscillating across the return threshold never flaps")
    check(p.current == .navigation, "and it is still sharp at the end of it")
}

// --- leaving --------------------------------------------------------------

do {
    var p = LocationPolicy()
    check(browsing(&p, 195, 400) == .navigation, "on screen")
    // Exactly ON the leave threshold is still inside it: the comparison is
    // inclusive, which is the same convention `RenderGate` uses for the
    // symbol's reach and is what keeps a boundary case from depending on a
    // rounding.
    check(browsing(&p, -390, 400) == .navigation, "exactly on the leave threshold")
    check(browsing(&p, -391, 400) == .coarse, "one point past it drops to coarse")
}

do {
    var p = LocationPolicy()
    check(browsing(&p, 195, 844 + 845) == .coarse, "a whole screen below drops")
}
do {
    var p = LocationPolicy()
    check(browsing(&p, 390 + 391, 400) == .coarse, "a whole screen right drops")
}
do {
    var p = LocationPolicy()
    check(browsing(&p, 195, -845) == .coarse, "a whole screen above drops")
}

// --- coming back ----------------------------------------------------------

do {
    var p = LocationPolicy()
    check(browsing(&p, -2000, 400) == .coarse, "far away, coarse")
    // Back inside the LEAVE band is not enough — that is the point of the two
    // thresholds. It has to reach the return band.
    check(browsing(&p, -300, 400) == .coarse, "inside the leave band is not a return")
    check(browsing(&p, -98, 400) == .coarse, "one point outside the return band")
    check(browsing(&p, -97, 400) == .navigation, "inside the return band comes back")
    check(browsing(&p, 195, 400) == .navigation, "and on screen stays back")
}

// A ship that never comes back would be the same class of bug as P16's, so the
// round trip is walked twice: coarse, sharp, coarse again.
do {
    var p = LocationPolicy()
    check(browsing(&p, 195, 400) == .navigation, "on screen")
    check(browsing(&p, -2000, 400) == .coarse, "away")
    check(browsing(&p, 195, 400) == .navigation, "back")
    check(browsing(&p, -2000, 400) == .coarse, "away again")
    check(browsing(&p, 195, 400) == .navigation, "and back again")
}

// --- the up-front demand --------------------------------------------------

do {
    var p = LocationPolicy()
    check(browsing(&p, -2000, 400) == .coarse, "idling coarse")
    // The GPS button, or Record. The receiver is asked for its best BEFORE the
    // mode is entered — a warm start is seconds and a cold one thirty — and
    // the bit moves with it so the policy agrees with the hardware rather than
    // sitting in coarse believing something the receiver is no longer doing.
    p.demandNavigation()
    check(p.current == .navigation, "the demand moves the bit, not just the hardware")
    // What holds it there is the mode, not the demand. A tenth of a second
    // later `gpsMode` or `isRecording` is true, and from then on every fix
    // answers full accuracy wherever the ship is; the demand only covers the
    // gap between the press and the flag.
    check(p.accuracy(shipAt: CGPoint(x: -2000, y: 400), inSurface: surface,
                     following: true, recording: false) == .navigation,
          "and the mode that follows holds it, however far off screen the ship is")
}

// The refused press, the other side of the same coin: `setGpsMode` demands
// full accuracy first and can still bail on the authorization check, so the
// mode never arrives. Nothing then holds the receiver sharp, and nothing
// should, because nobody is following. The next fix puts it back, which is
// the demand being undone correctly.
do {
    var p = LocationPolicy()
    check(browsing(&p, -2000, 400) == .coarse, "idling coarse")
    p.demandNavigation()
    check(browsing(&p, -2000, 400) == .coarse,
          "a demand with no mode behind it lasts exactly one fix")
}

// --- the thresholds are fractions, so they mean the same thing at any zoom --

do {
    // A tiny surface — a phone in a corner of a split view, or a test rig.
    let small = CGSize(width: 100, height: 100)
    var p = LocationPolicy()
    check(p.accuracy(shipAt: CGPoint(x: -101, y: 50), inSurface: small,
                     following: false, recording: false) == .coarse,
          "the leave threshold scales down with the surface")
    check(p.accuracy(shipAt: CGPoint(x: -26, y: 50), inSurface: small,
                     following: false, recording: false) == .coarse,
          "just outside the return band on a small surface")
    check(p.accuracy(shipAt: CGPoint(x: -25, y: 50), inSurface: small,
                     following: false, recording: false) == .navigation,
          "the return threshold scales down too")
}

print("location_policy: \(checks - failures)/\(checks) checks passed")
if failures > 0 {
    print("location_policy: \(failures) FAILED")
    exit(1)
}
