// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
//
// Pippin — offline cycling and walking navigation on Kiawah Island, the
// third shell over the Peregrine core. Plan: port/pippin-plan.md.

import SwiftUI

@main
struct PippinApp: App {
    /// Which measurement screen to show, or nil for the map. Each is a
    /// screen rather than a log line so the answer can be photographed, and
    /// each is behind a launch argument rather than a button so the shipping
    /// app keeps the one screen the UI design describes.
    ///
    ///   -PPPixelProbe YES     PixelBuffer to CGImage, the alpha question
    ///   -PPViewportProbe YES  gestures to PPViewport, the drift question
    ///
    /// DEBUG only: a shipping build cannot reach these, so it does not carry
    /// them. Under this guard the Release app is `MapScreen` alone.
    #if DEBUG
    private var probe: String? {
        if UserDefaults.standard.bool(forKey: "PPPixelProbe") { return "pixel" }
        if UserDefaults.standard.bool(forKey: "PPViewportProbe") { return "viewport" }
        return nil
    }
    #endif

    var body: some Scene {
        WindowGroup {
            #if DEBUG
            switch probe {
            case "pixel": PixelProbeScreen()
            case "viewport": ViewportProbeScreen()
            default: MapScreen()
            }
            #else
            MapScreen()
            #endif
        }
    }
}
