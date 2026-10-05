// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ScenePolicy.swift — whether the map may draw, from the scene's phase.
//
// A backgrounded app that keeps scheduling frames is doing work it was told
// to stop, and that risks a watchdog kill. Fixes keep arriving through
// `MapModel.receive(_:)` regardless; only drawing is gated here.
//
// Following and recording do not change the answer: nothing is drawn in the
// background either way. Whether the receiver keeps running is
// `LocationPolicy`'s question, not this one.
//
// Foundation-only, like `RenderGate`, so the transition table is tested on
// the mac (`ctest -R scene_policy`).

/// The scene's phase, mirroring SwiftUI's `ScenePhase` without importing it.
enum SceneVisibility {
    /// Frontmost and receiving input.
    case active
    /// Visible but not receiving input: Control Centre, a call banner, the
    /// app switcher, and every return from the background on its way to active.
    case inactive
    /// Not visible.
    case background
}

/// Tracks whether the map draws, and reports the edges a caller acts on.
/// Owned by `MapModel` and touched only from the main thread.
struct ScenePolicy {
    /// What a phase change asks the caller to do.
    enum Transition: Equatable {
        /// Nothing changed for drawing.
        case none
        /// Stop drawing: pause the display link, publish no frame.
        case suspend
        /// Draw again: one exact frame at the current fix, without animating
        /// the camera from where the map was before.
        case resume
    }

    /// Whether frames may be drawn and published. Starts true: a scene that
    /// launches in the background reports it, and one that does not draws.
    private(set) var draws = true

    /// Records a new phase and returns the edge it crossed, if any.
    /// `.inactive` draws, because the screen is still visible.
    mutating func enter(_ phase: SceneVisibility) -> Transition {
        let next = phase != .background
        defer { draws = next }
        switch (draws, next) {
        case (true, false): return .suspend
        case (false, true): return .resume
        default: return .none
        }
    }
}
