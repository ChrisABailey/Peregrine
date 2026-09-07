// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// RenderGate.swift — decides whether a newly-arrived fix is worth a frame.
//
// Without this, `MapModel.receive(_ fix:)` called `setContentDirty()` on
// every fix, which wakes the display link and draws. There is no raster cache
// under that, so `PPMap` re-opens the MBTiles and rasterizes vector tiles for
// a symbol that may be nowhere near the screen: a rider browsing the far end
// of the island paid a decode-and-draw once a second for a chevron behind
// their thumb.
//
// So when nothing is following, nothing is recording, and the ship is off
// screen, the frame is allowed to stand still. That state is bounded by
// Auto-Lock and by the rider's attention, so it is not a long-lived leak.
//
// It carries one bit of state, in its own file, because the state is the part
// that can be subtly wrong. A gate that only asks "is the ship visible now"
// has two bugs that no still frame shows:
//
//   * The ship that never comes back. The test must run on every fix even
//     when the render does not, since projecting one point is free next to a
//     render and is the only thing that notices the ship returning.
//   * The chevron pinned to the edge. The last fix before the ship leaves is
//     drawn; skipping the next one leaves that stale symbol half on the edge
//     for as long as the rider keeps going. So the departure gets one frame
//     too (`visible || wasVisible`), which draws the ship off the canvas and
//     clears it honestly.
//
// Nothing a normal user sees freezes: the trip readout is gated on `gpsMode`,
// which this excludes by definition, and the only other consumers of
// `ownship` and `status` are in `statsOverlay`, behind `-PPShowStats`.

import CoreGraphics

/// Whether a newly-arrived fix is worth a frame.
///
/// Owned by `MapModel` and touched only from the main thread. A struct, so
/// the caller's `mutating` call site is where the one bit of state visibly
/// changes.
struct RenderGate {
    /// Whether the ship was on screen (or within a symbol of it) when last
    /// asked. Starts true, which is the safe way round: the first fix of a
    /// launch draws, and every uncertainty below resolves the same way. A
    /// gate starting false would leave the map empty until the ship moved.
    private var shipWasVisible = true

    /// Decides whether to render, from an already-projected ship position.
    /// `PPViewport.point(forGeo:)` is the one projection this app trusts, and
    /// keeping it on the model's side is what lets this file compile and be
    /// tested against nothing but CoreGraphics.
    ///
    /// A nil `screenPoint` means the question could not be asked: no fix, no
    /// surface, or a non-finite projection. All of those render and record
    /// the ship as visible, because a gate that guesses when it does not know
    /// is how a feature breaks in the one case nobody tested.
    mutating func shouldRender(shipAt screenPoint: CGPoint?,
                               inSurface surface: CGSize,
                               symbolRadius: CGFloat,
                               following: Bool,
                               recording: Bool) -> Bool {
        // Either one ends the question. Following moves the camera on every
        // fix, so the frame is stale by definition; recording refreshes
        // `recordedPointCount` off the frame, and a rider watching a ride
        // being written should see it tick. The recorded file itself would
        // survive gating, since a live receiver's fixes reach the recorder
        // through `pushFix:` — but the demo feed is polled inside the tick,
        // so a replay being recorded would lose points.
        if following || recording {
            shipWasVisible = true
            return true
        }
        guard let screenPoint,
              screenPoint.x.isFinite, screenPoint.y.isFinite,
              surface.width > 0, surface.height > 0 else {
            shipWasVisible = true
            return true
        }
        // The viewport grown by the symbol radius: the ship is drawn around
        // its position, so a chevron whose centre is just past the edge still
        // has its nose on screen.
        let visible = screenPoint.x >= -symbolRadius
            && screenPoint.y >= -symbolRadius
            && screenPoint.x <= surface.width + symbolRadius
            && screenPoint.y <= surface.height + symbolRadius
        defer { shipWasVisible = visible }
        return visible || shipWasVisible
    }
}
