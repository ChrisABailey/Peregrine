// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Combine
import PippinKit
import SwiftUI
import UIKit

/// View model for the map screen: the camera, the last rendered frame, and
/// the render loop between them.
///
/// Threading:
///   * `viewport` (the camera) lives on the main thread as an immutable value.
///     Gestures derive a new one and publish it.
///   * `PPMap` (pack, renderer, canvas) is touched only on `renderQueue`. It
///     takes a viewport and returns a `PPFrame` whose image is already a copy.
///   * At most one render is in flight at a time.
///
/// The `CADisplayLink` pauses itself when nothing is dirty; `setNeedsRender()`
/// wakes it. Two dirty flags exist because a frame does not depend only on the
/// camera: `needsRender` for camera moves and `contentDirty` for anything else
/// (ownship fix, route edit, mode change). The "same viewport, skip the render"
/// shortcut is only valid when `contentDirty` is clear.
///
/// Rendering also happens during a gesture whenever the queue is idle and the
/// last base draw came in under `liveRenderBudgetMs`; otherwise the preview
/// transform in `MapScreen` carries the gesture until settle.
@MainActor
final class MapModel: ObservableObject {
    /// The camera. Every writer (pan, pinch, twist, resize, moving-map answer)
    /// goes through this property, so the crosshair label and the location
    /// accuracy policy are refreshed from one `didSet` rather than at each site.
    @Published private(set) var viewport: PPViewport? {
        didSet {
            if pickProfile != nil { refreshPickPlace() }
            reconsiderLocationAccuracy()
        }
    }
    /// The last finished frame, and the viewport it was drawn at.
    @Published private(set) var frame: PPFrame?
    @Published private(set) var status: String = ""
    @Published private(set) var failure: String?

    /// Ownship position as of the last drawn frame (the render queue's answer).
    @Published private(set) var ownship: PPOwnship?

    /// Trip computer readout as of the last frame, or nil outside GPS mode.
    /// Arrives on the frame because the trip computer is fed on the render queue.
    @Published private(set) var trip: PPTrip?

    /// The route on the map, or nil until one has been loaded or set. A
    /// snapshot taken on the render queue; see `PPRoute.h`.
    @Published private(set) var route: PPRoute?
    /// A plan is in flight; bound to the sheet's spinner.
    @Published private(set) var isPlanning = false
    @Published private(set) var feedIsRunning = false

    /// Whether the receiver is currently in its coarse (battery-saving) tier.
    /// Shown on the `-PPShowStats` line; a tier change has no visible effect
    /// otherwise.
    @Published private(set) var receiverIsCoarse = false
    @Published private(set) var locationAuthorization: PPLocationAuthorization =
        .notDetermined

    /// GPS mode: the map follows the ship and the road snapper is on. Whether
    /// the chart also turns with the rider is `courseUp`.
    @Published private(set) var gpsMode = false { didSet { updateIdleTimer() } }

    /// Course-up: the chart turns so the way ahead is up the screen, and the
    /// map recentres continuously. Remembered outside GPS mode so the next
    /// press of the GPS button comes up in the same state. Startup value is
    /// the pack's `movingmap.course_up`.
    @Published private(set) var courseUp = true

    /// The points on the map in document (and draw) order. A snapshot taken on
    /// the render queue. Sheets re-read a point from here after an edit rather
    /// than trusting what the editor typed; see `point(id:)`.
    @Published private(set) var points: [PPMapPoint] = []

    /// Whether the point overlay is drawn. Not a document property.
    @Published private(set) var pointsVisible = true

    /// The hints for the gestures that have no visible affordance.
    private var hints = MapHints()

    /// The document's embedded artwork, for the editor's symbol picker. Read
    /// once with the points; nothing adds artwork at runtime.
    @Published private(set) var pointSymbols: [PPPointSymbol] = []

    /// The point whose info sheet is up, or nil. Setting it also highlights
    /// the marker.
    @Published private(set) var selectedPoint: PPMapPoint?

    /// A transient message for the user. Separate from `status`, which every
    /// finished frame overwrites. See `MapNotice`.
    @Published private(set) var notice: MapNotice?
    private var noticeTask: Task<Void, Never>?

    /// A base draw slower than this stops live rendering during a gesture and
    /// leaves the preview transform to carry it until settle. 40 ms is two
    /// frames at 60 Hz.
    private let liveRenderBudgetMs = 40.0

    private let renderer: Renderer?
    private let renderQueue = DispatchQueue(
        label: "org.peregrine.Pippin.render", qos: .userInitiated)

    private var memoryWarning: NSObjectProtocol?
    private var link: CADisplayLink?
    private var needsRender = false
    private var renderInFlight = false
    private var gestureActive = false

    /// Something other than the camera changed the picture. Defeats the
    /// same-viewport shortcut in `tick()`.
    private var contentDirty = false

    /// Decides whether a fix is worth a frame. Stateful (remembers whether the
    /// ship was visible last time), so it is stored rather than made per fix.
    private var renderGate = RenderGate()

    /// Decides how hard the receiver should work. Stateful like `renderGate`.
    private var locationPolicy = LocationPolicy()

    /// The ownship symbol's reach in points; a launch-time constant from the pack.
    private var ownshipSymbolRadius: Double { renderer?.ownshipSymbolRadius ?? 0 }

    /// The phone's receiver. Nil until `startFeed()`, so the permission prompt
    /// does not appear at launch.
    private var locationSource: PPLocationSource?
    private var locationProxy: AnyObject?

    /// Clock for the demo feed. A live receiver pushes fixes and wakes the
    /// loop itself; a `ScriptedSource` has no thread and only emits when a
    /// render polls it, so the demo needs a timer to request those renders.
    /// Four ticks a second is enough for a 1 Hz track.
    private var demoTimer: Timer?
    private let demoPollInterval = 0.25

    /// `startFeed` has been called and not stopped. Distinct from
    /// `feedIsRunning`, which an authorization refusal clears. Makes a second
    /// `onAppear` a no-op.
    private var feedRequested = false

    /// `loadSavedRoute` / `loadPoints` have run. A second `onAppear` must not
    /// re-read the documents underneath an edit.
    private var routeLoaded = false
    private var pointsLoaded = false

    init() {
        guard let pack = Bundle.main.url(forResource: "Data", withExtension: nil) else {
            renderer = nil
            failure = "The data pack is missing from the app bundle."
            return
        }
        do {
            let map = try PPMap(dataPack: pack)
            // Document locations are a Foundation question, answered here.
            // When and in what order to write is the C++ store's business.
            map.setRouteDocumentURL(Self.routeDocumentURL())
            map.setPointsDocumentURL(Self.pointsDocumentURL())
            renderer = Renderer(map: map)
            courseUp = renderer?.initialCourseUp ?? true
            // Centred on the pack with its zoom limits applied; no surface
            // until the view reports one.
            viewport = map.initialViewport()
        } catch {
            renderer = nil
            failure = error.localizedDescription
        }
        // Restore the stored symbol size before the first frame so the launch
        // does not flash the pack's default. Clamped in case the pack's step
        // list has shrunk since it was stored.
        symbolStep = max(0, min(UserDefaults.standard.integer(
            forKey: Self.symbolStepKey), symbolZoomSteps.count - 1))
        applySymbolZoom()
        startDisplayLink()
        // The cached base map is the only large allocation not currently
        // needed; dropping it costs one render.
        memoryWarning = NotificationCenter.default.addObserver(
            forName: UIApplication.didReceiveMemoryWarningNotification,
            object: nil, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated {
                    guard let self, let renderer = self.renderer else { return }
                    self.renderQueue.async { renderer.invalidateBaseLayer() }
                }
            }
    }

    deinit {
        if let memoryWarning {
            NotificationCenter.default.removeObserver(memoryWarning)
        }
        link?.invalidate()
        demoTimer?.invalidate()
        noticeTask?.cancel()
    }

    // MARK: - View callbacks

    /// Recomputes the viewport for a new surface size and display scale. On
    /// the first layout the camera is placed at the pack's home view.
    func resize(to size: CGSize, scale: CGFloat) {
        guard let current = viewport, size.width > 0, size.height > 0 else { return }
        let hadSurface = current.hasSurface
        var next = current.resized(to: size, displayScale: scale)
        if !hadSurface { next = next.atHomeView() }
        viewport = next
        setNeedsRender()
    }

    // MARK: - Gestures (arithmetic is in PPViewport)

    func gestureBegan() {
        gestureActive = true
        baseDrawsThisGesture = 0
    }

    func gestureEnded() {
        gestureActive = false
        // Settle frame: replace whatever the preview transform was showing.
        setNeedsRender()
    }

    /// Pans the content by `translation` in points.
    func pan(by translation: CGSize) {
        guard let current = viewport else { return }
        viewport = current.panned(
            by: CGVector(dx: translation.width, dy: translation.height))
        setNeedsRender()
    }

    /// Zooms by `factor` (> 1 is in), keeping the position under `anchor` fixed.
    func zoom(by factor: CGFloat, about anchor: CGPoint) {
        guard let current = viewport else { return }
        viewport = current.zoomed(by: Double(factor), about: anchor)
        setNeedsRender()
    }

    /// Rotates by `degrees` clockwise since the last callback, keeping the
    /// position under `anchor` fixed. Ignored in GPS mode, where the camera
    /// owns the rotation.
    func rotate(by degrees: CGFloat, about anchor: CGPoint) {
        guard !gpsMode, let current = viewport else { return }
        viewport = current.rotated(by: Double(degrees), about: anchor)
        setNeedsRender()
    }

    // MARK: - Position feed

    /// Starts the position feed: the recorded demo ride when `-PPDemoFeed YES`
    /// is passed to a DEBUG build, otherwise the phone's receiver. Starting
    /// the feed does not make the map follow it; see `setGpsMode`.
    func startFeed() {
        guard let renderer, !feedRequested else { return }
        feedRequested = true
        // Replay is DEBUG only. Release packs do not carry the recorded ride
        // (`stage_data.py --release`).
        #if DEBUG
        if UserDefaults.standard.bool(forKey: "PPDemoFeed") {
            renderQueue.async { [weak self] in
                let result = renderer.startDemoFeed()
                Task { @MainActor in
                    guard let self else { return }
                    switch result {
                    case .success:
                        self.feedIsRunning = true
                        self.startDemoTimer()
                        self.setContentDirty()
                    case .failure(let error):
                        self.feedRequested = false
                        self.failure = error.localizedDescription
                    }
                }
            }
            return
        }
        #endif

        let source = PPLocationSource()
        let proxy = LocationProxy(
            onFix: { [weak self] fix in self?.receive(fix) },
            onAuthorization: { [weak self] auth in self?.receive(auth) },
            onFailure: { [weak self] error in
                // Non-fatal. `PPLocationSource` already swallows
                // kCLErrorLocationUnknown.
                self?.status = error.localizedDescription
            })
        // The source holds its delegate weakly; keep the proxy alive here.
        locationProxy = proxy
        source.delegate = proxy
        locationSource = source
        locationAuthorization = source.authorization
        source.start()
        feedIsRunning = true
    }

    // MARK: - GPS mode

    /// Toggles GPS mode. Entering asks the map to follow; leaving asks it to
    /// stop and unwind the chart to north.
    func toggleGpsMode() {
        guard renderer != nil else { return }
        setGpsMode(!gpsMode)
    }

    func setGpsMode(_ on: Bool) {
        guard let renderer, on != gpsMode else { return }
        if on {
            // Request full accuracy first: the receiver may have been idling
            // coarse, and GNSS warm-up starts from this call.
            demandFullAccuracy()
            // A denied receiver is reported at the moment it is needed, not
            // at launch.
            if !feedIsRunning && locationAuthorization != .authorized {
                post(Self.authorizationNotice(locationAuthorization))
                return
            }
            applyZoomOutRule()
        }
        gpsMode = on
        renderQueue.async { renderer.setGpsMode(on) }
        // Mode change without a camera move.
        setContentDirty()
    }

    // MARK: - Auto-Lock

    /// Keeps the screen awake while following or recording. The app has no
    /// background location mode, so Auto-Lock would stop fix delivery: a
    /// frozen moving map, or a hole in the recording. The flag only binds
    /// while the app is frontmost, so nothing needs unwinding on a
    /// scene-phase change. Low Power Mode overrides it.
    private func updateIdleTimer() {
        let keepAwake = gpsMode || isRecording
        guard UIApplication.shared.isIdleTimerDisabled != keepAwake else { return }
        UIApplication.shared.isIdleTimerDisabled = keepAwake
    }

    // MARK: - Compass

    /// The compass button. In GPS mode it toggles course-up. Otherwise it is
    /// on screen only because the user rotated the map, and it returns the
    /// chart to north without changing `courseUp`.
    func toggleCompass() {
        guard renderer != nil else { return }
        if gpsMode {
            setCourseUp(!courseUp)
        } else {
            returnToNorth()
        }
    }

    func setCourseUp(_ on: Bool) {
        guard let renderer, on != courseUp else { return }
        courseUp = on
        renderQueue.async { renderer.setCourseUp(on) }
        setContentDirty()
    }

    /// Slews the chart back to north using the same machinery as leaving GPS mode.
    func returnToNorth() {
        guard let renderer else { return }
        renderQueue.async { renderer.requestNorthUp() }
        setContentDirty()
    }

    /// Screen angle of north in degrees clockwise from up. This is the chart's
    /// own rotation, not its negative: `rotationDegrees` turns the content
    /// clockwise and carries north (screen angle 0) round with it.
    var northScreenAngleDegrees: Double { viewport?.rotationDegrees ?? 0 }

    /// Whether the chart is rotated by more than half a degree. The camera
    /// already collapses anything under a tenth to zero; the wider threshold
    /// keeps the compass from flickering on rounding.
    var isChartTurned: Bool {
        guard let rotation = viewport?.rotationDegrees else { return false }
        return rotation > 0.5 && rotation < 359.5
    }

    /// On entry to GPS mode, widens the scale until both the ship and the
    /// route's start are visible. Runs once, not per frame. Skipped when there
    /// is no route or no fix yet.
    private func applyZoomOutRule() {
        guard let vp = viewport, vp.hasSurface,
              let ship = ownship?.coordinate,
              let start = route?.waypoints.first?.coordinate
        else { return }
        let widened = vp.widened(toShow: start, from: ship)
        guard !widened.isEquivalent(to: vp) else { return }
        viewport = widened
        setNeedsRender()
    }

    /// Shows a notice and clears it after a delay. A notice that is also a
    /// button stays up longer so it can be read and reached.
    private func post(_ notice: MapNotice) {
        self.notice = notice
        noticeTask?.cancel()
        let seconds = notice.opensSettings ? 8.0 : 4.0
        noticeTask = Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(seconds))
            guard !Task.isCancelled else { return }
            self?.notice = nil
        }
    }

    private func post(_ text: String) { post(MapNotice(text: text)) }

    /// The notice for a missing ownship. Denied/restricted is the only
    /// actionable state, so only that one opens Settings.
    private static func authorizationNotice(
        _ authorization: PPLocationAuthorization
    ) -> MapNotice {
        switch authorization {
        case .denied, .restricted:
            return MapNotice(
                text: "Location is off for \(AppName.display). "
                    + "Tap to open Settings.",
                opensSettings: true)
        default:
            return MapNotice(text: "Waiting for location…")
        }
    }

    private func receive(_ fix: PPFix) {
        guard let renderer else { return }
        // Every fix is forwarded: the heading resolver, trip computer and
        // recorder all need the full history. Only the frame is gated.
        renderQueue.async { renderer.push(fix) }
        if renderIsWorthIt(for: fix) { setContentDirty() }
        reconsiderLocationAccuracy(for: fix)
    }

    /// Where the ship would be drawn, and on what surface. A nil point means
    /// the question cannot be asked (no position or no surface); callers
    /// treat that as the expensive case.
    private func shipPosition(for fix: PPFix?) -> (point: CGPoint?, surface: CGSize) {
        guard let fix, fix.hasPosition, let vp = viewport, vp.hasSurface else {
            return (nil, .zero)
        }
        return (vp.point(forGeo: fix.coordinate), vp.sizeInPoints)
    }

    /// Asks `RenderGate` whether this fix deserves a frame. Projects the raw
    /// fix, which is correct outside GPS mode (the snapper only runs while
    /// following); inside GPS mode the gate answers true before looking.
    private func renderIsWorthIt(for fix: PPFix) -> Bool {
        let ship = shipPosition(for: fix)
        return renderGate.shouldRender(
            shipAt: ship.point,
            inSurface: ship.surface,
            symbolRadius: CGFloat(ownshipSymbolRadius),
            following: gpsMode,
            recording: isRecording)
    }

    // MARK: - Receiver accuracy policy

    /// Re-evaluates the accuracy policy and tells the receiver. Called on
    /// every fix and every camera move; costs one projection and no queue hop.
    /// No-op for the demo feed, which has no receiver.
    private func reconsiderLocationAccuracy(for fix: PPFix? = nil) {
        guard let source = locationSource else { return }
        let ship = shipPosition(for: fix ?? source.lastFix)
        let wanted = locationPolicy.accuracy(
            shipAt: ship.point,
            inSurface: ship.surface,
            following: gpsMode,
            recording: isRecording)
        source.accuracyMode = wanted == .coarse ? .coarse : .navigation
        // Compare before assigning: a @Published assigned its own value still
        // publishes, and this runs at gesture rate.
        if receiverIsCoarse != (wanted == .coarse) {
            receiverIsCoarse = wanted == .coarse
        }
    }

    /// Forces navigation accuracy immediately. Called before entering GPS
    /// mode or starting a recording, since GNSS warm-up can take tens of
    /// seconds and must not wait for the next fix.
    private func demandFullAccuracy() {
        locationPolicy.demandNavigation()
        locationSource?.accuracyMode = .navigation
        if receiverIsCoarse { receiverIsCoarse = false }
    }

    private func receive(_ authorization: PPLocationAuthorization) {
        locationAuthorization = authorization
        // Denied is not reported as a failure; the GPS button says so when pressed.
        if authorization != .authorized { feedIsRunning = false }
    }

    func stopFeed() {
        locationSource?.stop()
        locationSource = nil
        locationProxy = nil
        demoTimer?.invalidate()
        demoTimer = nil
        // The scripted source is polled by every render, so it must be
        // stopped on the C++ side too.
        if let renderer { renderQueue.async { renderer.stopDemoFeed() } }
        feedIsRunning = false
        feedRequested = false
    }

    private func startDemoTimer() {
        demoTimer?.invalidate()
        let timer = Timer(timeInterval: demoPollInterval, repeats: true) {
            [weak self] _ in
            MainActor.assumeIsolated { self?.setContentDirty() }
        }
        // `.common` so the timer keeps firing while a gesture is tracking.
        RunLoop.main.add(timer, forMode: .common)
        demoTimer = timer
    }

    // MARK: - Recording state

    /// Whether a ride is being written. The model's copy drives the UI and is
    /// set before the queue hop; `PPMap`'s copy drives the file.
    @Published private(set) var isRecording = false { didSet { updateIdleTimer() } }

    /// Points written so far, refreshed while the sheet is open.
    @Published private(set) var recordedPointCount = 0

    /// The rides on disk, newest first. Re-read when the sheet opens and
    /// after a recording stops.
    @Published private(set) var rides: [RecordedRide] = []

    /// The file being written, kept out of `rides` so the sheet does not list
    /// it twice. It joins the list when recording stops, or on the next
    /// launch if the app never got to stop.
    private var recordingURL: URL?

    // MARK: - Route

    /// Reads `Documents/current.fvrte` and replans it. Called once from the
    /// screen's `onAppear`. Runs on the render queue because the plan is not
    /// stored in the document.
    func loadSavedRoute() {
        guard let renderer, !routeLoaded else { return }
        routeLoaded = true
        renderQueue.async { [weak self] in
            let result = renderer.loadSavedRoute()
            Task { @MainActor in
                guard let self else { return }
                switch result {
                case .success(let route):
                    // An empty route is a first launch, not an error.
                    guard route.exists else { return }
                    self.route = route
                    self.setContentDirty()
                case .failure(let error):
                    self.status = error.localizedDescription
                }
            }
        }
    }

    /// Replaces the waypoints, replans, and writes the document. `isPlanning`
    /// covers the round trip.
    func setRoute(waypoints: [PPWaypoint], profile: String) {
        guard let renderer else { return }
        isPlanning = true
        renderQueue.async { [weak self] in
            let route = renderer.setRoute(waypoints: waypoints, profile: profile)
            Task { @MainActor in
                guard let self else { return }
                self.isPlanning = false
                self.route = route
                self.setContentDirty()
                self.offerWaypointHint()
            }
        }
    }

    /// Offers the drag hint on a route the rider just planned. Not on the one
    /// restored at launch: that arrives while the first frame and its notices
    /// are still landing, and it would go unread. A single stop has nothing
    /// worth dragging.
    private func offerWaypointHint() {
        guard let route, route.exists, route.waypoints.count >= 2,
              let hint = hints.dragWaypoint() else { return }
        post(hint)
    }

    func clearRoute() {
        guard let renderer else { return }
        renderQueue.async { [weak self] in
            let route = renderer.clearRoute()
            Task { @MainActor in
                guard let self else { return }
                self.route = route
                self.setContentDirty()
            }
        }
    }

    // MARK: - Dragging a route waypoint

    /// The waypoint label the finger has hold of, or nil. While set, the
    /// gesture view does not pan, zoom or rotate.
    @Published private(set) var draggingWaypoint: String?

    /// Latest drag position waiting for the render queue. Touch events arrive
    /// at up to 120 Hz and each hop also replans and draws, so only the
    /// newest position is kept.
    private var pendingDragPoint: CGPoint?
    private var dragHopInFlight = false

    /// Hit-tests and grabs the waypoint under `screenPoint` in one hop, and
    /// reports on the main thread whether one was found. The gesture needs
    /// the answer: a miss must let the map pan, a hit must not.
    func grabRouteWaypoint(at screenPoint: CGPoint,
                           then handle: @escaping (Bool) -> Void) {
        guard let renderer, let vp = viewport, route?.exists == true else {
            handle(false)
            return
        }
        let tolerance = renderer.routeWaypointHitTolerance
        renderQueue.async { [weak self] in
            var grabbed: String?
            if let label = renderer.routeWaypointLabel(near: screenPoint,
                                                       in: vp,
                                                       tolerance: tolerance),
               renderer.beginRouteWaypointDrag(label, at: screenPoint, in: vp) {
                grabbed = label
            }
            Task { @MainActor in
                guard let self else { return }
                self.draggingWaypoint = grabbed
                // Highlight halo; overlay-only pass.
                if grabbed != nil { self.setContentDirty() }
                handle(grabbed != nil)
            }
        }
    }

    /// Records the finger's position. Cheap to call at touch rate.
    func dragRouteWaypoint(to screenPoint: CGPoint) {
        guard draggingWaypoint != nil else { return }
        pendingDragPoint = screenPoint
        pumpWaypointDrag()
    }

    private func pumpWaypointDrag() {
        guard !dragHopInFlight, let point = pendingDragPoint,
              let renderer, let vp = viewport else { return }
        pendingDragPoint = nil
        dragHopInFlight = true
        renderQueue.async { [weak self] in
            renderer.dragRouteWaypoint(to: point, in: vp)
            Task { @MainActor in
                guard let self else { return }
                self.dragHopInFlight = false
                self.setContentDirty()
                self.pumpWaypointDrag()
            }
        }
    }

    /// Ends the drag: replan, write, and publish the committed route. Any
    /// pending move is dropped; `EndDrag` commits the release position itself.
    func dropRouteWaypoint(at screenPoint: CGPoint) {
        guard draggingWaypoint != nil else { return }
        hints.waypointWasDragged()
        draggingWaypoint = nil
        pendingDragPoint = nil
        guard let renderer, let vp = viewport else { return }
        isPlanning = true
        renderQueue.async { [weak self] in
            let route = renderer.endRouteWaypointDrag(at: screenPoint, in: vp)
            Task { @MainActor in
                guard let self else { return }
                self.isPlanning = false
                self.route = route
                self.setContentDirty()
            }
        }
    }

    /// Cancels the drag (phone call, system gesture, backgrounding). The
    /// waypoint returns to where it was and nothing is written.
    func cancelRouteWaypointDrag() {
        guard draggingWaypoint != nil else { return }
        draggingWaypoint = nil
        pendingDragPoint = nil
        guard let renderer else { return }
        renderQueue.async { [weak self] in
            let route = renderer.cancelRouteWaypointDrag()
            Task { @MainActor in
                guard let self else { return }
                self.route = route
                self.setContentDirty()
            }
        }
    }

    // MARK: - Points

    /// Reads the user's point set, seeding it from the pack on first launch.
    /// Called once from the screen's `onAppear`.
    func loadPoints() {
        guard let renderer, !pointsLoaded else { return }
        pointsLoaded = true
        renderQueue.async { [weak self] in
            let result = renderer.loadPoints()
            let snapshot = renderer.points()
            let palette = renderer.pointSymbols()
            let visible = renderer.pointsVisible()
            let seeded = renderer.pointsWereSeeded()
            Task { @MainActor in
                guard let self else { return }
                self.points = snapshot
                self.pointSymbols = palette
                self.pointsVisible = visible
                if case .failure(let error) = result {
                    self.post(error.localizedDescription)
                } else if seeded && !snapshot.isEmpty {
                    self.post("\(snapshot.count) pins came with the map — "
                              + "tap one, or hold the Pins button to add "
                              + "your own.")
                }
                self.setContentDirty()
            }
        }
    }

    /// The Pins button. Also the moment the add-a-pin hint is offered: the
    /// button the hint names is the one under the finger, and the hint is
    /// misleading with the pins hidden, since adding is refused there.
    func togglePoints() {
        setPointsVisible(!pointsVisible)
        if pointsVisible, let hint = hints.addPoint() { post(hint) }
    }

    func setPointsVisible(_ on: Bool) {
        guard let renderer, on != pointsVisible else { return }
        pointsVisible = on
        // Hiding the set closes any sheet open on one of them.
        if !on { selectedPoint = nil }
        renderQueue.async { [weak self] in
            renderer.setPointsVisible(on)
            Task { @MainActor in self?.setContentDirty() }
        }
    }

    /// Hit-tests a tap against the point overlay on the render queue. The
    /// viewport is passed rather than read on the other side so the answer is
    /// about the map the user saw when the finger landed.
    func point(under tap: CGPoint, then handle: @escaping (PPMapPoint?) -> Void) {
        guard let renderer, pointsVisible, let vp = viewport else {
            handle(nil)
            return
        }
        renderQueue.async { [weak self] in
            let hit = renderer.point(near: tap, in: vp)
            Task { @MainActor in
                guard self != nil else { return }
                handle(hit)
            }
        }
    }

    /// Highlights a point (or nothing) and opens/closes the sheet on it.
    func select(_ point: PPMapPoint?) {
        selectedPoint = point
        guard let renderer else { return }
        let id = point?.pointId ?? 0
        renderQueue.async { [weak self] in
            renderer.setSelectedPoint(id)
            Task { @MainActor in self?.setContentDirty() }
        }
    }

    /// The document's current version of a point, or nil if it has gone.
    func point(id: Int64) -> PPMapPoint? {
        points.first { $0.pointId == id }
    }

    /// The palette row a `symbolId` names, or nil (including for an id no
    /// longer in the table; the overlay then draws the bare shape).
    func symbol(id: Int64) -> PPPointSymbol? {
        id == 0 ? nil : pointSymbols.first { $0.symbolId == id }
    }

    /// Adds a point and writes the document. Selection moves to the stored
    /// point so the sheet holds the id the document assigned.
    func addPoint(_ point: PPMapPoint) {
        // The gesture has been found; its hint is finished. A place shared in
        // from another app goes through `acceptSharedPlace` and does not
        // count, because no one learned the hold from it.
        hints.pointWasAdded()
        mutatePoints { $0.add(point).pointId }
    }

    func updatePoint(_ point: PPMapPoint) {
        mutatePoints { $0.update(point) ? point.pointId : 0 }
    }

    func deletePoint(id: Int64) {
        mutatePoints {
            _ = $0.removePoint(id: id)
            return 0  // nothing is selected after a delete
        }
    }

    /// Common shape for the three edits: run `body` on the render queue, use
    /// its result as the id to select, re-read the whole set, and publish.
    ///
    /// The set is re-read rather than patched because the document is the
    /// authority (it assigns ids and may refuse an edit). `then` runs on the
    /// main actor after the publish, for callers that must act on the screen
    /// afterwards.
    private func mutatePoints(_ body: @escaping (Renderer) -> Int64,
                              then: (@MainActor (Int64) -> Void)? = nil) {
        guard let renderer else { return }
        renderQueue.async { [weak self] in
            let selectId = body(renderer)
            renderer.setSelectedPoint(selectId)
            let snapshot = renderer.points()
            let writeError = renderer.pointWriteError()
            Task { @MainActor in
                guard let self else { return }
                self.points = snapshot
                self.selectedPoint =
                    selectId == 0 ? nil
                                  : snapshot.first { $0.pointId == selectId }
                // A failed write is not a refused edit: the point is on
                // screen and correct, so this is a notice, not an alert.
                if !writeError.isEmpty {
                    self.post("Saved to the map but not to disk: \(writeError)")
                }
                self.setContentDirty()
                then?(selectId)
            }
        }
    }

    // MARK: - Shared places

    /// Adds a place shared in from another app as a point and reports the id
    /// it ended up on (0 on refusal). The point gets the default marker, the
    /// "Shared" category, and the address as remarks. No editor is shown; the
    /// info sheet opens on it instead.
    func acceptSharedPlace(_ place: SharedPlace,
                           then: @escaping @MainActor (Int64) -> Void) {
        PippinLog.share.notice(
            "app: accepting \(place.latitude, privacy: .public),\(place.longitude, privacy: .public) \(place.displayName, privacy: .public)")

        // A share can arrive on a cold launch before `onAppear` has loaded the
        // points. Adding to an unloaded overlay would write a one-point
        // document over the user's own. `loadPoints()` is idempotent and the
        // queue is serial, so calling it here orders the load ahead of the add.
        loadPoints()

        let coordinate = PPGeoPointMake(place.latitude, place.longitude)
        let name = place.displayName
        // Written on the render queue, read on the main actor after the
        // publish; the hop is the ordering.
        let alreadyHere = Box()

        mutatePoints({ renderer in
            let existing = renderer.points()
            // Sharing the same place twice must not stack two markers.
            // Fifteen metres is about the width of a building.
            if let near = existing.first(where: {
                Self.metres(from: $0.coordinate, to: coordinate) < 15
            }) {
                alreadyHere.value = true
                return near.pointId
            }
            let point = PPMapPoint(
                pointId: 0,
                name: name,
                coordinate: coordinate,
                shape: PPPointShapeCircle,
                // Match the size the set already uses; same rule as PointDraft.
                sizePx: existing.first?.sizePx ?? 22,
                colorHex: PointPalette.swatches[0].hex,
                symbolId: 0,
                category: "Shared",
                elevationFt: 0,
                remarks: place.address,
                phone: "",
                url: "")
            return renderer.add(point).pointId
        }, then: { [weak self] id in
            guard let self else { return }
            self.showSharedPlace(id: id, at: coordinate, name: name,
                                 wasAlreadyHere: alreadyHere.value)
            then(id)
        })
    }

    /// Posts the notice for a shared point and, when not following, centres
    /// the map on it.
    private func showSharedPlace(id: Int64, at coordinate: PPGeoPoint,
                                 name: String, wasAlreadyHere: Bool) {
        guard id != 0 else {
            PippinLog.share.error("app: the document refused the point")
            post("That place could not be added.")
            return
        }
        PippinLog.share.notice(
            "app: point \(id, privacy: .public) is on the overlay, \(wasAlreadyHere ? "already there" : "new", privacy: .public)")

        // Off the pack is not an error: the point is saved and will draw when
        // a pack that covers it is installed. Say so rather than centring on
        // nothing.
        if let bounds = renderer?.homeBounds, !Self.contains(bounds, coordinate) {
            post("\(name) saved, but it is outside the Kiawah map.")
            return
        }

        post(wasAlreadyHere ? "\(name) is already saved." : "Added \(name).")

        // In GPS mode the camera owns the centre; do not move the rider's view.
        guard !gpsMode, let current = viewport else { return }
        viewport = current.moved(toCenter: coordinate,
                                 scaleDenominator: current.scaleDenominator,
                                 rotationDegrees: current.rotationDegrees)
        setNeedsRender()
    }

    /// A mutable flag that crosses one queue hop in one direction. Not
    /// general-purpose; see its single use above.
    private final class Box: @unchecked Sendable {
        var value = false
    }

    /// Approximate metres between two coordinates. Equirectangular, which is
    /// exact enough over the tens of metres this is asked about.
    private static func metres(from a: PPGeoPoint, to b: PPGeoPoint) -> Double {
        let meanLatitude = (a.latitude + b.latitude) * 0.5 * .pi / 180
        let dLat = (b.latitude - a.latitude) * 111_320
        let dLon = (b.longitude - a.longitude) * 111_320 * cos(meanLatitude)
        return (dLat * dLat + dLon * dLon).squareRoot()
    }

    private static func contains(_ bounds: PPGeoBounds, _ point: PPGeoPoint) -> Bool {
        point.latitude >= bounds.southWest.latitude
            && point.latitude <= bounds.northEast.latitude
            && point.longitude >= bounds.southWest.longitude
            && point.longitude <= bounds.northEast.longitude
    }

    // MARK: - Recording

    func toggleRecording() {
        if isRecording { stopRecording() } else { startRecording() }
    }

    /// Starts writing `Documents/trips/ride-<date>.gpx`. Requires a feed but
    /// not GPS mode: following and recording are independent.
    func startRecording() {
        guard let renderer, !isRecording else { return }
        // Before anything else, so GNSS warm-up starts now.
        demandFullAccuracy()
        guard feedIsRunning || locationAuthorization == .authorized else {
            post(Self.authorizationNotice(locationAuthorization))
            return
        }
        guard let url = RideLibrary.newRideURL() else {
            post("Cannot make a file for the ride.")
            return
        }
        let name = RideLibrary.titleFormatter.string(from: Date())
        isRecording = true
        recordingURL = url
        recordedPointCount = 0
        renderQueue.async { [weak self] in
            let result = renderer.startRecording(to: url, name: name)
            Task { @MainActor in
                guard let self else { return }
                if case .failure(let error) = result {
                    // Roll back: the recorder never opened.
                    self.isRecording = false
                    self.recordingURL = nil
                    self.post("Could not start recording: \(error.localizedDescription)")
                } else {
                    // Every time, not once: the cost of not knowing is a hole
                    // in the ride. Auto-Lock is held off while recording, so
                    // the ways to lose fixes are the deliberate ones — and
                    // Low Power Mode, which overrides the idle timer. Delete
                    // this sentence when fixes survive the background.
                    self.post("Recording this ride. Locking the phone or "
                              + "switching apps stops it.")
                }
            }
        }
    }

    func stopRecording() {
        guard let renderer, isRecording else { return }
        isRecording = false
        renderQueue.async { [weak self] in
            let count = renderer.recordedPointCount()
            renderer.stopRecording()
            Task { @MainActor in
                guard let self else { return }
                self.recordedPointCount = count
                // Clear before listing, or the finished ride is filtered out.
                self.recordingURL = nil
                self.refreshRides()
                self.post(count == 0
                          ? "Nothing was recorded — no fixes arrived."
                          : "Ride saved, \(count) points. Hold the record "
                            + "button to share it.")
            }
        }
    }

    /// Fetches the recorder's count from the render queue. Called by the
    /// sheet while it is open, not per frame.
    func refreshRecordedCount() {
        guard let renderer, isRecording else { return }
        renderQueue.async { [weak self] in
            let count = renderer.recordedPointCount()
            Task { @MainActor in self?.recordedPointCount = count }
        }
    }

    /// Re-reads `Documents/trips/`. Called when a ride ends or the sheet opens.
    func refreshRides() {
        let current = recordingURL
        rides = RideLibrary.rides().filter { $0.url != current }
    }

    func deleteRide(_ ride: RecordedRide) {
        RideLibrary.delete(ride)
        refreshRides()
    }

    var pointHitTolerance: Double { renderer?.pointHitTolerance ?? 22.0 }

    /// The last valid fix, or nil.
    var currentLocation: PPGeoPoint? { ownship?.coordinate }

    /// How long a press is held before it grabs a waypoint. The gesture view
    /// reads this before the renderer may exist, hence the default.
    var routeWaypointHoldSeconds: Double {
        renderer?.routeWaypointHoldSeconds ?? 0.5
    }

    var routeProfileNames: [String] { renderer?.routeProfileNames ?? [] }
    var routeDefaultProfile: String { renderer?.routeDefaultProfile ?? "bicycle" }

    // MARK: - Symbol size

    /// The symbol sizes the menu offers, smallest first, from the pack.
    var symbolZoomSteps: [Double] { renderer?.symbolZoomSteps ?? [1.0] }

    /// Index into `symbolZoomSteps`. Stored as an index rather than a factor
    /// so a stored choice survives a pack that retunes its steps.
    @Published private(set) var symbolStep: Int = 0

    private static let symbolStepKey = "PPSymbolStep"

    /// Sets the symbol size step, persists it, and redraws.
    func setSymbolStep(_ step: Int) {
        let steps = symbolZoomSteps
        let clamped = max(0, min(step, steps.count - 1))
        guard clamped != symbolStep else { return }
        symbolStep = clamped
        UserDefaults.standard.set(clamped, forKey: Self.symbolStepKey)
        applySymbolZoom()
    }

    /// Hands the current step's factor to the map. `PPMap` drops its cached
    /// base map on the set.
    private func applySymbolZoom() {
        guard let renderer else { return }
        let zoom = symbolZoomSteps[min(symbolStep, symbolZoomSteps.count - 1)]
        renderQueue.async { [weak self] in
            renderer.setSymbolZoom(zoom)
            Task { @MainActor in self?.setContentDirty() }
        }
    }

    // MARK: - Crosshair pick

    /// What is under the crosshair, or nil when no pick is running or before
    /// the first answer. `isUsable == false` is an answer, not nil.
    @Published private(set) var pickPlace: PPPlace?

    /// What the pick would snap to, or nil when nothing is within tolerance.
    /// Answered in the same hop as `pickPlace` so the two never disagree.
    @Published private(set) var pickSnap: PPSnapTarget?

    /// The profile the running pick uses; nil when none is running.
    private var pickProfile: String?

    /// One place query at a time with the latest request kept, same shape as
    /// `renderInFlight` / `needsRender`. A drag writes the viewport at display
    /// rate and every write would otherwise queue a stale query.
    private var placeQueryInFlight = false
    private var placeQueryPending = false

    /// The profile a place is described with when nothing has said otherwise:
    /// the route's profile if there is a route, else the pack default.
    var namingProfile: String {
        guard let profile = route?.profile, !profile.isEmpty else {
            return routeDefaultProfile
        }
        return profile
    }

    /// Starts a crosshair pick with `profile`. Clears the C++ side's place
    /// memory so the last pick's road does not get a head start elsewhere.
    func beginPick(profile: String) {
        pickProfile = profile
        pickPlace = nil
        pickSnap = nil
        if let renderer {
            // Queued ahead of the query that follows; the queue is serial.
            renderQueue.async { renderer.forgetNamedPlace() }
        }
        refreshPickPlace()
    }

    func endPick() {
        pickProfile = nil
        pickPlace = nil
        pickSnap = nil
    }

    private func refreshPickPlace() {
        guard let renderer, let profile = pickProfile,
              let coordinate = crosshairCoordinate,
              let vp = viewport, vp.hasSurface else { return }
        if placeQueryInFlight {
            placeQueryPending = true
            return
        }
        placeQueryInFlight = true
        // The crosshair is drawn at the surface centre, so the snap is asked
        // about that same pixel rather than a re-projected coordinate.
        let size = vp.sizeInPoints
        let centre = CGPoint(x: (size.width - 1) / 2, y: (size.height - 1) / 2)
        let tolerance = renderer.snapTolerance
        renderQueue.async { [weak self] in
            let place = renderer.describePlace(at: coordinate, profile: profile,
                                               remembering: true)
            // Same hop as the place so the two answers land together.
            let snap = renderer.snapTarget(near: centre, in: vp,
                                           tolerance: tolerance)
            Task { @MainActor in
                guard let self else { return }
                self.placeQueryInFlight = false
                // The pick ended while this was in flight.
                guard self.pickProfile != nil else { return }
                self.pickPlace = place
                self.pickSnap = snap
                if self.placeQueryPending {
                    self.placeQueryPending = false
                    self.refreshPickPlace()
                }
            }
        }
    }

    /// Describes one fixed coordinate without touching the crosshair's place
    /// memory. Callers ask once per place, so a plain hop suffices.
    func describePlace(at coordinate: PPGeoPoint) async -> PPPlace? {
        guard let renderer else { return nil }
        let profile = namingProfile
        return await withCheckedContinuation { continuation in
            renderQueue.async {
                continuation.resume(returning: renderer.describePlace(
                    at: coordinate, profile: profile, remembering: false))
            }
        }
    }

    // MARK: - Search

    /// Searches the overlay stack on the render queue. The first search of a
    /// launch loads the road graph. The caller is a debounced text field, so
    /// no coalescing is needed here; cancellation of a query already inside
    /// the C++ is not wired up because queries over this pack take
    /// milliseconds. An empty query returns [] without a hop.
    func search(for text: String) async -> [PPSearchResult] {
        let query = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty, let renderer else { return [] }
        let vp = viewport
        return await withCheckedContinuation { continuation in
            renderQueue.async {
                continuation.resume(returning: renderer.search(for: query, in: vp))
            }
        }
    }

    /// Records a search term that produced a result the user accepted. Not
    /// done in `search(for:)`: cancelled prefix queries can still resume and
    /// would store a partial term.
    func rememberSearch(_ text: String) {
        let term = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !term.isEmpty else { return }
        lastSearchText = term
    }

    /// What the search box opens on: the last successful term, or the pack's
    /// `search.initial_text` until there has been one. A user default rather
    /// than a pack key because it is a fact about this install.
    var lastSearchText: String {
        get {
            let stored = UserDefaults.standard.string(forKey: Self.lastSearchKey)
            if let stored, !stored.isEmpty { return stored }
            return renderer?.searchInitialText ?? ""
        }
        set {
            UserDefaults.standard.set(newValue, forKey: Self.lastSearchKey)
        }
    }

    private static let lastSearchKey = "PPLastSearchText"

    /// Frames a search result: centred, zoomed to its extent, and biased up
    /// the screen so the sheet does not cover it. Ignored in GPS mode, where
    /// the camera owns the centre.
    func frame(_ result: PPSearchResult) {
        guard !gpsMode, let current = viewport, current.hasSurface,
              let renderer else { return }
        viewport = current.framing(result.bounds,
                                   minScaleDenominator: renderer.searchFrameMinScale,
                                   topBias: renderer.searchFrameTopBias)
        setNeedsRender()
    }

    /// The coordinate under the crosshair. The single definition used by both
    /// the label and the stored coordinate, so they cannot disagree. Asks
    /// `PPViewport` rather than approximating pixels-per-degree.
    var crosshairCoordinate: PPGeoPoint? {
        guard let vp = viewport, vp.hasSurface else { return nil }
        let size = vp.sizeInPoints
        return vp.geo(at: CGPoint(x: (size.width - 1) / 2,
                                  y: (size.height - 1) / 2))
    }

    /// The coordinate a pick actually takes: the snap target if there is one,
    /// else the crosshair. Every call site reads this and none re-derives it.
    var pickCoordinate: PPGeoPoint? {
        pickSnap?.coordinate ?? crosshairCoordinate
    }

    // MARK: - Render loop

    private func startDisplayLink() {
        let proxy = DisplayLinkProxy { [weak self] in
            // The link fires on the main thread; a hop here would put the
            // render a frame late.
            MainActor.assumeIsolated { self?.tick() }
        }
        let link = CADisplayLink(target: proxy, selector: #selector(DisplayLinkProxy.fire))
        // `.common` so the loop runs while a gesture is tracking.
        link.add(to: .main, forMode: .common)
        link.isPaused = true
        self.link = link
    }

    private func setNeedsRender() {
        needsRender = true
        link?.isPaused = false
    }

    /// Marks the picture stale without a camera move.
    private func setContentDirty() {
        contentDirty = true
        setNeedsRender()
    }

    /// Base-cache counters. Cumulative because a hit and a miss look identical
    /// on screen and the settle frame overwrites the last hit's numbers.
    private var baseHits = 0
    private var baseDraws = 0
    private var lastHitMs = 0.0

    /// Cost of the last base draw, kept apart from the frame time: a cached
    /// frame reports only its overlay pass.
    private var lastBaseDrawMs = 0.0

    /// Base maps drawn during the current gesture. The first is always
    /// allowed; see `tick()`.
    private var baseDrawsThisGesture = 0

    /// The last frame was served from the cached base under a non-identity
    /// transform, so one exact frame is owed when the map stops. See `settle`.
    private var baseIsResampled = false

    /// Guard-band margin for this frame. Zero when the scale changed since the
    /// last frame (no cached base can serve a zoom, so the band would be paid
    /// for and never used) and on the first frame (no cache yet).
    private func bandMargin(for vp: PPViewport) -> Double {
        guard let renderer, let last = frame else { return 0 }
        if last.viewport.scaleDenominator != vp.scaleDenominator { return 0 }
        return renderer.bandMargin
    }

    private func tick() {
        guard !renderInFlight else { return }
        guard needsRender, let renderer, let vp = viewport, vp.hasSurface else {
            // Nothing to draw: pause, unless a settle frame is owed.
            if !settle(renderer: renderer, viewport: viewport) {
                link?.isPaused = true
            }
            return
        }
        // During a gesture, skip the frame when the base would have to be
        // redrawn and the last base draw was over budget. The gesture's first
        // base draw is always allowed because it builds the band later frames
        // are served from; a frame the cache can serve is only an overlay
        // pass and always runs.
        let servedByCache = frame.map {
            $0.baseViewport.covers(vp, maxTurnDegrees: renderer.baseMaxTurnDegrees)
        } ?? false
        if gestureActive, !servedByCache, baseDrawsThisGesture > 0,
           lastBaseDrawMs > liveRenderBudgetMs {
            return
        }
        // Already showing this viewport and nothing else changed.
        if !contentDirty, let last = frame, vp.isEquivalent(to: last.viewport) {
            needsRender = false
            if !settle(renderer: renderer, viewport: vp) {
                link?.isPaused = true
            }
            return
        }

        needsRender = false
        contentDirty = false
        draw(vp, renderer: renderer, bandMargin: bandMargin(for: vp),
             reuseBase: true)
    }

    /// Draws one exact frame when the loop is about to pause with a resampled
    /// base on screen. The cache is refused and no band is requested, so the
    /// result is the same size as the screen at zero offset and `MapScreen`
    /// can display it unfiltered. Returns true when a settle was started.
    @discardableResult
    private func settle(renderer: Renderer?, viewport vp: PPViewport?) -> Bool {
        guard baseIsResampled, let renderer, let vp, vp.hasSurface,
              !gestureActive else { return false }
        needsRender = false
        contentDirty = false
        draw(vp, renderer: renderer, bandMargin: 0, reuseBase: false)
        return true
    }

    private func draw(_ vp: PPViewport, renderer: Renderer,
                      bandMargin: Double, reuseBase: Bool) {
        renderInFlight = true
        renderQueue.async { [weak self] in
            let result = renderer.render(vp, bandMargin: bandMargin,
                                         reuseBase: reuseBase)
            Task { @MainActor in
                guard let self else { return }
                self.renderInFlight = false
                switch result {
                case .success(let frame):
                    self.frame = frame
                    if frame.baseWasDrawn {
                        self.baseDraws += 1
                        self.lastBaseDrawMs = frame.baseMilliseconds
                        if self.gestureActive { self.baseDrawsThisGesture += 1 }
                    } else {
                        self.baseHits += 1
                        self.lastHitMs = frame.renderMilliseconds
                    }
                    // A drawn base is exact; a served one is exact only when
                    // drawn for this same camera and surface.
                    self.baseIsResampled =
                        !frame.baseWasDrawn
                        && !frame.baseViewport.isEquivalent(to: frame.viewport)
                    self.ownship = frame.ownship
                    self.trip = frame.trip
                    self.status = self.describe(frame)
                    self.applyCamera(from: frame)
                case .failure(let error):
                    self.failure = error.localizedDescription
                    // Do not retry a failed settle on every tick.
                    self.baseIsResampled = false
                }
                // The next tick pauses the loop if nothing is dirty.
                self.link?.isPaused = false
            }
        }
    }

    /// Applies the moving-map camera's answer from a frame. The scale is left
    /// alone (the camera has no opinion on it), and a live gesture wins
    /// outright: the next tick re-bases from wherever the finger left off.
    private func applyCamera(from frame: PPFrame) {
        if frame.hasCameraUpdate, !gestureActive, let vp = viewport {
            viewport = vp.moved(
                toCenter: frame.cameraCenter,
                scaleDenominator: vp.scaleDenominator,
                rotationDegrees: frame.cameraRotationDegrees)
        }
        // Keep the loop awake for the rest of a slew.
        if frame.cameraIsAnimating { setNeedsRender() }
    }

    // MARK: - Misc

    var styleName: String { renderer?.styleName ?? "" }
    var hasLabelFont: Bool { renderer?.hasLabelFont ?? false }

    /// `Documents/current.fvrte`. Nil only if Foundation cannot name the
    /// directory, in which case the route is simply not persisted.
    private static func routeDocumentURL() -> URL? {
        FileManager.default
            .urls(for: .documentDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("current.fvrte")
    }

    /// `Documents/points.fvpoints`. The bundle copy is read-only; the point
    /// store seeds this file from it on first launch.
    private static func pointsDocumentURL() -> URL? {
        FileManager.default
            .urls(for: .documentDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("points.fvpoints")
    }

    /// The `-PPShowStats` line. `12/68 ms` is a 12 ms overlay pass over a base
    /// that cost 68; `3/– ms` is an overlay pass over a cached base. Feature
    /// and draw counts belong to the base, so on a hit they are the cached
    /// frame's.
    private func describe(_ frame: PPFrame) -> String {
        let base = frame.baseWasDrawn
            ? String(format: "%.0f", frame.baseMilliseconds) : "–"
        return String(
            format: "z%ld · %lu features · %lu draws · %.0f/%@ ms · %d/%d cached @%.0f · 1:%@",
            frame.queryZoom, frame.featuresQueried, frame.drawsEmitted,
            frame.renderMilliseconds - frame.baseMilliseconds, base,
            baseHits, baseHits + baseDraws, lastHitMs,
            Self.scaleFormatter.string(from: NSNumber(value: frame.viewport.scaleDenominator))
                ?? "?")
    }

    private static let scaleFormatter: NumberFormatter = {
        let f = NumberFormatter()
        f.numberStyle = .decimal
        f.maximumFractionDigits = 0
        return f
    }()
}

/// Wraps `PPMap` with the promise that only the render queue touches it.
/// Launch-time constants are read once at construction on the main thread so
/// nothing later has to hop the queue for a value that cannot change.
private final class Renderer: @unchecked Sendable {
    private let map: PPMap
    let styleName: String
    let hasLabelFont: Bool
    let routeProfileNames: [String]
    let routeDefaultProfile: String
    let pointHitTolerance: Double
    /// The ownship symbol's reach in points; the render gate's margin.
    let ownshipSymbolRadius: Double
    /// The pack's `movingmap.course_up`.
    let initialCourseUp: Bool
    /// The pack's extent; shared places outside it are saved but not shown.
    let homeBounds: PPGeoBounds
    /// Base-cache guard band, from `display.base_cache_band_margin`.
    let bandMargin: Double
    /// Base-cache rotation tolerance; must match what `PPMap` uses.
    let baseMaxTurnDegrees: Double
    /// Search framing: the closest scale a result is framed at, and how far
    /// up the screen it is pushed.
    let searchFrameMinScale: Double
    let searchFrameTopBias: Double
    /// The pack's seed text for the search box.
    let searchInitialText: String
    /// The pack's symbol-size steps.
    let symbolZoomSteps: [Double]

    init(map: PPMap) {
        self.map = map
        // The rule file lives in a read-only bundle, so its profiles are
        // constants too.
        styleName = map.styleName
        hasLabelFont = map.hasLabelFont
        routeProfileNames = map.routeProfileNames
        routeDefaultProfile = map.routeDefaultProfile
        pointHitTolerance = map.pointHitTolerance
        ownshipSymbolRadius = map.ownshipSymbolRadiusInPoints
        initialCourseUp = map.isCourseUpEnabled
        homeBounds = map.homeBounds
        bandMargin = map.baseCacheBandMargin
        baseMaxTurnDegrees = map.baseCacheMaxTurnDegrees
        searchFrameMinScale = map.searchFrameMinScale
        searchFrameTopBias = map.searchFrameTopBias
        searchInitialText = map.searchInitialText
        symbolZoomSteps = map.symbolZoomSteps.map(\.doubleValue)
    }

    /// Render queue only. See `PPMap.h` for `bandMargin` and `reuseBase`.
    func render(_ viewport: PPViewport, bandMargin: Double,
                reuseBase: Bool) -> Result<PPFrame, Error> {
        do {
            return .success(try map.render(viewport, bandMargin: bandMargin,
                                           reuseBase: reuseBase))
        } catch {
            return .failure(error)
        }
    }

    /// Render queue only. Drops the cached base map; see `PPMap.symbolZoom`.
    func setSymbolZoom(_ zoom: Double) {
        map.symbolZoom = zoom
    }

    /// Render queue only. Drops the cached base map.
    func invalidateBaseLayer() {
        map.invalidateBaseLayer()
    }

    /// Render queue only.
    func push(_ fix: PPFix) {
        map.push(fix)
    }

    /// Render queue only.
    func setGpsMode(_ on: Bool) {
        map.isGpsModeEnabled = on
    }

    /// Render queue only. An ObjC BOOL property with an `is…` getter imports
    /// under that name for both reads and writes.
    func setCourseUp(_ on: Bool) {
        map.isCourseUpEnabled = on
    }

    func requestNorthUp() {
        map.requestNorthUp()
    }

    /// Render queue only.
    func stopDemoFeed() {
        map.stopDemoFeed()
    }

    /// Render queue only.
    func startDemoFeed() -> Result<Void, Error> {
        do {
            try map.startDemoFeed()
            return .success(())
        } catch {
            return .failure(error)
        }
    }

    /// Render queue only. May load the road graph on first call.
    func describePlace(at coordinate: PPGeoPoint, profile: String,
                       remembering: Bool) -> PPPlace {
        map.describePlace(at: coordinate, profile: profile,
                          remembering: remembering)
    }

    func forgetNamedPlace() {
        map.forgetNamedPlace()
    }

    /// Render queue only.
    func snapTarget(near screenPoint: CGPoint, in viewport: PPViewport,
                    tolerance: Double) -> PPSnapTarget? {
        map.snapTarget(near: screenPoint, in: viewport, tolerance: tolerance)
    }

    var snapTolerance: Double { map.snapTolerance }

    /// Render queue only. The first call builds the search overlays and reads
    /// the road file.
    func search(for text: String, in viewport: PPViewport?) -> [PPSearchResult] {
        map.search(for: text, in: viewport)
    }

    // MARK: Route (render queue only)

    /// A first launch is a route whose `exists` is false, not nil or an error.
    func loadSavedRoute() -> Result<PPRoute, Error> {
        do {
            return .success(try map.loadSavedRoute())
        } catch {
            return .failure(error)
        }
    }

    func setRoute(waypoints: [PPWaypoint], profile: String) -> PPRoute {
        map.setRoute(waypoints: waypoints, profile: profile)
    }

    func clearRoute() -> PPRoute {
        map.clearRoute()
    }

    // MARK: Waypoint drag (render queue only)

    func routeWaypointLabel(near screenPoint: CGPoint, in viewport: PPViewport,
                            tolerance: Double) -> String? {
        map.routeWaypointLabel(near: screenPoint, in: viewport,
                               tolerance: tolerance)
    }

    var routeWaypointHitTolerance: Double { map.routeWaypointHitTolerance }
    var routeWaypointHoldSeconds: Double { map.routeWaypointHoldSeconds }

    func beginRouteWaypointDrag(_ label: String, at screenPoint: CGPoint,
                                in viewport: PPViewport) -> Bool {
        map.beginRouteWaypointDrag(label, at: screenPoint, in: viewport)
    }

    @discardableResult
    func dragRouteWaypoint(to screenPoint: CGPoint,
                           in viewport: PPViewport) -> Bool {
        map.dragRouteWaypoint(to: screenPoint, in: viewport)
    }

    func endRouteWaypointDrag(at screenPoint: CGPoint,
                              in viewport: PPViewport) -> PPRoute {
        map.endRouteWaypointDrag(at: screenPoint, in: viewport)
    }

    func cancelRouteWaypointDrag() -> PPRoute {
        map.cancelRouteWaypointDrag()
    }

    // MARK: Points (render queue only)

    func loadPoints() -> Result<Void, Error> {
        do {
            try map.loadPoints()
            return .success(())
        } catch {
            return .failure(error)
        }
    }

    func points() -> [PPMapPoint] { map.points }
    func pointSymbols() -> [PPPointSymbol] { map.pointSymbols }
    func pointsVisible() -> Bool { map.arePointsVisible }
    // `arePointsVisible` imports under that name for reads and writes.
    func setPointsVisible(_ on: Bool) { map.arePointsVisible = on }
    func pointsWereSeeded() -> Bool { map.pointsWereSeeded }
    func setSelectedPoint(_ id: Int64) { map.selectedPointId = id }
    func pointWriteError() -> String { map.pointWriteError }

    /// Hit-tests the point overlay with the pack's tolerance.
    func point(near tap: CGPoint, in viewport: PPViewport) -> PPMapPoint? {
        map.point(near: tap, in: viewport, tolerance: map.pointHitTolerance)
    }

    /// Render queue only.
    func startRecording(to url: URL, name: String) -> Result<Void, Error> {
        do {
            try map.startRecording(to: url, name: name)
            return .success(())
        } catch {
            return .failure(error)
        }
    }

    func stopRecording() { map.stopRecording() }
    func recordedPointCount() -> Int { Int(map.recordedPointCount) }

    func add(_ point: PPMapPoint) -> PPMapPoint { map.add(point) }
    func update(_ point: PPMapPoint) -> Bool { map.update(point) }
    func removePoint(id: Int64) -> Bool { map.removePoint(id: id) }
}

// MARK: - Notices

/// A transient message for the user, and whether tapping it opens Settings.
/// A flag rather than a closure: only one notice is actionable, and a flag
/// keeps the banner non-hit-testable in every other case.
struct MapNotice: Equatable {
    let text: String
    /// Tapping the notice opens this app's page in Settings.
    var opensSettings = false
}

// MARK: - Gesture hints

/// The hints for the two gestures with nothing on screen to suggest them:
/// holding the Pins button to add a pin, and holding a route stop to drag it.
///
/// Each is offered at most once per launch until the rider performs it, and
/// never after. Once ever is missed on a phone that is opened for two minutes
/// at a time; every time is nagging.
@MainActor
struct MapHints {
    private static let addedPointKey = "PPHasAddedPoint"
    private static let draggedWaypointKey = "PPHasDraggedWaypoint"

    private var addShownThisLaunch = false
    private var dragShownThisLaunch = false

    /// The add-a-place hint, or nil when it is not due.
    mutating func addPoint() -> String? {
        guard !addShownThisLaunch,
              !UserDefaults.standard.bool(forKey: Self.addedPointKey)
        else { return nil }
        addShownThisLaunch = true
        return "Hold the Pins button to add a pin"
    }

    /// The drag-a-stop hint, or nil when it is not due.
    mutating func dragWaypoint() -> String? {
        guard !dragShownThisLaunch,
              !UserDefaults.standard.bool(forKey: Self.draggedWaypointKey)
        else { return nil }
        dragShownThisLaunch = true
        return "Hold a route stop, then drag it to move the route."
    }

    /// Retires the add-a-place hint for good.
    func pointWasAdded() {
        UserDefaults.standard.set(true, forKey: Self.addedPointKey)
    }

    /// Retires the drag-a-stop hint for good.
    func waypointWasDragged() {
        UserDefaults.standard.set(true, forKey: Self.draggedWaypointKey)
    }
}

// MARK: - Receiver delegate

/// Adapts `PPLocationSourceDelegate` (which requires an `NSObject`) to
/// closures, so `MapModel` can stay a plain Swift type. All callbacks arrive
/// on the main thread because `CLLocationManager` delivers to the run loop it
/// was created on.
private final class LocationProxy: NSObject, PPLocationSourceDelegate {
    private let onFix: @MainActor (PPFix) -> Void
    private let onAuthorization: @MainActor (PPLocationAuthorization) -> Void
    private let onFailure: @MainActor (Error) -> Void

    init(onFix: @escaping @MainActor (PPFix) -> Void,
         onAuthorization: @escaping @MainActor (PPLocationAuthorization) -> Void,
         onFailure: @escaping @MainActor (Error) -> Void) {
        self.onFix = onFix
        self.onAuthorization = onAuthorization
        self.onFailure = onFailure
    }

    func locationSource(_ source: PPLocationSource, didProduce fix: PPFix) {
        MainActor.assumeIsolated { onFix(fix) }
    }

    func locationSource(_ source: PPLocationSource,
                        didChangeAuthorization authorization: PPLocationAuthorization) {
        MainActor.assumeIsolated { onAuthorization(authorization) }
    }

    func locationSource(_ source: PPLocationSource, didFailWithError error: Error) {
        MainActor.assumeIsolated { onFailure(error) }
    }
}

/// `CADisplayLink` retains its target, so the target is this proxy rather
/// than the model.
private final class DisplayLinkProxy: NSObject {
    private let body: () -> Void
    init(body: @escaping () -> Void) { self.body = body }
    @objc func fire() { body() }
}
