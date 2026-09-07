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

/// Measures the gesture arithmetic and reports pass/fail per check.
///
///     xcrun simctl launch booted org.peregrine.Pippin --args -PPViewportProbe YES
///
/// It cannot press the screen: there is no way to inject a touch from outside
/// a UI test. Everything from `MapModel.pan(by:)` down is covered, which is
/// where the arithmetic lives.
enum ViewportProbe {
    struct Check {
        let name: String
        let detail: String
        let ok: Bool
    }

    /// Approximate metres between two positions. Equirectangular: this
    /// reports error, it is not a geodesy library.
    static func metres(_ a: PPGeoPoint, _ b: PPGeoPoint) -> Double {
        let mPerDeg = 111_320.0
        let dLat = (a.latitude - b.latitude) * mPerDeg
        let dLon = (a.longitude - b.longitude) * mPerDeg
            * cos(a.latitude * .pi / 180.0)
        return (dLat * dLat + dLon * dLon).squareRoot()
    }

    /// The same error in points, which is the unit that decides whether
    /// anybody can see it: 9 m is a disaster at a junction and a fifth of a
    /// pixel on an island view.
    static func points(_ a: PPGeoPoint, _ b: PPGeoPoint, in v: PPViewport) -> Double {
        let metresPerPoint =
            v.scaleDenominator * v.mmPerPixel / 1000.0 * Double(v.displayScale)
        return metresPerPoint > 0 ? metres(a, b) / metresPerPoint : 0
    }

    static func run(from start: PPViewport, size: CGSize, scale: CGFloat) -> [Check] {
        var checks: [Check] = []
        let home = start.resized(to: size, displayScale: scale).atHomeView()
        let anchor = CGPoint(x: size.width * 0.28, y: size.height * 0.62)
        let centre = CGPoint(x: size.width / 2, y: size.height / 2)

        // 1. The startup view covers the screen with the pack. Cover, not fit:
        //    no edge of the pack's box may be inside the screen on either
        //    axis, or the app opens with bands of background beside the map.
        let bounds = home.centerBounds
        let sw = home.point(forGeo: PPGeoPoint(latitude: bounds.southWest.latitude,
                                               longitude: bounds.southWest.longitude))
        let ne = home.point(forGeo: PPGeoPoint(latitude: bounds.northEast.latitude,
                                               longitude: bounds.northEast.longitude))
        // North is up, so the NE corner is the top right: y grows downwards.
        checks.append(Check(
            name: "home view fills the screen",
            detail: String(format: "1:%.0f · pack spans x %.0f…%.0f of %.0f, y %.0f…%.0f of %.0f pt",
                           home.scaleDenominator, sw.x, ne.x, size.width,
                           ne.y, sw.y, size.height),
            ok: sw.x <= 1 && ne.x >= size.width - 1
                && ne.y <= 1 && sw.y >= size.height - 1))

        // 2. Drift under the finger, measured separately for the two axes. A
        //    pan is "which position is under the centre pixel now", so the
        //    centre is exact and everything else is not quite: re-centring an
        //    equal-arc projection changes its degrees-per-pixel with the
        //    cosine of the new centre latitude, so only a north-south drag
        //    rescales the frame under the finger. Reported in points, since
        //    that is the unit a finger is in; half a point is the bar.
        let across = CGVector(dx: -137, dy: 0)
        let down = CGVector(dx: 0, dy: 84)
        for (label, d) in [("across", across), ("down", down)] {
            let before = home.geo(at: anchor)
            let panned = home.panned(by: d)
            let after = panned.geo(at: CGPoint(x: anchor.x + d.dx, y: anchor.y + d.dy))
            let slipPt = points(before, after, in: panned)
            checks.append(Check(
                name: "pan: no drift under the finger (\(label))",
                detail: String(format: "%.3f pt (%.2f m) over a %.0f,%.0f pt drag",
                               slipPt, metres(before, after), d.dx, d.dy),
                ok: slipPt < 0.5))
        }

        // 3. A pan is reversible, to within the same cosine — hence a
        //    tolerance of a point rather than a millimetre.
        let d = CGVector(dx: across.dx, dy: down.dy)
        let panned = home.panned(by: d)
        let there_and_back = panned.panned(by: CGVector(dx: -d.dx, dy: -d.dy))
        let roundTrip = points(home.center, there_and_back.center, in: home)
        checks.append(Check(
            name: "pan: round trip returns",
            detail: String(format: "%.3f pt (%.2f m)", roundTrip,
                           metres(home.center, there_and_back.center)),
            ok: roundTrip < 0.5))

        // 4. A pinch keeps the position under the fingers.
        let under = home.geo(at: anchor)
        let zoomed = home.zoomed(by: 2.4, about: anchor)
        let stillUnder = zoomed.geo(at: anchor)
        let slip = points(under, stillUnder, in: zoomed)
        checks.append(Check(
            name: "pinch: the anchor holds",
            detail: String(format: "%.3f pt at x2.4 (1:%.0f → 1:%.0f)", slip,
                           home.scaleDenominator, zoomed.scaleDenominator),
            ok: slip < 0.5))

        // 5. The anchor still holds at the limit, which pins the order: the
        //    clamp happens before the anchor correction.
        let hardIn = home.zoomed(by: 1e6, about: anchor)
        let slipAtLimit = points(under, hardIn.geo(at: anchor), in: hardIn)
        checks.append(Check(
            name: "pinch: clamped in, anchor still holds",
            detail: String(format: "1:%.0f (limit 1:%.0f) · %.3f pt",
                           hardIn.scaleDenominator, hardIn.minScaleDenominator,
                           slipAtLimit),
            ok: abs(hardIn.scaleDenominator - hardIn.minScaleDenominator) < 1e-6
                && slipAtLimit < 0.5))

        // 6. And out. The far limit is the pack's extent plus two levels, so
        //    a pinch-out cannot leave the data behind.
        let hardOut = home.zoomed(by: 1e-6, about: centre)
        checks.append(Check(
            name: "pinch: clamped out at the pack's extent",
            detail: String(format: "1:%.0f (limit 1:%.0f, home 1:%.0f)",
                           hardOut.scaleDenominator, hardOut.maxScaleDenominator,
                           home.scaleDenominator),
            ok: abs(hardOut.scaleDenominator - hardOut.maxScaleDenominator) < 1e-6
                && hardOut.maxScaleDenominator > home.scaleDenominator))

        // 7. A drag to Africa ends at the edge of the data, so there is no
        //    lost-in-the-ocean state to get out of.
        var far = home
        for _ in 0..<40 { far = far.panned(by: CGVector(dx: size.width, dy: 0)) }
        checks.append(Check(
            name: "pan: the centre stays over the pack",
            detail: String(format: "%.5f°, west edge %.5f°",
                           far.center.longitude, bounds.southWest.longitude),
            ok: far.center.longitude >= bounds.southWest.longitude - 1e-9
                && far.center.longitude <= bounds.northEast.longitude + 1e-9))

        // 8. A device rotation keeps the camera; only the limits move,
        //    because a wider screen fits the island at a nearer scale.
        let landscape = zoomed.resized(to: CGSize(width: size.height, height: size.width),
                                       displayScale: scale)
        checks.append(Check(
            name: "resize keeps the camera",
            detail: String(format: "%.5f,%.5f · 1:%.0f → 1:%.0f",
                           landscape.center.latitude, landscape.center.longitude,
                           zoomed.scaleDenominator, landscape.scaleDenominator),
            ok: points(landscape.center, zoomed.center, in: landscape) < 0.01))

        // 9. What the limits mean as ground across the screen: whether a
        //    junction is legible, and whether the island still fits.
        let inM = home.minScaleDenominator * home.mmPerPixel / 1000.0
            * Double(size.width) * Double(scale)
        let outM = home.maxScaleDenominator * home.mmPerPixel / 1000.0
            * Double(size.width) * Double(scale)
        checks.append(Check(
            name: "zoom range, as ground across the screen",
            detail: String(format: "in %.0f m … out %.0f m", inM, outM),
            ok: inM < 600 && outM > 3000))

        return checks
    }

    static func report(_ checks: [Check]) -> String {
        checks.map { "\($0.ok ? "PASS" : "FAIL")  \($0.name)\n      \($0.detail)" }
            .joined(separator: "\n")
    }
}

/// The probe on screen, so a gesture session's acceptance artifact is a
/// screenshot. `-PPViewportProbe YES`.
struct ViewportProbeScreen: View {
    @State private var checks: [ViewportProbe.Check] = []
    @State private var failure: String?
    @Environment(\.displayScale) private var displayScale

    var body: some View {
        GeometryReader { geo in
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text("Gestures → PPViewport")
                        .font(.headline)
                    if let failure {
                        Text(failure).font(.callout).foregroundStyle(.red)
                    }
                    ForEach(Array(checks.enumerated()), id: \.offset) { _, c in
                        VStack(alignment: .leading, spacing: 2) {
                            HStack(alignment: .firstTextBaseline, spacing: 8) {
                                Text(c.ok ? "PASS" : "FAIL")
                                    .font(.system(size: 11, weight: .bold, design: .monospaced))
                                    .foregroundStyle(c.ok ? .green : .red)
                                Text(c.name).font(.callout)
                            }
                            Text(c.detail)
                                .font(.system(size: 10, design: .monospaced))
                                .foregroundStyle(.secondary)
                        }
                    }
                    if !checks.isEmpty {
                        Text(checks.allSatisfy(\.ok) ? "PASS" : "FAIL")
                            .font(.title2.bold())
                            .foregroundStyle(checks.allSatisfy(\.ok) ? .green : .red)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding()
            }
            .onAppear { load(size: geo.size) }
        }
    }

    private func load(size: CGSize) {
        guard checks.isEmpty else { return }
        guard let pack = Bundle.main.url(forResource: "Data", withExtension: nil) else {
            failure = "The data pack is missing from the app bundle."
            return
        }
        do {
            let map = try PPMap(dataPack: pack)
            let result = ViewportProbe.run(from: map.initialViewport(),
                                           size: size, scale: displayScale)
            checks = result
            // Also to the console, which is how it is read over
            // `simctl launch --console`.
            print("PPViewportProbe\n" + ViewportProbe.report(result))
        } catch {
            failure = error.localizedDescription
        }
    }
}

/// Drives the same model calls `MapGestureView` makes, on a timer, so a
/// session that cannot inject a touch can still screenshot a map that has
/// been dragged and pinched. `-PPGestureDemo YES`.
///
/// It runs once at launch and stops, leaving the map ready for a finger.
@MainActor
enum GestureDemo {
    static func run(on model: MapModel, size: CGSize) {
        let anchor = CGPoint(x: size.width * 0.5, y: size.height * 0.42)
        var step = 0
        model.gestureBegan()
        Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { timer in
            MainActor.assumeIsolated {
                step += 1
                if step <= 24 {
                    // North-west into the island, then a pinch about a point
                    // above the middle, ending somewhere with street names on
                    // it rather than out at sea.
                    model.pan(by: CGSize(width: 5, height: 11))
                    model.zoom(by: 1.05, about: anchor)
                } else {
                    timer.invalidate()
                    model.gestureEnded()
                }
            }
        }
    }
}

#endif  // DEBUG
