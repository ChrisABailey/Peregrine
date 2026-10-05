// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// scene_policy_test.swift — the scene-phase drawing gate, tested off the phone.
//
// The phase sequences are the ones iOS delivers. The hazards are a resume
// owed twice (background → inactive → active must resume once, at inactive)
// and a suspend missed or repeated.
//
//   ctest --test-dir build -R scene_policy

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

/// Feeds `phases` to a fresh policy and returns each transition.
func run(_ phases: [SceneVisibility]) -> [ScenePolicy.Transition] {
    var policy = ScenePolicy()
    return phases.map { policy.enter($0) }
}

// A fresh policy draws.
check(ScenePolicy().draws, "a fresh policy draws")

// Launch to the foreground: inactive then active, nothing to do.
check(run([.inactive, .active]) == [.none, .none], "launch to foreground")

// Control Centre or a call banner: inactive and back, still drawing.
do {
    var policy = ScenePolicy()
    check(policy.enter(.inactive) == .none, "inactive does not suspend")
    check(policy.draws, "inactive draws")
    check(policy.enter(.active) == .none, "inactive to active is no edge")
}

// Home button or lock: active, inactive, background — one suspend.
check(run([.active, .inactive, .background]) == [.none, .none, .suspend],
      "backgrounding suspends once, at background")

// Return: background, inactive, active — one resume, at inactive, because
// the screen is visible from that moment.
check(run([.background, .inactive, .active]) == [.suspend, .resume, .none],
      "return resumes once, at inactive")

// A direct background → active edge, which SwiftUI does not normally deliver
// but which must still resume.
check(run([.background, .active]) == [.suspend, .resume],
      "background straight to active resumes")

// A repeated phase is not an edge.
check(run([.background, .background]) == [.suspend, .none],
      "a repeated background does not suspend twice")
check(run([.active, .active]) == [.none, .none],
      "a repeated active is no edge")

// Launch into the background (the share extension opening the app is
// foreground, but a system launch need not be): the first report suspends.
do {
    var policy = ScenePolicy()
    check(policy.enter(.background) == .suspend, "launch in background suspends")
    check(!policy.draws, "and does not draw")
    check(policy.enter(.inactive) == .resume, "and resumes on the way in")
    check(policy.draws, "and draws again")
}

// Two full round trips: each suspends and resumes exactly once.
check(run([.active, .inactive, .background, .inactive, .active,
           .inactive, .background, .inactive, .active])
      == [.none, .none, .suspend, .resume, .none,
          .none, .suspend, .resume, .none],
      "two round trips")

print("scene_policy: \(checks - failures)/\(checks) checks passed")
if failures > 0 { exit(1) }
