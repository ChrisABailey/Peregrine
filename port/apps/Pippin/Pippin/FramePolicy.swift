// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// FramePolicy.swift — the display link's frame budget while following.
///
/// CoreGraphics-only so the mac can test it (`test/frame_policy_test.swift`).
/// `MapModel` owns one, turns `rate` into a `CAFrameRateRange` when the mode
/// changes, and asks `drawsFollowFrame` before each camera-only follow frame.

import CoreGraphics

/// The frame rate for the current mode, and whether a follow frame moved the
/// picture far enough to be worth drawing.
struct FramePolicy {
    /// What the display link is asked for.
    enum Rate: Equatable {
        /// The system's default range (60 Hz on a phone without ProMotion
        /// opted in). Gestures and every non-following frame use it.
        case system
        /// A fixed rate in frames per second.
        case fixed(Float)
    }

    static let defaultFollowFPS = 20.0
    static let followFPSRange = 15.0...30.0
    static let defaultMinMovePoints = 0.25

    /// Frames per second while following with no gesture, clamped to
    /// `followFPSRange`.
    let followFPS: Float
    /// Smallest camera movement, in points, that earns a follow frame.
    /// Zero draws every frame.
    let minMovePoints: CGFloat

    /// Builds the policy from `[display] follow_fps` and `min_move_pt`. A
    /// non-finite or non-positive rate falls back to the default; a negative
    /// or non-finite threshold becomes zero.
    init(followFPS: Double = defaultFollowFPS,
         minMovePoints: Double = defaultMinMovePoints) {
        let fps = followFPS.isFinite && followFPS > 0 ? followFPS : Self.defaultFollowFPS
        self.followFPS = Float(min(max(fps, Self.followFPSRange.lowerBound),
                                   Self.followFPSRange.upperBound))
        self.minMovePoints = minMovePoints.isFinite ? CGFloat(max(minMovePoints, 0)) : 0
    }

    /// The rate for a mode. A gesture wins over following, since a finger
    /// dragging the chart wants the full rate whatever the camera is doing.
    func rate(gesture: Bool, following: Bool) -> Rate {
        if gesture || !following { return .system }
        return .fixed(followFPS)
    }

    /// How far the picture moves between two cameras at the same scale, in
    /// points: the centre's shift plus the arc the rotation sweeps at the
    /// surface's corner, which is where a turn moves pixels furthest.
    ///
    /// - Parameters:
    ///   - centerShift: the old centre's position minus the new centre's, both
    ///     projected through the new camera.
    ///   - rotationDelta: degrees between the two cameras, either sign, any
    ///     number of turns.
    static func movement(centerShift: CGVector, rotationDelta: Double,
                         surface: CGSize) -> CGFloat {
        var turn = rotationDelta.truncatingRemainder(dividingBy: 360)
        if turn > 180 { turn -= 360 } else if turn < -180 { turn += 360 }
        let halfDiagonal = (surface.width * surface.width
                            + surface.height * surface.height).squareRoot() / 2
        let arc = CGFloat(abs(turn) * .pi / 180) * halfDiagonal
        let shift = (centerShift.dx * centerShift.dx
                     + centerShift.dy * centerShift.dy).squareRoot()
        return shift + arc
    }

    /// Whether a camera-only follow frame that moves the picture `movement`
    /// points since the last drawn frame is drawn. A non-finite movement is
    /// drawn: a projection that could not be measured is not evidence of rest.
    func drawsFollowFrame(movement: CGFloat) -> Bool {
        guard movement.isFinite else { return true }
        return movement >= minMovePoints
    }
}
