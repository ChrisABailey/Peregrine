// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import CoreGraphics
import Foundation

/// The coast after a fling: an exponential decay of the release velocity, the
/// curve `UIScrollView` uses. CoreGraphics-only, so it is tested on the mac.
///
/// The speed is `v0·e^(−t/τ)` and the distance travelled the closed form
/// `v0·τ·(1 − e^(−t/τ))`, so a fling goes the same distance whatever the
/// display link's rate.
struct Momentum {
    /// UIKit's `DecelerationRate.normal`: the speed kept per millisecond.
    static let defaultDecelerationRate = 0.998
    /// Release speed below which a drag simply stops, in points per second.
    static let defaultMinSpeed = 200.0
    /// Speed at which a coast ends, in points per second.
    static let stopSpeed = 10.0

    /// The release velocity, in points per second.
    let velocity: CGVector
    /// The decay time constant in seconds.
    let tau: Double

    /// A coast for a release at `velocity`, or nil when it is slower than
    /// `minSpeed`. A `decelerationRate` outside (0, 1) falls back to UIKit's.
    init?(velocity: CGVector,
          decelerationRate: Double = Momentum.defaultDecelerationRate,
          minSpeed: Double = Momentum.defaultMinSpeed) {
        let speed = Double(hypot(velocity.dx, velocity.dy))
        let floor = minSpeed.isFinite ? max(minSpeed, Self.stopSpeed) : Self.defaultMinSpeed
        guard speed.isFinite, speed >= floor else { return nil }
        let rate = decelerationRate > 0 && decelerationRate < 1
            ? decelerationRate : Self.defaultDecelerationRate
        self.velocity = velocity
        // v(t + 1 ms) = v(t)·rate, so e^(−0.001/τ) = rate.
        tau = -0.001 / log(rate)
    }

    /// The release speed in points per second.
    var initialSpeed: Double { Double(hypot(velocity.dx, velocity.dy)) }

    /// How long the coast lasts before the speed falls to `stopSpeed`.
    var duration: Double { tau * log(initialSpeed / Self.stopSpeed) }

    /// The distance a coast would cover if it never stopped: `v0·τ`.
    var limit: CGVector {
        CGVector(dx: velocity.dx * tau, dy: velocity.dy * tau)
    }

    /// Distance travelled `t` seconds after release, in points.
    func offset(at t: Double) -> CGVector {
        let k = CGFloat(tau * (1 - exp(-max(t, 0) / tau)))
        return CGVector(dx: velocity.dx * k, dy: velocity.dy * k)
    }

    /// Velocity `t` seconds after release, in points per second.
    func velocity(at t: Double) -> CGVector {
        let k = CGFloat(exp(-max(t, 0) / tau))
        return CGVector(dx: velocity.dx * k, dy: velocity.dy * k)
    }

    /// True once the speed has fallen below `stopSpeed`.
    func isFinished(at t: Double) -> Bool { t >= duration }

    /// True when a pan asked to move the content by `requested` moved it by
    /// only `applied`: the camera's centre clamp at the pack's edge has bitten
    /// and the coast stops. Half a point absorbs the projection's round trip.
    static func clampBit(requested: CGVector, applied: CGVector) -> Bool {
        hypot(requested.dx - applied.dx, requested.dy - applied.dy) > 0.5
    }
}

/// A running coast: a `Momentum` and when it began, stepped once per display
/// frame. Each step returns the movement since the previous one, taken from
/// the closed form, so the sum of the steps is the curve at any frame rate.
struct Coast {
    let momentum: Momentum
    let start: Double
    private var travelled = CGVector.zero

    init(_ momentum: Momentum, start: Double) {
        self.momentum = momentum
        self.start = start
    }

    /// The step for a frame at time `now` (seconds, the same clock as
    /// `start`): the pan since the last step, the velocity for the band's
    /// lead, and whether the coast has ended.
    mutating func step(at now: Double)
        -> (delta: CGVector, velocity: CGVector, finished: Bool) {
        let t = now - start
        let at = momentum.offset(at: t)
        let delta = CGVector(dx: at.dx - travelled.dx, dy: at.dy - travelled.dy)
        travelled = at
        return (delta, momentum.velocity(at: t), momentum.isFinished(at: t))
    }
}
