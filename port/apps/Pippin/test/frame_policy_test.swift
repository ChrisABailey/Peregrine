// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// frame_policy_test.swift — `FramePolicy` on the mac.
///
///   ctest --test-dir build -R frame_policy

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

func near(_ a: CGFloat, _ b: CGFloat, _ tol: CGFloat = 1e-9) -> Bool { abs(a - b) <= tol }

// MARK: - Settings

do {
    let p = FramePolicy()
    check(p.followFPS == 20, "default follow rate is 20")
    check(p.minMovePoints == 0.25, "default threshold is a quarter point")
}
do {
    check(FramePolicy(followFPS: 60).followFPS == 30, "rate clamped to 30")
    check(FramePolicy(followFPS: 5).followFPS == 15, "rate clamped to 15")
    check(FramePolicy(followFPS: 24).followFPS == 24, "in-range rate kept")
    check(FramePolicy(followFPS: 0).followFPS == 20, "zero falls back to the default")
    check(FramePolicy(followFPS: -3).followFPS == 20, "negative falls back to the default")
    check(FramePolicy(followFPS: .nan).followFPS == 20, "NaN falls back to the default")
    check(FramePolicy(minMovePoints: -1).minMovePoints == 0, "negative threshold is zero")
    check(FramePolicy(minMovePoints: .infinity).minMovePoints == 0,
          "infinite threshold is zero, not a frozen map")
}

// MARK: - Rate by mode

do {
    let p = FramePolicy(followFPS: 20)
    check(p.rate(gesture: false, following: false) == .system, "idle: system rate")
    check(p.rate(gesture: true, following: false) == .system, "gesture: system rate")
    check(p.rate(gesture: false, following: true) == .fixed(20), "following: 20 Hz")
    check(p.rate(gesture: true, following: true) == .system,
          "a gesture while following gets the full rate")
}

// MARK: - Movement

do {
    let phone = CGSize(width: 393, height: 852)
    check(near(FramePolicy.movement(centerShift: CGVector(dx: 3, dy: 4),
                                    rotationDelta: 0, surface: phone), 5),
          "pure shift is its length")
    let halfDiag = (393.0 * 393.0 + 852.0 * 852.0).squareRoot() / 2
    let oneDeg = CGFloat(halfDiag * .pi / 180)
    check(near(FramePolicy.movement(centerShift: .zero, rotationDelta: 1, surface: phone),
               oneDeg), "one degree sweeps the corner's arc")
    check(near(FramePolicy.movement(centerShift: .zero, rotationDelta: -1, surface: phone),
               oneDeg), "sign does not matter")
    check(near(FramePolicy.movement(centerShift: .zero, rotationDelta: 359, surface: phone),
               oneDeg, 1e-6), "359 degrees is one the short way")
    check(near(FramePolicy.movement(centerShift: .zero, rotationDelta: -721, surface: phone),
               oneDeg, 1e-6), "whole turns are ignored")
    check(near(FramePolicy.movement(centerShift: CGVector(dx: 0.1, dy: 0),
                                    rotationDelta: 1, surface: phone), 0.1 + oneDeg),
          "shift and turn add")
}

// MARK: - Drawing a follow frame

do {
    let p = FramePolicy(minMovePoints: 0.25)
    check(!p.drawsFollowFrame(movement: 0), "a camera at rest is not drawn")
    check(!p.drawsFollowFrame(movement: 0.2), "under a quarter point is not drawn")
    check(p.drawsFollowFrame(movement: 0.25), "the threshold itself is drawn")
    check(p.drawsFollowFrame(movement: 3), "a real move is drawn")
    check(p.drawsFollowFrame(movement: .nan), "an unmeasurable move is drawn")
    check(p.drawsFollowFrame(movement: .infinity), "an infinite move is drawn")

    // A rider stopped at a junction: GPS jitter moves the target a few
    // centimetres, a hundredth of a point at riding scale, and a 0.01-degree
    // wobble in the resolved heading.
    let phone = CGSize(width: 393, height: 852)
    let jitter = FramePolicy.movement(centerShift: CGVector(dx: 0.01, dy: 0.01),
                                      rotationDelta: 0.01, surface: phone)
    check(!p.drawsFollowFrame(movement: jitter), "junction jitter is not drawn")
}
do {
    let p = FramePolicy(minMovePoints: 0)
    check(p.drawsFollowFrame(movement: 0), "threshold zero draws every frame")
}

print("frame_policy_test: \(checks) checks, \(failures) failures")
exit(failures == 0 ? 0 : 1)
