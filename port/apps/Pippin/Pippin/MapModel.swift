// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Combine
import CoreLocation
import PippinKit
import SwiftUI
import UIKit

/// The state that changes on every frame or camera step: the camera, the
/// frame, and the readouts drawn from it. Written only by `MapModel`.
///
/// Kept off `MapModel`'s own publisher because a presented `Menu` or sheet is
/// rebuilt whenever the view holding it is invalidated. Were these on
/// `MapModel`, every GPS step would re-run `MapScreen.body` and replace the
/// rows under a finger mid-tap. Views that draw this state read it through
/// `LiveMapReader`.
@MainActor
final class LiveMapState: ObservableObject {
    @Published fileprivate(set) var viewport: PPViewport?
    @Published fileprivate(set) var frame: PPFrame?
    @Published fileprivate(set) var underlay: PPUnderlay?
    @Published fileprivate(set) var mapBackground: CGColor?
    @Published fileprivate(set) var status: String = ""
    @Published fileprivate(set) var ownship: PPOwnship?
    @Published fileprivate(set) var trip: PPTrip?
    @Published fileprivate(set) var guidance: PPGuidance?
}

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
/// The base map is drawn into a guard band larger than the screen and kept at
/// rest, so a drag starts covered. During a pan or turn the band is redrawn
/// early, ahead of the finger, when the screen predicted one draw ahead nears
/// its edge; during a pinch a base draw over `liveRenderBudgetMs` hands the
/// gesture to the preview transform in `MapScreen` until settle.
@MainActor
final class MapModel: ObservableObject {
    /// The per-frame state, published separately so a frame redraws only the
    /// views that read it. See `LiveMapState`.
    let live = LiveMapState()

    /// The camera. Every writer (pan, pinch, twist, resize, moving-map answer)
    /// goes through this property, so the crosshair label and the location
    /// accuracy policy are refreshed from one setter rather than at each site.
    private(set) var viewport: PPViewport? {
        get { live.viewport }
        set {
            live.viewport = newValue
            if pickProfile != nil { refreshPickPlace() }
            reconsiderLocationAccuracy()
        }
    }
    /// The last finished frame, and the viewport it was drawn at.
    private(set) var frame: PPFrame? {
        get { live.frame }
        set { live.frame = newValue }
    }

    /// The map two zoom levels out, shown under the base so the screen past
    /// the guard band is soft rather than blank. Built when the loop is idle;
    /// see `pauseOrBuildUnderlay`.
    private(set) var underlay: PPUnderlay? {
        get { live.underlay }
        set { live.underlay = newValue }
    }

    /// The style's background colour, painted behind every layer. Kept when
    /// the underlay is dropped: a colour costs nothing to hold.
    private(set) var mapBackground: CGColor? {
        get { live.mapBackground }
        set { live.mapBackground = newValue }
    }
    private(set) var status: String {
        get { live.status }
        set { live.status = newValue }
    }

    #if DEBUG
    /// `-PPShowStats YES`. Read once at launch, and consulted before the
    /// per-frame readout is formatted at all: nothing draws it otherwise.
    static let showsStats = UserDefaults.standard.bool(forKey: "PPShowStats")
    #endif
    @Published private(set) var failure: String?

    /// Ownship position as of the last drawn frame (the render queue's answer).
    private(set) var ownship: PPOwnship? {
        get { live.ownship }
        set { live.ownship = newValue }
    }

    /// Trip computer readout as of the last frame, or nil outside GPS mode.
    /// Arrives on the frame because the trip computer is fed on the render queue.
    private(set) var trip: PPTrip? {
        get { live.trip }
        set { live.trip = newValue }
    }

    /// The next turn, or nil when there is no banner to draw: outside GPS
    /// mode, with no planned route, or past the destination. Arrives on the
    /// frame for the trip's reason — the state machine is fed on the render
    /// queue, where both feeds have already become one stream.
    private(set) var guidance: PPGuidance? {
        get { live.guidance }
        set { live.guidance = newValue }
    }

    /// What the guidance says, out loud and in the hand (GD4). Not published:
    /// the alerts are an effect of a frame arriving, not state a view draws.
    private let alerts = TurnAlerts()

    /// The route on the map, or nil until one has been loaded or set. A
    /// snapshot taken on the render queue; see `PPRoute.h`.
    @Published private(set) var route: PPRoute?
    /// A plan is in flight; bound to the sheet's spinner.
    @Published private(set) var isPlanning = false
    @Published private(set) var feedIsRunning = false

    /// Whether the receiver is currently in its coarse (battery-saving)
    /// tier. Shown on the `-PPShowStats` line; a tier change has no visible
    /// effect otherwise.
    var receiverIsCoarse: Bool { locationPolicy.current == .coarse }
    @Published private(set) var locationAuthorization: PPLocationAuthorization =
        .notDetermined

    /// GPS mode: the map follows the ship and the road snapper is on. Whether
    /// the chart also turns with the rider is `courseUp`.
    @Published private(set) var gpsMode = false { didSet { ridingDidChange() } }

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

    /// A base draw slower than this stops live rendering during a pinch and
    /// leaves the preview transform to carry it until settle. 40 ms is two
    /// frames at 60 Hz. Pans and turns are governed by the band instead.
    private let liveRenderBudgetMs = 40.0

    private let renderer: Renderer?

    /// The pack's tide table, or nil when it carries none.
    var tide: PPTide? { renderer?.tide }

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

    /// The display link's rate by mode and the follow-frame threshold.
    private lazy var framePolicy = FramePolicy(
        followFPS: renderer?.followFramesPerSecond ?? FramePolicy.defaultFollowFPS,
        minMovePoints: renderer?.followMinMovePoints ?? FramePolicy.defaultMinMovePoints)

    /// The rate last handed to the display link, so it is set on a change only.
    private var appliedFrameRate: FramePolicy.Rate?

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
            alerts.amplitude = map.alertAmplitude
            WindFeed.shared.configure(map.wind)
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
        setSymbolStep(UserDefaults.standard.integer(forKey: Self.symbolStepKey),
                      persist: false)
        startDisplayLink()
        // The cached base map is the only large allocation not currently
        // needed; dropping it costs one render.
        memoryWarning = NotificationCenter.default.addObserver(
            forName: UIApplication.didReceiveMemoryWarningNotification,
            object: nil, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated {
                    guard let self, let renderer = self.renderer else { return }
                    self.dropUnderlay()
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
        coast = nil
        gestureActive = true
        baseDrawsThisGesture = 0
        panVelocity = .zero
        turningThisGesture = false
        applyFrameRate()
    }

    /// The last finger lifted. A release faster than the pack's
    /// `fling_min_speed_pt` starts a coast, and the gesture lasts until it
    /// ends; there is no fling while following, where the next fix would pull
    /// the map straight back.
    func gestureEnded(releaseVelocity: CGVector? = nil) {
        if let v = releaseVelocity, !gpsMode, let renderer,
           let momentum = Momentum(velocity: v,
                                   decelerationRate: renderer.flingDecelerationRate,
                                   minSpeed: renderer.flingMinSpeedPoints) {
            coast = Coast(momentum, start: CACurrentMediaTime())
            turningThisGesture = false
            setNeedsRender()
            return
        }
        endGesture()
    }

    /// True while a fling is coasting.
    var isCoasting: Bool { coast != nil }

    /// Stops a coast where it is. The touch that called this is spent on
    /// stopping; see `MapGestureView.CoastStopRecognizer`.
    func stopCoast() {
        guard coast != nil else { return }
        coast = nil
        endGesture()
    }

    private func endGesture() {
        gestureActive = false
        panVelocity = .zero
        turningThisGesture = false
        applyFrameRate()
        // Settle frame: replace whatever the preview transform was showing.
        setNeedsRender()
    }

    /// Moves a running coast to the display link's time. Each step is a pan,
    /// so it is rotation-aware and leads the band like a drag. Stops when the
    /// speed has decayed or the centre clamp at the pack's edge bites.
    private func advanceCoast() {
        guard var running = coast else { return }
        guard !gpsMode, let before = viewport else { stopCoast(); return }
        let step = running.step(at: CACurrentMediaTime())
        coast = running
        pan(by: CGSize(width: step.delta.dx, height: step.delta.dy),
            velocity: step.velocity)
        if let after = viewport {
            let was = after.point(forGeo: before.center)
            let now = after.point(forGeo: after.center)
            let applied = CGVector(dx: was.x - now.x, dy: was.y - now.y)
            if Momentum.clampBit(requested: step.delta, applied: applied) {
                stopCoast()
                return
            }
        }
        if step.finished { stopCoast() }
    }

    /// Pans the content by `translation` in points. `velocity` is the finger's,
    /// in points per second; the band is drawn ahead of it.
    func pan(by translation: CGSize, velocity: CGVector = .zero) {
        panVelocity = velocity
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
        if gestureActive { turningThisGesture = true }
        setNeedsRender()
    }

    // MARK: - Position feed

    /// Starts the position feed: the recorded demo ride when `-PPDemoFeed YES`
    /// is passed to a DEBUG build, otherwise the phone's receiver. Starting
    /// the feed does not make the map follow it; see `setGpsMode`.
    func startFeed() {
        guard renderer != nil, !feedRequested else { return }
        feedRequested = true
        // Replay is DEBUG only. Release packs do not carry the recorded ride
        // (`stage_data.py --release`).
        #if DEBUG
        if UserDefaults.standard.bool(forKey: "PPDemoFeed") {
            onRenderQueue({ $0.startDemoFeed() }) { model, result in
                switch result {
                case .success:
                    model.feedIsRunning = true
                    model.startDemoTimer()
                    model.setContentDirty()
                case .failure(let error):
                    model.feedRequested = false
                    model.failure = error.localizedDescription
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
            stopCoast()
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

    // MARK: - Riding state

    /// Following or recording started or stopped.
    private func ridingDidChange() {
        updateIdleTimer()
        applyFrameRate()
        #if DEBUG
        energy.update(following: gpsMode, recording: isRecording)
        #endif
    }

    #if DEBUG
    /// The per-ride energy log, Debug builds only. Frames are the render
    /// loop's own counters; see `EnergyRecorder`.
    private lazy var energy = EnergyRecorder { [weak self] in
        guard let self else { return EnergyCounters() }
        return EnergyCounters(frames: self.baseDraws + self.baseHits,
                              baseDraws: self.baseDraws,
                              cacheHits: self.baseHits,
                              fixes: self.fixCount)
    }
    #endif

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
        fixCount += 1
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
    }

    /// Forces navigation accuracy immediately. Called before entering GPS
    /// mode or starting a recording, since GNSS warm-up can take tens of
    /// seconds and must not wait for the next fix.
    private func demandFullAccuracy() {
        locationPolicy.demandNavigation()
        locationSource?.accuracyMode = .navigation
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
    @Published private(set) var isRecording = false { didSet { ridingDidChange() } }

    /// Points written so far, refreshed while the sheet is open.
    @Published private(set) var recordedPointCount = 0

    /// The rides on disk, newest first. Re-read when the sheet opens and
    /// after a recording stops.
    @Published private(set) var rides: [RecordedRide] = []

    /// The file being written, kept out of `rides` so the sheet does not list
    /// it twice. It joins the list when recording stops, or on the next
    /// launch if the app never got to stop.
    private var recordingURL: URL?

    // MARK: - Render-queue round trips

    /// Runs `work` on the render queue and hands what it returns to `then` on
    /// the main actor. The hop, the weak capture and the guard are the same
    /// at every call site, so only the two closures are written out. `then`
    /// takes the model rather than capturing it, which is what keeps the
    /// escaping closure from holding it alive.
    private func onRenderQueue<T>(
        _ work: @escaping (Renderer) -> T,
        then: @escaping @MainActor (MapModel, T) -> Void
    ) {
        guard let renderer else { return }
        renderQueue.async { [weak self] in
            let result = work(renderer)
            Task { @MainActor in
                guard let self else { return }
                then(self, result)
            }
        }
    }

    /// The shape of the four route mutators: one renderer call returning the
    /// new route, published with a redraw. `planning` brackets the round trip
    /// with the sheet's spinner; `then` is for what a particular mutator does
    /// once the route is on screen.
    private func publishRoute(planning: Bool = false,
                              _ body: @escaping (Renderer) -> PPRoute,
                              then: (@MainActor (MapModel) -> Void)? = nil) {
        guard renderer != nil else { return }
        if planning { isPlanning = true }
        onRenderQueue(body) { model, route in
            if planning { model.isPlanning = false }
            model.route = route
            model.setContentDirty()
            then?(model)
        }
    }

    // MARK: - Route

    /// Reads `Documents/current.fvrte` and replans it. Called once from the
    /// screen's `onAppear`. Runs on the render queue because the plan is not
    /// stored in the document.
    func loadSavedRoute() {
        guard renderer != nil, !routeLoaded else { return }
        routeLoaded = true
        onRenderQueue({ $0.loadSavedRoute() }) { model, result in
            switch result {
            case .success(let route):
                // An empty route is a first launch, not an error.
                guard route.exists else { return }
                model.route = route
                model.setContentDirty()
            case .failure(let error):
                model.status = error.localizedDescription
            }
        }
    }

    /// Replaces the waypoints, replans, and writes the document. `isPlanning`
    /// covers the round trip.
    func setRoute(waypoints: [PPWaypoint], profile: String) {
        publishRoute(planning: true,
                     { $0.setRoute(waypoints: waypoints, profile: profile) },
                     then: { $0.offerWaypointHint() })
    }

    /// As above, also setting the route's beach use.
    func setRoute(waypoints: [PPWaypoint], profile: String, beachUse: PPBeachUse) {
        publishRoute(planning: true,
                     { $0.setRoute(waypoints: waypoints, profile: profile,
                                   beachUse: beachUse) },
                     then: { $0.offerWaypointHint() })
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
        publishRoute { $0.clearRoute() }
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
        onRenderQueue({ renderer -> String? in
            guard let label = renderer.routeWaypointLabel(near: screenPoint,
                                                          in: vp,
                                                          tolerance: tolerance),
                  renderer.beginRouteWaypointDrag(label, at: screenPoint, in: vp)
            else { return nil }
            return label
        }) { model, grabbed in
            model.draggingWaypoint = grabbed
            // Highlight halo; overlay-only pass.
            if grabbed != nil { model.setContentDirty() }
            handle(grabbed != nil)
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
              renderer != nil, let vp = viewport else { return }
        pendingDragPoint = nil
        dragHopInFlight = true
        onRenderQueue({ $0.dragRouteWaypoint(to: point, in: vp) }) { model, _ in
            model.dragHopInFlight = false
            model.setContentDirty()
            model.pumpWaypointDrag()
        }
    }

    /// Ends the drag: replan, write, and publish the committed route. Any
    /// pending move is dropped; `EndDrag` commits the release position itself.
    func dropRouteWaypoint(at screenPoint: CGPoint) {
        guard draggingWaypoint != nil else { return }
        hints.waypointWasDragged()
        draggingWaypoint = nil
        pendingDragPoint = nil
        guard let vp = viewport else { return }
        publishRoute(planning: true) {
            $0.endRouteWaypointDrag(at: screenPoint, in: vp)
        }
    }

    /// Cancels the drag (phone call, system gesture, backgrounding). The
    /// waypoint returns to where it was and nothing is written.
    func cancelRouteWaypointDrag() {
        guard draggingWaypoint != nil else { return }
        draggingWaypoint = nil
        pendingDragPoint = nil
        publishRoute { $0.cancelRouteWaypointDrag() }
    }

    // MARK: - Points

    /// Reads the user's point set, seeding it from the pack on first launch.
    /// Called once from the screen's `onAppear`.
    func loadPoints() {
        guard renderer != nil, !pointsLoaded else { return }
        pointsLoaded = true
        onRenderQueue({ renderer in
            (result: renderer.loadPoints(),
             snapshot: renderer.points(),
             palette: renderer.pointSymbols(),
             visible: renderer.pointsVisible(),
             seeded: renderer.pointsWereSeeded())
        }) { model, loaded in
            model.points = loaded.snapshot
            model.pointSymbols = loaded.palette
            model.pointsVisible = loaded.visible
            if case .failure(let error) = loaded.result {
                model.post(error.localizedDescription)
            } else if loaded.seeded && !loaded.snapshot.isEmpty {
                model.post("\(loaded.snapshot.count) pins came with the map — "
                           + "tap one, or hold the Pins button to add "
                           + "your own.")
            }
            model.setContentDirty()
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
        guard renderer != nil, on != pointsVisible else { return }
        pointsVisible = on
        // Hiding the set closes any sheet open on one of them.
        if !on { selectedPoint = nil }
        onRenderQueue({ $0.setPointsVisible(on) }) { model, _ in
            model.setContentDirty()
        }
    }

    /// Hit-tests a tap against the point overlay on the render queue. The
    /// viewport is passed rather than read on the other side so the answer is
    /// about the map the user saw when the finger landed.
    func point(under tap: CGPoint, then handle: @escaping (PPMapPoint?) -> Void) {
        guard renderer != nil, pointsVisible, let vp = viewport else {
            handle(nil)
            return
        }
        onRenderQueue({ $0.point(near: tap, in: vp) }) { _, hit in handle(hit) }
    }

    /// Highlights a point (or nothing) and opens/closes the sheet on it.
    func select(_ point: PPMapPoint?) {
        selectedPoint = point
        let id = point?.pointId ?? 0
        onRenderQueue({ $0.setSelectedPoint(id) }) { model, _ in
            model.setContentDirty()
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

    /// Common shape for the edits: run `body` on the render queue, use the id
    /// it returns as the id to select, re-read the whole set, and publish.
    ///
    /// The set is re-read rather than patched because the document is the
    /// authority (it assigns ids and may refuse an edit). `then` runs on the
    /// main actor after the publish, for callers that must act on the screen
    /// afterwards; `body`'s second return value crosses the hop with it, so
    /// what the render queue learned does not need a shared mutable box.
    private func mutatePoints<T>(_ body: @escaping (Renderer) -> (Int64, T),
                                 then: (@MainActor (Int64, T) -> Void)? = nil) {
        onRenderQueue({ renderer in
            let (selectId, payload) = body(renderer)
            renderer.setSelectedPoint(selectId)
            return (selectId, payload, renderer.points(),
                    renderer.pointWriteError())
        }) { model, edit in
            let (selectId, payload, snapshot, writeError) = edit
            model.points = snapshot
            model.selectedPoint =
                selectId == 0 ? nil
                              : snapshot.first { $0.pointId == selectId }
            // A failed write is not a refused edit: the point is on screen
            // and correct, so this is a notice, not an alert.
            if !writeError.isEmpty {
                model.post("Saved to the map but not to disk: \(writeError)")
            }
            model.setContentDirty()
            then?(selectId, payload)
        }
    }

    /// The common case: the edit has nothing to report beyond the id.
    private func mutatePoints(_ body: @escaping (Renderer) -> Int64,
                              then: (@MainActor (Int64) -> Void)? = nil) {
        mutatePoints({ (body($0), ()) },
                     then: then.map { done in { id, _ in done(id) } })
    }

    // MARK: - Shared places

    /// The palette icon a shared place wears: the teardrop pin.
    private static let sharedPlaceSymbol = "marker"

    /// Adds a place shared in from another app as a point and reports the id
    /// it ended up on (0 on refusal). The point gets a yellow pin badge, the
    /// "Shared" category, and the address as remarks (with a warning line
    /// when the position is a search result). No editor is shown; the
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
        let remarks = Self.sharedPlaceRemarks(place)

        mutatePoints({ renderer -> (Int64, Bool) in
            let existing = renderer.points()
            // Sharing the same place twice must not stack two markers.
            // Fifteen metres is about the width of a building.
            if let near = existing.first(where: {
                Self.metres(from: $0.coordinate, to: coordinate) < 15
            }) {
                return (near.pointId, true)
            }
            // Looked up by name: ids differ between documents, and a
            // document without the icon falls back to a plain yellow circle.
            let pin = renderer.pointSymbols().first { $0.name == Self.sharedPlaceSymbol }
            let point = PPMapPoint(
                pointId: 0,
                name: name,
                coordinate: coordinate,
                shape: PPPointShapeCircle,
                // Match the size the set already uses; same rule as PointDraft.
                sizePx: existing.first?.sizePx ?? 22,
                colorHex: PointPalette.yellowHex,
                symbolId: pin?.symbolId ?? 0,
                category: "Shared",
                elevationFt: 0,
                remarks: remarks,
                phone: "",
                url: "")
            return (renderer.add(point).pointId, false)
        }, then: { [weak self] id, wasAlreadyHere in
            guard let self else { return }
            self.showSharedPlace(id: id, at: coordinate, name: name,
                                 wasAlreadyHere: wasAlreadyHere)
            then(id)
        })
    }

    /// The remarks for a shared point: the address, preceded by a warning
    /// line when the position came from a search rather than the link.
    private static func sharedPlaceRemarks(_ place: SharedPlace) -> String {
        guard place.isApproximate else { return place.address }
        let warning = "Position from Apple's search for '\(place.searchQuery)' — check it"
        return place.address.isEmpty ? warning : warning + "\n" + place.address
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
        stopCoast()
        viewport = current.moved(toCenter: coordinate,
                                 scaleDenominator: current.scaleDenominator,
                                 rotationDegrees: current.rotationDegrees)
        setNeedsRender()
    }

    /// Metres between two coordinates, on the WGS84 ellipsoid.
    private static func metres(from a: PPGeoPoint, to b: PPGeoPoint) -> Double {
        CLLocation(latitude: a.latitude, longitude: a.longitude)
            .distance(from: CLLocation(latitude: b.latitude,
                                       longitude: b.longitude))
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
        guard renderer != nil, !isRecording else { return }
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
        onRenderQueue({ $0.startRecording(to: url, name: name) }) { model, result in
            if case .failure(let error) = result {
                // Roll back: the recorder never opened.
                model.isRecording = false
                model.recordingURL = nil
                model.post("Could not start recording: \(error.localizedDescription)")
            } else {
                // Every time, not once: the cost of not knowing is a hole in
                // the ride. Auto-Lock is held off while recording, so the ways
                // to lose fixes are the deliberate ones — and Low Power Mode,
                // which overrides the idle timer. Delete this sentence when
                // fixes survive the background.
                model.post("Recording this ride. Locking the phone or "
                           + "switching apps stops it.")
            }
        }
    }

    func stopRecording() {
        guard renderer != nil, isRecording else { return }
        isRecording = false
        onRenderQueue({ renderer -> Int in
            let count = renderer.recordedPointCount()
            renderer.stopRecording()
            return count
        }) { model, count in
            model.recordedPointCount = count
            // Clear before listing, or the finished ride is filtered out.
            model.recordingURL = nil
            model.refreshRides()
            model.post(count == 0
                       ? "Nothing was recorded — no fixes arrived."
                       : "Ride saved, \(count) points. Hold the record "
                         + "button to share it.")
        }
    }

    /// Fetches the recorder's count from the render queue. Called by the
    /// sheet while it is open, not per frame.
    func refreshRecordedCount() {
        guard renderer != nil, isRecording else { return }
        onRenderQueue({ $0.recordedPointCount() }) { model, count in
            model.recordedPointCount = count
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

    /// The road graph has a beach, so the route sheet offers "Use beach:".
    var beachAvailable: Bool { renderer?.beachAvailable ?? false }
    var routeDefaultProfile: String { renderer?.routeDefaultProfile ?? "bicycle" }

    // MARK: - Symbol size

    /// The symbol sizes the menu offers, smallest first, from the pack.
    var symbolZoomSteps: [Double] { renderer?.symbolZoomSteps ?? [1.0] }

    /// Index into `symbolZoomSteps`. Stored as an index rather than a factor
    /// so a stored choice survives a pack that retunes its steps.
    @Published private(set) var symbolStep: Int = 0

    private static let symbolStepKey = "PPSymbolStep"

    /// Sets the symbol size step, persists it, and redraws. The clamp lives
    /// here so `symbolStep` is always a valid index; `persist` is false when
    /// restoring a stored choice made against a pack whose steps have since
    /// changed.
    func setSymbolStep(_ step: Int, persist: Bool = true) {
        let clamped = max(0, min(step, symbolZoomSteps.count - 1))
        guard clamped != symbolStep || !persist else { return }
        symbolStep = clamped
        if persist {
            UserDefaults.standard.set(clamped, forKey: Self.symbolStepKey)
        }
        applySymbolZoom()
    }

    /// Hands the current step's factor to the map. `PPMap` drops its cached
    /// base map on the set.
    private func applySymbolZoom() {
        guard renderer != nil else { return }
        let zoom = symbolZoomSteps[symbolStep]
        dropUnderlay()
        onRenderQueue({ $0.setSymbolZoom(zoom) }) { model, _ in
            model.setContentDirty()
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
        let centre = Self.centrePoint(of: vp.sizeInPoints)
        let tolerance = renderer.snapTolerance
        onRenderQueue({ renderer in
            // One hop for both so the two answers land together.
            (place: renderer.describePlace(at: coordinate, profile: profile,
                                           remembering: true),
             snap: renderer.snapTarget(near: centre, in: vp,
                                       tolerance: tolerance))
        }) { model, answer in
            model.placeQueryInFlight = false
            // The pick ended while this was in flight.
            guard model.pickProfile != nil else { return }
            model.pickPlace = answer.place
            model.pickSnap = answer.snap
            if model.placeQueryPending {
                model.placeQueryPending = false
                model.refreshPickPlace()
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
        stopCoast()
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
        return vp.geo(at: Self.centrePoint(of: vp.sizeInPoints))
    }

    /// The centre pixel of a surface. The `- 1` matches the renderer's pixel
    /// centre convention, so the label, the stored coordinate and the snap
    /// query all ask about the same pixel.
    private static func centrePoint(of size: CGSize) -> CGPoint {
        CGPoint(x: (size.width - 1) / 2, y: (size.height - 1) / 2)
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
        applyFrameRate()
    }

    /// Hands the display link the rate `framePolicy` gives the current mode.
    private func applyFrameRate() {
        guard let link else { return }
        let rate = framePolicy.rate(gesture: gestureActive, following: gpsMode)
        guard rate != appliedFrameRate else { return }
        appliedFrameRate = rate
        switch rate {
        case .system:
            link.preferredFrameRateRange = .default
        case .fixed(let fps):
            link.preferredFrameRateRange =
                CAFrameRateRange(minimum: fps, maximum: fps, preferred: fps)
        }
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

    /// Fixes received since launch, for the energy log.
    private var fixCount = 0

    /// Follow frames advanced without drawing, since launch.
    private var cameraSteps = 0

    /// The camera has been stepped away from the drawn frame, so one drawn
    /// frame is owed before the loop pauses.
    private var stepLeftFrameBehind = false

    /// Cost of the last base draw, kept apart from the frame time: a cached
    /// frame reports only its overlay pass.
    private var lastBaseDrawMs = 0.0

    /// Base maps drawn during the current gesture. A pinch's first is always
    /// allowed; see `tick()`.
    private var baseDrawsThisGesture = 0

    /// The finger's last velocity in points per second, for the band's lead.
    private var panVelocity = CGVector.zero

    /// The fling in progress. `gestureActive` stays true while it runs.
    private var coast: Coast?

    /// The chart has been turned by two fingers during this gesture, so the
    /// band is drawn rotation-safe.
    private var turningThisGesture = false

    /// The last frame's base is resampled on screen, or is not the resting
    /// band, so one exact banded frame is owed when the map stops. See
    /// `settle`.
    private var settleOwed = false

    /// The underlay camera last asked for, drawn or failed. A build is started
    /// only when this no longer serves the live camera, so a failure is not
    /// retried every tick.
    private var underlayKey: PPViewport?

    /// Bumped whenever the underlay is dropped, so a build already on the
    /// render queue cannot bring back pixels drawn under the old settings.
    private var underlayGeneration = 0

    /// Soft and blank frames since launch, for `-PPShowStats`: frames on which
    /// some of the screen was the underlay, or the background colour.
    private var softFrames = 0
    private var blankFrames = 0
    private var lastCountedViewport: PPViewport?

    /// Drops the underlay; the next idle loop builds a new one.
    private func dropUnderlay() {
        underlay = nil
        underlayKey = nil
        underlayGeneration += 1
    }

    /// Called where the loop has nothing to draw. Starts an underlay build
    /// when the current one no longer serves the camera, and otherwise pauses
    /// the display link. The render queue is serial, so the build runs only
    /// here, never ahead of a frame that is already waiting.
    private func pauseOrBuildUnderlay(_ vp: PPViewport?) {
        guard !gestureActive, let renderer, let vp, vp.hasSurface, frame != nil,
              !(underlayKey?.underlayServes(vp) ?? false) else {
            link?.isPaused = true
            return
        }
        underlayKey = vp.forUnderlay()
        renderInFlight = true
        let generation = underlayGeneration
        renderQueue.async { [weak self] in
            let result = renderer.renderUnderlay(for: vp)
            Task { @MainActor in
                guard let self else { return }
                self.renderInFlight = false
                if generation == self.underlayGeneration,
                   case .success(let underlay) = result {
                    self.underlay = underlay
                    self.mapBackground = underlay.backgroundColor
                }
                // The next tick pauses if nothing else is due.
                self.link?.isPaused = false
            }
        }
    }

    #if DEBUG
    /// Counts the displayed frame as soft or blank when the base does not
    /// reach the whole live screen. Only frames on which the camera moved are
    /// counted, so an idle display link adds nothing.
    private func countCoverage(_ vp: PPViewport) {
        guard Self.showsStats, let frame,
              lastCountedViewport.map({ !vp.isEquivalent(to: $0) }) ?? true
        else { return }
        lastCountedViewport = vp
        switch frame.baseViewport.coverage(of: vp, underlay: underlay?.viewport) {
        case .sharp: break
        case .underlay: softFrames += 1
        case .background: blankFrames += 1
        @unknown default: break
        }
    }
    #endif

    /// The band to draw the base at for `vp`, or nil for the screen itself.
    /// None when the scale changed since the last frame (no cached base can
    /// serve a zoom, so the band would be paid for and never used) and on the
    /// first frame (no cache yet). `lead` shifts it ahead of a pan, in points.
    private func band(for vp: PPViewport, lead: CGVector = .zero) -> PPViewport? {
        guard let renderer, let last = frame, renderer.bandMargin > 0 else { return nil }
        if last.viewport.scaleDenominator != vp.scaleDenominator { return nil }
        let centre = lead == .zero
            ? vp : vp.panned(by: lead)
        return centre.grown(byMargin: renderer.bandMargin,
                            rotationSafe: gestureActive && turningThisGesture)
    }

    /// Where the pan will have carried the camera `seconds` from now, in
    /// points of content movement, capped inside the band's margin.
    private func lead(for vp: PPViewport, seconds: Double) -> CGVector {
        guard let renderer, panVelocity != .zero else { return .zero }
        let m = renderer.bandMargin
        return CGVector(
            dx: PPBandLead(Double(panVelocity.dx), seconds,
                           m * vp.sizeInPoints.width),
            dy: PPBandLead(Double(panVelocity.dy), seconds,
                           m * vp.sizeInPoints.height))
    }

    /// During a same-scale gesture: true when the base must be drawn now,
    /// because it no longer covers the live camera or because the screen,
    /// predicted one base draw ahead, is within `bandRefreshFraction` of the
    /// band margin of its edge.
    private func bandRefreshDue(_ vp: PPViewport, base: PPViewport,
                                renderer: Renderer) -> Bool {
        if !base.covers(vp, maxTurnDegrees: renderer.baseMaxTurnDegrees) { return true }
        let ahead = lead(for: vp, seconds: lastBaseDrawMs / 1000)
        let predicted = ahead == .zero
            ? vp : vp.panned(by: ahead)
        let shortSide = min(vp.sizeInPoints.width, vp.sizeInPoints.height)
        let marginPx = renderer.bandMargin * Double(shortSide * vp.displayScale)
        return base.bandHeadroom(for: predicted)
            < renderer.bandRefreshFraction * marginPx
    }

    private func tick() {
        advanceCoast()
        #if DEBUG
        if let vp = viewport, vp.hasSurface { countCoverage(vp) }
        #endif
        guard !renderInFlight else { return }
        guard needsRender, let renderer, let vp = viewport, vp.hasSurface else {
            // A stepped camera at rest is shown through a sub-point preview
            // transform; draw it once exactly before pausing.
            if stepLeftFrameBehind, let renderer, let vp = viewport, vp.hasSurface {
                draw(vp, renderer: renderer, band: band(for: vp), reuseBase: true)
                return
            }
            // Nothing to draw: pause, unless a settle frame is owed.
            if !settle(renderer: renderer, viewport: viewport) {
                pauseOrBuildUnderlay(viewport)
            }
            return
        }
        var reuseBase = true
        var drawBand = band(for: vp)
        if gestureActive, let last = frame {
            if last.baseViewport.scaleDenominator != vp.scaleDenominator {
                // A pinch misses every frame. After its first draw, a slow
                // base leaves the preview transform to carry it.
                if baseDrawsThisGesture > 0, lastBaseDrawMs > liveRenderBudgetMs {
                    return
                }
            } else if bandRefreshDue(vp, base: last.baseViewport, renderer: renderer) {
                reuseBase = false
                drawBand = band(for: vp, lead: lead(for: vp, seconds: lastBaseDrawMs / 1000))
            }
        }
        // Already showing this viewport and nothing else changed.
        if reuseBase, !contentDirty, let last = frame, vp.isEquivalent(to: last.viewport) {
            needsRender = false
            if !settle(renderer: renderer, viewport: vp) {
                pauseOrBuildUnderlay(vp)
            }
            return
        }
        if gpsMode, !gestureActive, !contentDirty, let last = frame,
           let moved = followMovement(from: last.viewport, to: vp),
           !framePolicy.drawsFollowFrame(movement: moved) {
            needsRender = false
            step(vp, renderer: renderer)
            return
        }

        needsRender = false
        contentDirty = false
        draw(vp, renderer: renderer, band: drawBand, reuseBase: reuseBase)
    }

    /// How far the picture moves from the drawn camera to the live one, in
    /// points, or nil when they differ in anything but centre and rotation.
    private func followMovement(from drawn: PPViewport, to live: PPViewport) -> CGFloat? {
        guard drawn.scaleDenominator == live.scaleDenominator,
              drawn.sizeInPoints == live.sizeInPoints,
              drawn.displayScale == live.displayScale,
              drawn.mmPerPixel == live.mmPerPixel else { return nil }
        let old = live.point(forGeo: drawn.center)
        let now = live.point(forGeo: live.center)
        return FramePolicy.movement(
            centerShift: CGVector(dx: old.x - now.x, dy: old.y - now.y),
            rotationDelta: live.rotationDegrees - drawn.rotationDegrees,
            surface: live.sizeInPoints)
    }

    /// Advances the moving map without drawing. The last frame stays on
    /// screen and `MapScreen`'s preview transform carries the sub-point move.
    private func step(_ vp: PPViewport, renderer: Renderer) {
        renderInFlight = true
        renderQueue.async { [weak self] in
            let step = renderer.stepCamera(at: vp)
            Task { @MainActor in
                guard let self else { return }
                self.renderInFlight = false
                if let step {
                    self.cameraSteps += 1
                    self.stepLeftFrameBehind = true
                    self.ownship = step.ownship
                    self.trip = step.trip
                    self.guidance = step.guidance
                    if !step.guidanceEvents.isEmpty {
                        self.alerts.play(step.guidanceEvents)
                    }
                    self.applyCamera(hasUpdate: step.hasCameraUpdate,
                                     center: step.cameraCenter,
                                     rotation: step.cameraRotationDegrees,
                                     animating: step.cameraIsAnimating)
                    // The ownship moved; the next frame is drawn.
                    if step.sawNewFix { self.setContentDirty() }
                }
                self.link?.isPaused = false
            }
        }
    }

    /// Draws the resting band when the loop is about to pause with a
    /// resampled base on screen, or with a base that is not the resting band
    /// (none after a pinch, led or rotation-safe after a drag or turn). The
    /// band is drawn at the live camera and lands on the screen's pixels, so
    /// `MapScreen` displays it unfiltered and the next drag starts covered.
    /// Returns true when a settle was started.
    @discardableResult
    private func settle(renderer: Renderer?, viewport vp: PPViewport?) -> Bool {
        guard settleOwed, let renderer, let vp, vp.hasSurface,
              !gestureActive else { return false }
        needsRender = false
        contentDirty = false
        let resting = vp.grown(byMargin: renderer.bandMargin)
        draw(vp, renderer: renderer, band: resting, reuseBase: false)
        return true
    }

    /// True when `frame` needs a settle: its base is not on the screen's
    /// pixels, or is not the size of the resting band.
    private func owesSettle(_ frame: PPFrame) -> Bool {
        guard let renderer else { return false }
        let base = frame.baseViewport
        if !base.isPixelAligned(to: frame.viewport) { return true }
        let resting = frame.viewport.grown(byMargin: renderer.bandMargin)
        return base.sizeInPoints != resting.sizeInPoints
    }

    private func draw(_ vp: PPViewport, renderer: Renderer,
                      band: PPViewport?, reuseBase: Bool) {
        renderInFlight = true
        stepLeftFrameBehind = false
        renderQueue.async { [weak self] in
            let result = renderer.render(vp, band: band, reuseBase: reuseBase)
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
                    self.settleOwed = self.owesSettle(frame)
                    self.ownship = frame.ownship
                    self.trip = frame.trip
                    self.guidance = frame.guidance
                    // After the banner, so the sound and the words a rider
                    // looks up to read describe the same corner.
                    if !frame.guidanceEvents.isEmpty {
                        self.alerts.play(frame.guidanceEvents)
                    }
                    #if DEBUG
                    if Self.showsStats { self.status = self.describe(frame) }
                    #endif
                    self.applyCamera(hasUpdate: frame.hasCameraUpdate,
                                     center: frame.cameraCenter,
                                     rotation: frame.cameraRotationDegrees,
                                     animating: frame.cameraIsAnimating)
                case .failure(let error):
                    self.failure = error.localizedDescription
                    // Do not retry a failed settle on every tick.
                    self.settleOwed = false
                }
                // The next tick pauses the loop if nothing is dirty.
                self.link?.isPaused = false
            }
        }
    }

    /// Applies the moving-map camera's answer from a frame or a step. The
    /// scale is left alone (the camera has no opinion on it), and a live
    /// gesture wins outright: the next tick re-bases from wherever the finger
    /// left off.
    private func applyCamera(hasUpdate: Bool, center: PPGeoPoint,
                             rotation: Double, animating: Bool) {
        if hasUpdate, !gestureActive, let vp = viewport {
            viewport = vp.moved(toCenter: center,
                                scaleDenominator: vp.scaleDenominator,
                                rotationDegrees: rotation)
        }
        // Keep the loop awake for the rest of a slew.
        if animating { setNeedsRender() }
    }

    // MARK: - Misc

    var styleName: String { renderer?.styleName ?? "" }
    var hasLabelFont: Bool { renderer?.hasLabelFont ?? false }

    /// `Documents/current.fvrte`. Nil only if Foundation cannot name the
    /// directory, in which case the route is simply not persisted.
    private static func routeDocumentURL() -> URL? {
        DocumentFolder.documents?.appendingPathComponent("current.fvrte")
    }

    /// `Documents/points.fvpoints`. The bundle copy is read-only; the point
    /// store seeds this file from it on first launch.
    private static func pointsDocumentURL() -> URL? {
        DocumentFolder.documents?.appendingPathComponent("points.fvpoints")
    }

    #if DEBUG

    /// The `-PPShowStats` line. `12/68 ms` is a 12 ms overlay pass over a base
    /// that cost 68; `3/– ms` is an overlay pass over a cached base. Feature
    /// and draw counts belong to the base, so on a hit they are the cached
    /// frame's. `soft` and `blank` count moving frames on which the base fell
    /// short of the screen; `under` is the last underlay's draw time.
    private func describe(_ frame: PPFrame) -> String {
        let base = frame.baseWasDrawn
            ? String(format: "%.0f", frame.baseMilliseconds) : "–"
        return String(
            format: "z%ld · %lu features · %lu draws · %.0f/%@ ms · %d/%d cached @%.0f · %d stepped · %d soft %d blank · under %@ · 1:%@",
            frame.queryZoom, frame.featuresQueried, frame.drawsEmitted,
            frame.renderMilliseconds - frame.baseMilliseconds, base,
            baseHits, baseHits + baseDraws, lastHitMs, cameraSteps,
            softFrames, blankFrames,
            underlay.map { String(format: "%.0f ms", $0.renderMilliseconds) } ?? "–",
            Self.scaleFormatter.string(from: NSNumber(value: frame.viewport.scaleDenominator))
                ?? "?")
    }

    private static let scaleFormatter: NumberFormatter = {
        let f = NumberFormatter()
        f.numberStyle = .decimal
        f.maximumFractionDigits = 0
        return f
    }()

    #endif  // DEBUG
}

/// Wraps `PPMap` with the promise that only the render queue touches it.
/// Launch-time constants are read once at construction on the main thread so
/// nothing later has to hop the queue for a value that cannot change.
private final class Renderer: @unchecked Sendable {
    private let map: PPMap
    let styleName: String
    let hasLabelFont: Bool
    let routeProfileNames: [String]
    /// The graph has beach arcs. Fixed for the pack.
    let beachAvailable: Bool
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
    /// `display.band_refresh_fraction`: how close to the band's edge a
    /// gesture may predict the screen before the band is redrawn.
    let bandRefreshFraction: Double
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
    /// `display.fling_min_speed_pt` and `display.fling_deceleration`.
    let flingMinSpeedPoints: Double
    let flingDecelerationRate: Double
    /// `display.follow_fps` and `display.min_move_pt`, for `FramePolicy`.
    let followFramesPerSecond: Double
    let followMinMovePoints: Double
    /// The pack's tide table, or nil. Immutable, so read from any thread.
    let tide: PPTide?

    init(map: PPMap) {
        self.map = map
        // The rule file lives in a read-only bundle, so its profiles are
        // constants too.
        styleName = map.styleName
        hasLabelFont = map.hasLabelFont
        routeProfileNames = map.routeProfileNames
        beachAvailable = map.beachAvailable
        if let depart = TideClock.override { map.routeDepartureOverride = depart }
        routeDefaultProfile = map.routeDefaultProfile
        pointHitTolerance = map.pointHitTolerance
        ownshipSymbolRadius = map.ownshipSymbolRadiusInPoints
        initialCourseUp = map.isCourseUpEnabled
        homeBounds = map.homeBounds
        bandMargin = map.baseCacheBandMargin
        bandRefreshFraction = map.baseCacheRefreshFraction
        baseMaxTurnDegrees = map.baseCacheMaxTurnDegrees
        searchFrameMinScale = map.searchFrameMinScale
        searchFrameTopBias = map.searchFrameTopBias
        searchInitialText = map.searchInitialText
        symbolZoomSteps = map.symbolZoomSteps.map(\.doubleValue)
        flingMinSpeedPoints = map.flingMinSpeedPoints
        flingDecelerationRate = map.flingDecelerationRate
        followFramesPerSecond = map.followFramesPerSecond
        followMinMovePoints = map.followMinMovePoints
        tide = map.tide
    }

    /// Render queue only. See `PPMap.h` for `band` and `reuseBase`.
    func render(_ viewport: PPViewport, band: PPViewport?,
                reuseBase: Bool) -> Result<PPFrame, Error> {
        Result { try map.render(viewport, band: band, reuseBase: reuseBase) }
    }

    /// Render queue only. See `PPMap.stepCameraAtViewport:`.
    func stepCamera(at viewport: PPViewport) -> PPCameraStep? {
        map.stepCamera(at: viewport)
    }

    /// Render queue only. Drops the cached base map; see `PPMap.symbolZoom`.
    func setSymbolZoom(_ zoom: Double) {
        map.symbolZoom = zoom
    }

    /// Render queue only. Drops the cached base map.
    func invalidateBaseLayer() {
        map.invalidateBaseLayer()
    }

    /// Render queue only. See `PPMap.renderUnderlayForViewport:`.
    func renderUnderlay(for viewport: PPViewport) -> Result<PPUnderlay, Error> {
        Result { try map.renderUnderlay(for: viewport) }
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
        Result { try map.startDemoFeed() }
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
        Result { try map.loadSavedRoute() }
    }

    func setRoute(waypoints: [PPWaypoint], profile: String) -> PPRoute {
        map.setRoute(waypoints: waypoints, profile: profile)
    }

    func setRoute(waypoints: [PPWaypoint], profile: String,
                  beachUse: PPBeachUse) -> PPRoute {
        map.setRoute(waypoints: waypoints, profile: profile, beachUse: beachUse)
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
        Result { try map.loadPoints() }
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
        Result { try map.startRecording(to: url, name: name) }
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
