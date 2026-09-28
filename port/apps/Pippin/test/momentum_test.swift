// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// momentum_test.swift — `Momentum` and `Coast` on the mac.
///
///   ctest --test-dir build -R momentum

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

func near(_ a: CGFloat, _ b: CGFloat, _ tol: CGFloat = 1e-6) -> Bool { abs(a - b) <= tol }

/// Steps a coast at the given frame times and returns the summed movement.
func run(_ m: Momentum, times: [Double]) -> CGVector {
    var coast = Coast(m, start: 100)
    var sum = CGVector.zero
    for t in times {
        let s = coast.step(at: 100 + t)
        sum.dx += s.delta.dx
        sum.dy += s.delta.dy
    }
    return sum
}

// MARK: - The release threshold

do {
    check(Momentum(velocity: CGVector(dx: 150, dy: 100)) == nil,
          "180 pt/s is under the 200 pt/s threshold")
    check(Momentum(velocity: CGVector(dx: 160, dy: 120)) != nil,
          "exactly 200 pt/s coasts")
    check(Momentum(velocity: CGVector(dx: 50, dy: 0), minSpeed: 40) != nil,
          "the threshold is the pack's to set")
    check(Momentum(velocity: CGVector(dx: 5, dy: 0), minSpeed: 0) == nil,
          "never below the stop speed, or a coast would end before it began")
    check(Momentum(velocity: CGVector(dx: CGFloat.nan, dy: 0)) == nil, "NaN does not coast")
}

// MARK: - The curve

do {
    let m = Momentum(velocity: CGVector(dx: 1000, dy: -500))!
    check(abs(m.tau - 0.4995) < 1e-3, "0.998 per ms is a half-second time constant")
    check(near(m.limit.dx, 1000 * CGFloat(m.tau)) && near(m.limit.dy, -500 * CGFloat(m.tau)),
          "the whole coast is v0·τ")
    let far = m.offset(at: 60)
    check(near(far.dx, m.limit.dx) && near(far.dy, m.limit.dy),
          "the curve reaches v0·τ and no further")
    check(m.offset(at: 0) == .zero, "no distance at release")
    check(m.offset(at: -1) == .zero, "no distance before release")
    let half = m.offset(at: m.tau)
    check(near(half.dx, m.limit.dx * CGFloat(1 - exp(-1.0))),
          "after one τ the coast has gone 1 − 1/e of the way")
    let v = m.velocity(at: m.tau)
    check(near(v.dx, 1000 * CGFloat(exp(-1.0))), "the speed decays by e per τ")
}

do {
    let m = Momentum(velocity: CGVector(dx: 1000, dy: 0), decelerationRate: 0.99)!
    check(abs(m.tau - 0.0995) < 1e-3, "UIKit's .fast rate is a tenth of a second")
    let bad = Momentum(velocity: CGVector(dx: 1000, dy: 0), decelerationRate: 1.5)!
    check(abs(bad.tau - 0.4995) < 1e-3, "a rate outside (0, 1) is UIKit's normal")
}

// MARK: - The stop

do {
    let m = Momentum(velocity: CGVector(dx: 0, dy: 2000))!
    check(abs(m.duration - m.tau * log(200)) < 1e-9, "the coast ends at 10 pt/s")
    check(!m.isFinished(at: m.duration - 0.001), "still coasting just before")
    check(m.isFinished(at: m.duration), "finished at the stop speed")
    check(Double(hypot(m.velocity(at: m.duration).dx, m.velocity(at: m.duration).dy))
          <= Momentum.stopSpeed + 1e-9, "the speed at the end is the stop speed")
}

// MARK: - Frame-rate independence

do {
    let m = Momentum(velocity: CGVector(dx: 1500, dy: 800))!
    let end = 0.8
    let at60 = run(m, times: stride(from: 1.0 / 60, through: end, by: 1.0 / 60).map { $0 } + [end])
    let at20 = run(m, times: stride(from: 1.0 / 20, through: end, by: 1.0 / 20).map { $0 } + [end])
    var jitter: [Double] = []
    var t = 0.0
    var seed: UInt64 = 42
    while t < end {
        seed = seed &* 6364136223846793005 &+ 1442695040888963407
        t = min(end, t + 0.005 + Double(seed >> 40) / Double(1 << 24) * 0.06)
        jitter.append(t)
    }
    let jittered = run(m, times: jitter)
    let truth = m.offset(at: end)
    for (name, got) in [("60 Hz", at60), ("20 Hz", at20), ("jittered", jittered)] {
        check(near(got.dx, truth.dx, 1e-6) && near(got.dy, truth.dy, 1e-6),
              "\(name) frames reach the same place at the same time")
    }
}

do {
    var coast = Coast(Momentum(velocity: CGVector(dx: 400, dy: 0))!, start: 0)
    let first = coast.step(at: 0.1)
    let again = coast.step(at: 0.1)
    check(first.delta.dx > 0 && again.delta == .zero, "a repeated timestamp moves nothing")
    check(!first.finished, "a fresh coast is running")
    check(coast.step(at: 10).finished, "and ends")
}

// MARK: - The clamp

do {
    check(!Momentum.clampBit(requested: CGVector(dx: 12, dy: -3),
                             applied: CGVector(dx: 12.2, dy: -3.1)),
          "a projection round trip is not a clamp")
    check(Momentum.clampBit(requested: CGVector(dx: 12, dy: -3),
                            applied: CGVector(dx: 4, dy: -3)),
          "the edge ate most of the step")
    check(Momentum.clampBit(requested: CGVector(dx: 12, dy: 0), applied: .zero),
          "the edge ate all of it")
}

print("\(checks - failures)/\(checks) momentum checks passed")
exit(failures == 0 ? 0 : 1)
