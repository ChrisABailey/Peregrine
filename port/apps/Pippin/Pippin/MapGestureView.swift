// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import SwiftUI
import UIKit

/// The map's touch surface: a bare `UIView` carrying pan, pinch, tap, rotate
/// and long-press recognizers.
///
/// UIKit recognizers rather than SwiftUI gestures, for three things SwiftUI
/// does not hand over cleanly: the pinch centroid in view coordinates every
/// frame (`location(in:)`), incremental deltas (both recognizers are reset to
/// zero after each callback, so a pan and a pinch compose without either
/// remembering where it started), and the gesture state the render loop needs
/// to know a gesture is running and when it settles.
///
/// The tap must be able to fail in favour of the pan: a finger that moves a
/// few pixels is panning. It must also be able to report that nothing was
/// hit, which a gesture attached to a marker's own view could not do.
///
/// Rotation is guarded by a dead zone rather than by absence, so a pinch that
/// drifts a few degrees moves nothing and a deliberate turn starts from where
/// the fingers are. It is refused in GPS mode by the model
/// (`MapModel.rotate`), not here: the recognizer still runs as part of the
/// two-finger gesture and its answer is simply not taken.
///
/// The long press is the only recognizer that can take the touch away from
/// the others, because a waypoint being dragged and a chart being panned are
/// two readings of one finger. The arbitration is not `require(toFail:)`,
/// which would put half a second in front of every pan; the two are already
/// exclusive in time, since a press needs the finger still and a pan needs it
/// moving. The press refuses the pan afterwards, in
/// `gestureRecognizerShouldBegin`.
///
/// Whether the press grabbed anything is answered by the render queue a hop
/// later, so `onHoldBegan` takes a completion rather than returning a Bool.
/// In that window a moving finger is a pan nobody has ruled out, so the pan
/// is allowed to begin and is cancelled if the grab comes back positive: a
/// few points of unwanted pan is a smaller wrong than half a second of dead
/// map on every press that grabs nothing.
struct MapGestureView: UIViewRepresentable {
    let onBegan: () -> Void
    let onEnded: () -> Void
    let onPan: (CGSize) -> Void
    let onZoom: (CGFloat, CGPoint) -> Void
    /// A single finger down and up again without the map moving, in view
    /// points.
    let onTap: (CGPoint) -> Void
    /// A twist in degrees clockwise since the last callback, about the
    /// centroid of the two fingers. Degrees because every angle above this
    /// layer is in degrees.
    let onRotate: (CGFloat, CGPoint) -> Void
    /// A press held long enough to mean "take hold of this". The completion
    /// reports whether anything was grabbed, which decides whether the rest
    /// of the touch belongs to the map or to the grabbed thing.
    let onHoldBegan: (CGPoint, @escaping (Bool) -> Void) -> Void
    /// The held thing follows the finger. Sent only when something was grabbed.
    let onHoldMoved: (CGPoint) -> Void
    /// The finger lifted, at this point.
    let onHoldEnded: (CGPoint) -> Void
    /// The touch went away without a release: a call, a system gesture.
    let onHoldCancelled: () -> Void
    /// How long the press is held before `onHoldBegan`. Applied in
    /// `updateUIView` because it comes from the pack, which is read by a
    /// renderer that does not exist on the first pass through `body`.
    let holdSeconds: Double

    func makeUIView(context: Context) -> UIView {
        let view = TouchView()
        let pan = UIPanGestureRecognizer(target: context.coordinator,
                                         action: #selector(Coordinator.handlePan(_:)))
        // Two fingers pan as well as one: fingers drift during a pinch, and a
        // map that does not follow them feels stuck.
        pan.maximumNumberOfTouches = 2
        pan.delegate = context.coordinator

        let pinch = UIPinchGestureRecognizer(target: context.coordinator,
                                             action: #selector(Coordinator.handlePinch(_:)))
        pinch.delegate = context.coordinator

        let tap = UITapGestureRecognizer(target: context.coordinator,
                                         action: #selector(Coordinator.handleTap(_:)))
        // The pan wins any ambiguity: moving the map is never surprising, and
        // opening an unasked-for dialog is.
        tap.require(toFail: pan)
        tap.delegate = context.coordinator

        let rotate = UIRotationGestureRecognizer(
            target: context.coordinator,
            action: #selector(Coordinator.handleRotate(_:)))
        rotate.delegate = context.coordinator

        // `allowableMovement` stays at UIKit's 10 points: a thumb on a moving
        // bike is not still to within three. The duration, not the slop, is
        // what keeps this from stealing pans.
        let hold = UILongPressGestureRecognizer(
            target: context.coordinator,
            action: #selector(Coordinator.handleHold(_:)))
        hold.delegate = context.coordinator
        // The hold's handler needs the pan, to cancel one already in flight
        // when a grab comes back late.
        context.coordinator.pan = pan
        context.coordinator.hold = hold

        view.addGestureRecognizer(pan)
        view.addGestureRecognizer(pinch)
        view.addGestureRecognizer(tap)
        view.addGestureRecognizer(rotate)
        view.addGestureRecognizer(hold)
        return view
    }

    func updateUIView(_ uiView: UIView, context: Context) {
        context.coordinator.owner = self
        if holdSeconds > 0 {
            context.coordinator.hold?.minimumPressDuration = holdSeconds
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(owner: self) }

    /// Takes touches for the map and passes everything else through: the
    /// floating controls sit above this in the ZStack and get first refusal.
    final class TouchView: UIView {
        override init(frame: CGRect) {
            super.init(frame: frame)
            backgroundColor = .clear
            isMultipleTouchEnabled = true
        }
        @available(*, unavailable)
        required init?(coder: NSCoder) { fatalError("not used") }
    }

    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var owner: MapGestureView
        /// How many recognizers are mid-gesture. A pinch that ends while a
        /// pan continues must not report a settle.
        private var active = 0
        /// The map's pan, so a late grab can cancel one in flight.
        weak var pan: UIPanGestureRecognizer?
        /// The long press, so `updateUIView` can retune its duration once the
        /// pack has been read.
        weak var hold: UILongPressGestureRecognizer?

        init(owner: MapGestureView) { self.owner = owner }

        @objc func handlePan(_ g: UIPanGestureRecognizer) {
            track(g)
            guard g.state == .changed, let view = g.view else { return }
            let t = g.translation(in: view)
            // Reset to zero: the model wants the delta since the last
            // callback, not since the gesture began.
            g.setTranslation(.zero, in: view)
            owner.onPan(CGSize(width: t.x, height: t.y))
        }

        @objc func handlePinch(_ g: UIPinchGestureRecognizer) {
            track(g)
            guard g.state == .changed, let view = g.view else { return }
            let factor = g.scale
            g.scale = 1.0
            // The centroid now, so the map stays under the fingers as they
            // move.
            owner.onZoom(factor, g.location(in: view))
        }

        /// How far a twist goes before the chart moves, in radians. Twelve
        /// degrees is a wall rather than a tolerance: two fingers pinching on
        /// a bicycle rotate several degrees without anybody meaning it.
        private static let rotationDeadZone: CGFloat = .pi / 15

        /// The dead zone has been paid and the chart is following.
        private var rotationArmed = false
        /// Signed twist spent so far, so a wobble never adds up to a turn.
        private var rotationSlack: CGFloat = 0

        @objc func handleRotate(_ g: UIRotationGestureRecognizer) {
            track(g)
            switch g.state {
            case .began:
                rotationArmed = false
                rotationSlack = 0
                g.rotation = 0
            case .changed:
                guard let view = g.view else { return }
                let delta = g.rotation
                // Reset to zero, as the pan and pinch do.
                g.rotation = 0
                if !rotationArmed {
                    rotationSlack += delta
                    guard abs(rotationSlack) >= Self.rotationDeadZone else { return }
                    rotationArmed = true
                    // Only what is past the wall is applied; handing over the
                    // whole slack would snap the chart twelve degrees as it
                    // arms.
                    let past = rotationSlack
                        - (rotationSlack < 0 ? -Self.rotationDeadZone : Self.rotationDeadZone)
                    owner.onRotate(past * 180 / .pi, g.location(in: view))
                    return
                }
                owner.onRotate(delta * 180 / .pi, g.location(in: view))
            default:
                break
            }
        }

        // MARK: Long press

        /// The press is down and has not ended, grabbed or not.
        private var holdLive = false
        /// Something was grabbed: every remaining touch belongs to it.
        private var holdCapturing = false

        @objc func handleHold(_ g: UILongPressGestureRecognizer) {
            // Not routed through `track`: a press that grabs nothing must not
            // put the render loop in live mode for as long as a finger rests
            // on the map. A press that grabs reports through the model, whose
            // content-dirty flag keeps the loop running.
            guard let view = g.view else { return }
            switch g.state {
            case .began:
                holdLive = true
                holdCapturing = false
                owner.onHoldBegan(g.location(in: view)) { [weak self] grabbed in
                    guard let self else { return }
                    guard self.holdLive else {
                        // Press and lift faster than a render-queue hop.
                        // Anything grabbed must be released or the next
                        // gesture starts holding it.
                        if grabbed { self.owner.onHoldCancelled() }
                        return
                    }
                    guard grabbed else { return }
                    self.holdCapturing = true
                    // A gesture with no button needs to acknowledge itself;
                    // the halo the map draws is the visible half.
                    UIImpactFeedbackGenerator(style: .medium).impactOccurred()
                    // The finger may have started a pan while this answer was
                    // in flight. Disabling and re-enabling is UIKit's way to
                    // cancel a recognizer mid-touch; from here
                    // `gestureRecognizerShouldBegin` keeps it out.
                    if let pan = self.pan, pan.state == .began
                        || pan.state == .changed {
                        pan.isEnabled = false
                        pan.isEnabled = true
                    }
                }
            case .changed:
                if holdCapturing { owner.onHoldMoved(g.location(in: view)) }
            case .ended:
                if holdCapturing { owner.onHoldEnded(g.location(in: view)) }
                holdLive = false
                holdCapturing = false
            case .cancelled, .failed:
                if holdCapturing { owner.onHoldCancelled() }
                holdLive = false
                holdCapturing = false
            default:
                break
            }
        }

        @objc func handleTap(_ g: UITapGestureRecognizer) {
            // Not routed through `track`: a began/ended pair would put the
            // loop in live-render mode for a touch that moves nothing.
            guard g.state == .ended, let view = g.view else { return }
            owner.onTap(g.location(in: view))
        }

        private func track(_ g: UIGestureRecognizer) {
            switch g.state {
            case .began:
                active += 1
                if active == 1 { owner.onBegan() }
            case .ended, .cancelled, .failed:
                active = max(0, active - 1)
                if active == 0 { owner.onEnded() }
            default:
                break
            }
        }

        /// Everything recognizes simultaneously. Pan and pinch are one
        /// gesture with two hands' worth of information in it, and the long
        /// press must be included too: left to argue with the pan, the pan
        /// recognizes on movement and usually wins, so a waypoint could never
        /// be dragged. They are separated by `gestureRecognizerShouldBegin`
        /// instead.
        func gestureRecognizer(
            _ g: UIGestureRecognizer,
            shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer
        ) -> Bool { true }

        /// Refuses pan, pinch and rotate while a waypoint is held. Zoom and
        /// rotate are refused as well as pan because `DragTo` resolves a
        /// pixel through the frame's projection: a map zooming or turning
        /// under a stationary finger would move the waypoint without the
        /// finger moving.
        ///
        /// The tap needs no refusal: `tap.require(toFail: pan)` leaves it
        /// waiting on a pan that never began, and a half-second press is
        /// past a tap's own timing.
        func gestureRecognizerShouldBegin(_ g: UIGestureRecognizer) -> Bool {
            guard holdCapturing else { return true }
            return !(g is UIPanGestureRecognizer
                     || g is UIPinchGestureRecognizer
                     || g is UIRotationGestureRecognizer)
        }
    }
}
