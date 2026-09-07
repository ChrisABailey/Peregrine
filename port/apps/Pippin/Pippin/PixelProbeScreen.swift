// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI

// The whole file is DEBUG only: a measuring instrument behind a launch
// argument, reachable by no rider. The `-PPPixelProbe` / `-PPViewportProbe`
// switch in `PippinApp` is inside the same guard, so a Release build has no
// probe and no reference to one.
#if DEBUG

/// Shows the `PixelBuffer` to `CGImage` test image. Launch with
/// `-PPPixelProbe YES`.
///
/// The quad sits on black because that is what makes the answer visible: the
/// fourth quadrant is half-alpha white and must read mid-grey. Reading white
/// means the alpha is being taken as premultiplied, and every translucent
/// thing the app draws will be too bright.
struct PixelProbeScreen: View {
    private let quad = PPPixelProbe.quadImage(size: 256)
    private let report = PPPixelProbe.report()

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                Text("PixelBuffer → CGImage")
                    .font(.headline)
                ZStack {
                    Color.black
                    if let quad {
                        Image(decorative: quad, scale: 1)
                            .interpolation(.none)
                            .resizable()
                            .frame(width: 220, height: 220)
                    }
                }
                .frame(height: 260)
                Text("bottom-right must be MID-GREY, not white")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text(report)
                    .font(.system(size: 9, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
                Text(PPPixelProbe.passes() ? "PASS" : "FAIL")
                    .font(.title2.bold())
                    .foregroundStyle(PPPixelProbe.passes() ? .green : .red)
            }
            .padding()
        }
    }
}

#endif  // DEBUG
