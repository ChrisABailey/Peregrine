// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// render_gate_test.swift — the render gate, tested off the phone.
//
// `RenderGate` is CoreGraphics-only so the mac can test it. The gate is a
// pure function of a projected point, a surface and two flags, plus one bit
// of state.
//
// The state is why this file exists. The arithmetic — is a point inside a
// grown rectangle — is not what breaks; both failure modes are about the bit,
// and both are invisible in a still frame:
//
//   * a ship that leaves and never wakes the loop again, and
//   * a stale chevron pinned to the edge of the last frame drawn.
//
// Neither shows up in a screenshot, and both would be found by a rider on a
// road rather than at a desk, so they are cases here.
//
//   swift port/apps/Pippin/test/render_gate_test.swift \
//         port/apps/Pippin/Pippin/RenderGate.swift
//
// or, since it is wired into ctest:  ctest --test-dir build -R render_gate

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

/// A phone-shaped surface, in points, and a chevron the size the pack ships.
let surface = CGSize(width: 390, height: 844)
let radius: CGFloat = 14

/// One fix, browsing the map: not following, not recording.
func browsing(_ gate: inout RenderGate, _ x: CGFloat, _ y: CGFloat) -> Bool {
    gate.shouldRender(shipAt: CGPoint(x: x, y: y), inSurface: surface,
                      symbolRadius: radius, following: false, recording: false)
}

// --- the plain cases -----------------------------------------------------

do {
    var gate = RenderGate()
    check(browsing(&gate, 195, 400), "a ship in the middle of the screen draws")
    check(browsing(&gate, 0, 0), "a ship in the corner draws")
    check(browsing(&gate, 389, 843), "a ship in the far corner draws")
}

// A ship past the edge but still within a symbol of it: the chevron's nose is
// on the screen, so the frame is owed. This is the case a bare rectangle gets
// wrong, and it gets it wrong by one fix — which on a bike is a whole second
// of the symbol not being there.
do {
    var gate = RenderGate()
    check(browsing(&gate, -13, 400), "a ship 13 points off the left edge draws")
    check(browsing(&gate, 195, 857), "a ship 13 points below the bottom draws")
    check(browsing(&gate, 403, 400), "a ship 13 points off the right edge draws")
    check(browsing(&gate, 195, -14), "a ship exactly a symbol above the top draws")
}

// --- the departure: one frame, then quiet --------------------------------

do {
    var gate = RenderGate()
    check(browsing(&gate, 195, 400), "on screen")
    // Gone. The frame on screen still has the chevron in the middle of it, so
    // this one MUST draw — the ship is painted where it now is, which is off
    // the canvas, and the screen is honest again.
    check(browsing(&gate, 195, 1200), "the fix that leaves the screen still draws")
    // And now nothing, for as long as it stays away.
    check(!browsing(&gate, 195, 1300), "a ship well away costs no frame")
    check(!browsing(&gate, 195, 2000), "still nothing")
    check(!browsing(&gate, -900, 2000), "still nothing, in the other corner")
}

// --- the return: the test runs even when the render does not -------------

do {
    var gate = RenderGate()
    _ = browsing(&gate, 195, 400)
    check(browsing(&gate, 195, 1200), "leaves")
    check(!browsing(&gate, 195, 1300), "quiet")
    check(browsing(&gate, 195, 800), "the ship coming back wakes the loop")
    check(browsing(&gate, 195, 400), "and keeps drawing once it is back")
}

// A ship that leaves and returns without ever being asked would be the bug;
// this is the same run again to prove the bit is not sticky in either
// direction.
do {
    var gate = RenderGate()
    for _ in 0..<3 {
        check(browsing(&gate, 195, 400), "in")
        check(browsing(&gate, 195, 1200), "out, once")
        check(!browsing(&gate, 195, 1200), "out, quiet")
    }
}

// --- following and recording end the question ----------------------------

do {
    var gate = RenderGate()
    // Off the screen by a mile, but the map is following: the camera is about
    // to move and the frame is stale by definition.
    check(gate.shouldRender(shipAt: CGPoint(x: 195, y: 9000), inSurface: surface,
                            symbolRadius: radius, following: true, recording: false),
          "GPS mode always draws")
    check(gate.shouldRender(shipAt: CGPoint(x: 195, y: 9000), inSurface: surface,
                            symbolRadius: radius, following: false, recording: true),
          "a recording always draws")
    // Leaving GPS mode with the ship off screen: the first browsing fix draws
    // once (the mode's own frame may have had the ship in it) and then quiets.
    check(browsing(&gate, 195, 9000), "the first fix after a mode change draws")
    check(!browsing(&gate, 195, 9000), "and the second does not")
}

// --- the questions that cannot be asked ----------------------------------

do {
    var gate = RenderGate()
    _ = browsing(&gate, 195, 9000)
    _ = browsing(&gate, 195, 9000)  // quiet by now
    // No fix position, no surface yet, a projection that came back non-finite:
    // all of them render rather than guess, and all of them leave the gate
    // believing the ship is visible so the NEXT real answer is compared
    // against something safe.
    check(gate.shouldRender(shipAt: nil, inSurface: surface, symbolRadius: radius,
                            following: false, recording: false),
          "a fix with no position draws")
    // …and it costs the departure frame again on the way back down, because an
    // unanswerable question leaves the gate believing the ship is visible. One
    // extra frame per unknown is the price of never guessing.
    check(browsing(&gate, 195, 9000), "the fix after it draws once")
    check(!browsing(&gate, 195, 9000), "and then quiet again")

    check(gate.shouldRender(shipAt: CGPoint(x: 195, y: 400), inSurface: .zero,
                            symbolRadius: radius, following: false, recording: false),
          "a surface with no size draws")

    var third = RenderGate()
    _ = browsing(&third, 195, 9000)
    _ = browsing(&third, 195, 9000)
    check(third.shouldRender(shipAt: CGPoint(x: CGFloat.nan, y: 400), inSurface: surface,
                             symbolRadius: radius, following: false, recording: false),
          "a projection that came back NaN draws")
    check(third.shouldRender(shipAt: CGPoint(x: CGFloat.infinity, y: 400), inSurface: surface,
                             symbolRadius: radius, following: false, recording: false),
          "and an infinite one draws")
}

// --- a fresh gate draws --------------------------------------------------

do {
    // The first fix of a launch, with the ship nowhere near the home view:
    // it draws, because a gate that started closed would be a map that never
    // shows the ship until it happens to move into view.
    var gate = RenderGate()
    check(browsing(&gate, -5000, -5000), "the first fix of a launch always draws")
    check(!browsing(&gate, -5000, -5000), "and the second settles")
}

// --- a symbol of zero reach is still a rectangle -------------------------

do {
    var gate = RenderGate()
    check(gate.shouldRender(shipAt: CGPoint(x: 0, y: 0), inSurface: surface,
                            symbolRadius: 0, following: false, recording: false),
          "no margin: the corner is still inside")
    check(gate.shouldRender(shipAt: CGPoint(x: -0.5, y: 0), inSurface: surface,
                            symbolRadius: 0, following: false, recording: false),
          "no margin: leaving still costs one frame")
    check(!gate.shouldRender(shipAt: CGPoint(x: -0.5, y: 0), inSurface: surface,
                             symbolRadius: 0, following: false, recording: false),
          "no margin: and then quiet")
}

print("render_gate_test: \(checks) checks, \(failures) failures")
exit(failures == 0 ? 0 : 1)
