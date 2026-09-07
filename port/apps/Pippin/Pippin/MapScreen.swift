// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI
import UIKit

/// The app's one screen: a full-bleed map with controls floating in the safe
/// area over it.
///
/// The screen has three mutually exclusive states for the route flow (map,
/// map with sheet, map with crosshair) and the same three again for the point
/// flow. The crosshair state exists because "Pick on map" must dismiss the
/// sheet, so the drafts are owned here rather than by the sheets. The rides,
/// problem-report and about sheets are about the app rather than the map and
/// do not join that exclusion.
struct MapScreen: View {
    @StateObject private var model = MapModel()
    @StateObject private var draft = RouteDraft()
    @Environment(\.displayScale) private var displayScale
    /// Opens Settings for a notice that offers it. See `openSettings()`.
    @Environment(\.openURL) private var openURL

    /// Measured heights of the ride bar and of its right-hand cluster,
    /// reported by the bar via preference keys. The developer readout clears
    /// the whole bar; the compass clears only the right cluster. Zero when
    /// there is no bar.
    @State private var rideBarHeight: CGFloat = 0
    @State private var rideBarTrailingHeight: CGFloat = 0

    /// The window's safe-area insets. Read from UIKit rather than the
    /// `GeometryProxy`, which reports zero here because the reader has
    /// `.ignoresSafeArea()` on it (the map surface must be the whole screen).
    /// The app is portrait only, so the value is fixed for the process.
    @State private var safeArea: UIEdgeInsets = .zero

    @State private var sheetShown = false
    /// Which draft row the crosshair is filling, or nil when it is not up.
    @State private var pickingIndex: Int?

    /// Point flow state: `infoShown` is a tap on a marker, `editing` is the
    /// form, `placingPoint` is the crosshair the form dismisses into. The
    /// draft lives here because "Move on map" destroys the sheet.
    @State private var infoShown = false
    @State private var editing: PointDraft?
    @State private var placingPoint: PointDraft?

    @State private var ridesShown = false
    @State private var problemShown = false
    @State private var aboutShown = false

    /// The unit preference. `@ObservedObject` because this view does not own
    /// it; `DisplayUnits.shared` is also observed from views across sheet
    /// boundaries.
    @ObservedObject private var display = DisplayUnits.shared

    /// `-PPShowStats YES` shows the renderer's per-frame cost. DEBUG only; in
    /// Release the property, overlay and formatters are compiled out.
    #if DEBUG
    private let showsStats = UserDefaults.standard.bool(forKey: "PPShowStats")
    #endif

    var body: some View {
        GeometryReader { geo in
            // The map is full-bleed, so everything floating over it keeps
            // clear of the status bar and home indicator by hand.
            let top = Self.topMargin(safeArea.top)
            let bottom = Self.bottomMargin(safeArea.bottom)
            ZStack {
                mapLayer(size: geo.size)
                MapGestureView(
                    onBegan: { model.gestureBegan() },
                    onEnded: { model.gestureEnded() },
                    onPan: { model.pan(by: $0) },
                    onZoom: { model.zoom(by: $0, about: $1) },
                    onTap: { handleTap(at: $0) },
                    onRotate: { model.rotate(by: $0, about: $1) },
                    onHoldBegan: { point, grabbed in
                        handleHold(at: point, then: grabbed)
                    },
                    onHoldMoved: { model.dragRouteWaypoint(to: $0) },
                    onHoldEnded: { model.dropRouteWaypoint(at: $0) },
                    onHoldCancelled: { model.cancelRouteWaypointDrag() },
                    holdSeconds: model.routeWaypointHoldSeconds)
                if let index = pickingIndex {
                    pickOverlay(index: index, top: top, bottom: bottom)
                } else if let draft = placingPoint {
                    pointPlaceOverlay(draft: draft, top: top, bottom: bottom)
                } else {
                    controls(top: top)
                }
                if model.gpsMode { rideBar(top: top) }
                if let notice = model.notice { noticeBanner(notice) }
                #if DEBUG
                if showsStats { statsOverlay(top: top) }
                #endif
                if let failure = model.failure { failureOverlay(failure) }
            }
            // Preferences travel up the tree, so the bar's height is read here
            // where both it and the views that clear it are visible.
            .onPreferenceChange(RideBarHeightKey.self) { height in
                rideBarHeight = height
            }
            .onPreferenceChange(RideBarTrailingHeightKey.self) { height in
                rideBarTrailingHeight = height
            }
            .onAppear {
                safeArea = Self.windowSafeArea()
                model.resize(to: geo.size, scale: displayScale)
                model.startFeed()
                model.loadSavedRoute()
                model.loadPoints()
                draft.profile = model.routeDefaultProfile
                // `-PPGestureDemo YES`: scripted gestures, DEBUG only.
                #if DEBUG
                if UserDefaults.standard.bool(forKey: "PPGestureDemo") {
                    GestureDemo.run(on: model, size: geo.size)
                }
                #endif
            }
            .onChange(of: geo.size) { _, size in
                // A size change is the one moment the insets can change too.
                safeArea = Self.windowSafeArea()
                model.resize(to: size, scale: displayScale)
            }
            // `pippin://place?lat=…&lon=…` from the share extension, whether
            // the app was running or launched by the share.
            .onOpenURL { url in acceptSharedPlace(from: url) }
            // A `.sheet` is a new presentation and does not inherit this
            // view's environment values, so each one gets them explicitly.
            .sheet(isPresented: $sheetShown) {
                routeSheet
                    .environment(\.describePlace, model.describePlace)
                    .environment(\.searchPlaces, model.search(for:))
            }
            // Two sheet modifiers rather than one `item:`: the info sheet is
            // bound to the model's selection (re-read after edits) and the
            // editor to a draft this view owns.
            .sheet(isPresented: $infoShown) { pointInfoSheet }
            .sheet(isPresented: $ridesShown) { ridesSheet }
            .sheet(isPresented: $problemShown) { problemSheet }
            .sheet(isPresented: $aboutShown) {
                AboutSheet(onClose: { aboutShown = false })
            }
            .sheet(item: $editing) { draft in
                pointEditSheet(draft)
                    .environment(\.describePlace, model.describePlace)
                    .environment(\.searchPlaces, model.search(for:))
            }
        }
        .ignoresSafeArea()
    }

    // MARK: - Route flow

    private var routeSheet: some View {
        RouteSheet(
            draft: draft,
            route: model.route,
            isPlanning: model.isPlanning,
            currentLocation: model.currentLocation,
            profileNames: model.routeProfileNames,
            search: model.search(for:),
            initialSearchText: model.lastSearchText,
            onSearched: { model.rememberSearch($0) },
            onPick: { index in
                sheetShown = false
                pickingIndex = index
            },
            onFrame: { result in
                model.frame(result)
            },
            onSubmit: {
                model.setRoute(waypoints: draft.waypoints(), profile: draft.profile)
                sheetShown = false
            },
            // Walk/Cycle replans the live route's waypoints on the spot, not
            // the draft's: a half-edited draft must not reach the document.
            // No-op with no route on the map.
            onProfileChange: { profile in
                guard let route = model.route, route.exists else { return }
                model.setRoute(waypoints: route.waypoints, profile: profile)
            },
            onClear: {
                // The sheet stays up and returns to the search box on an
                // empty draft; `load(from: nil)` sets the phase.
                model.clearRoute()
                draft.load(from: nil)
            },
            onCancel: {
                // Discards the draft only. Clear and the mode picker act on
                // the live route and are not undone.
                sheetShown = false
            })
        .presentationDetents([.medium, .large])
    }

    /// The crosshair state for a route stop. The map keeps its gestures; the
    /// coordinate is whatever is under the centre pixel.
    private func pickOverlay(index: Int, top: CGFloat,
                             bottom: CGFloat) -> some View {
        // `pickCoordinate` rather than `crosshairCoordinate`: when snapped to
        // a feature, the stored coordinate is that feature's exact position.
        let coordinate = model.pickCoordinate
        return PickOverlay(
            title: "Drag the map to place \(draft.label(at: index))",
            coordinate: coordinate,
            place: model.pickPlace,
            snap: model.pickSnap,
            topMargin: top,
            bottomMargin: bottom,
            onConfirm: {
                // The index outlived the sheet that validated it; check it here.
                if let coordinate, draft.stops.indices.contains(index) {
                    draft.stops[index].coordinate = coordinate
                    // Picked from the map, so the row names the road under it.
                    draft.stops[index].name = nil
                }
                pickingIndex = nil
                sheetShown = true
            },
            onCancel: {
                pickingIndex = nil
                sheetShown = true
            })
            // The overlay's lifetime is the pick.
            .onAppear { model.beginPick(profile: draft.profile) }
            .onDisappear { model.endPick() }
    }

    // MARK: - Point flow

    /// A tap on the map: a marker under the finger selects it and opens the
    /// info sheet; a miss with a sheet open closes it; a miss with nothing
    /// open does nothing (so a finger that lands without moving does not
    /// flicker a dialog).
    private func handleTap(at location: CGPoint) {
        // During a placement the map is being dragged, not tapped.
        guard pickingIndex == nil, placingPoint == nil, editing == nil else {
            return
        }
        model.point(under: location) { hit in
            if let hit {
                model.select(hit)
                infoShown = true
            } else if infoShown {
                infoShown = false
                model.select(nil)
            }
        }
    }

    /// A press held on the map. Tries to grab a route waypoint and tells the
    /// recognizer whether the rest of the touch belongs to it. Refused during
    /// a placement and while the route sheet (the other editor of the same
    /// waypoints) is up.
    private func handleHold(at location: CGPoint,
                            then grabbed: @escaping (Bool) -> Void) {
        guard pickingIndex == nil, placingPoint == nil, editing == nil,
              !sheetShown else {
            grabbed(false)
            return
        }
        model.grabRouteWaypoint(at: location, then: grabbed)
    }

    /// The info sheet, bound to the model's selection so an edit that lands
    /// while it is up shows the document's version.
    @ViewBuilder
    private var pointInfoSheet: some View {
        if let point = model.selectedPoint {
            PointInfoSheet(
                point: point,
                icon: model.symbol(id: point.symbolId),
                onEdit: {
                    // Selection stays so the marker remains highlighted.
                    infoShown = false
                    editing = PointDraft(editing: point)
                },
                onDelete: {
                    infoShown = false
                    model.deletePoint(id: point.pointId)
                },
                onClose: {
                    infoShown = false
                    model.select(nil)
                })
            .presentationDetents([.medium, .large])
        } else {
            // The selected point was deleted from under the sheet. Close it
            // from a task: view state cannot be mutated during body evaluation.
            Color.clear.task { infoShown = false }
        }
    }

    /// Starts a new point from a long press on the points button. Opens the
    /// editor with no location; the Location row offers the phone's fix or
    /// the map, and Save is held back until one is chosen. The size matches
    /// an existing point so the marker sits beside its neighbours.
    private func beginNewPoint() {
        guard pickingIndex == nil, placingPoint == nil, editing == nil else {
            return
        }
        infoShown = false
        model.select(nil)
        editing = PointDraft(newAt: nil,
                             sizePx: model.points.first?.sizePx ?? 22)
    }

    private func pointEditSheet(_ draft: PointDraft) -> some View {
        PointEditSheet(
            draft: draft,
            symbols: model.pointSymbols,
            currentLocation: model.currentLocation,
            onPickLocation: {
                editing = nil
                placingPoint = draft
            },
            onFrameFound: { result in model.frame(result) },
            onSave: {
                if let point = draft.mapPoint() {
                    if draft.pointId == 0 {
                        model.addPoint(point)
                    } else {
                        model.updatePoint(point)
                    }
                    // Back to the info sheet on the saved point, which the
                    // model re-reads from the document.
                    editing = nil
                    infoShown = true
                } else {
                    editing = nil
                }
            },
            onCancel: {
                editing = nil
                // A cancelled edit returns to the point's info sheet; a
                // cancelled new point leaves nothing selected.
                if draft.pointId != 0 { infoShown = true } else { model.select(nil) }
            })
    }

    /// The crosshair state for a point. Same `PickOverlay` as the route.
    private func pointPlaceOverlay(draft: PointDraft, top: CGFloat,
                                   bottom: CGFloat) -> some View {
        let coordinate = model.pickCoordinate
        let naming = draft.name.trimmingCharacters(in: .whitespaces)
        return PickOverlay(
            title: draft.pointId == 0
                ? "Drag the map to place the new pin"
                : "Drag the map to move \(naming.isEmpty ? "this pin" : naming)",
            coordinate: coordinate,
            place: model.pickPlace,
            snap: model.pickSnap,
            topMargin: top,
            bottomMargin: bottom,
            onConfirm: {
                if let coordinate { draft.coordinate = coordinate }
                placingPoint = nil
                editing = draft
            },
            onCancel: {
                // Cancelling a move keeps the position the point already had.
                placingPoint = nil
                editing = draft
            })
            .onAppear { model.beginPick(profile: model.namingProfile) }
            .onDisappear { model.endPick() }
    }

    // MARK: - Shared places

    /// Handles a `pippin://place` URL. The URL is untrusted input from another
    /// process: `PlaceLink.place(inCallback:)` validates it, and a malformed
    /// one does nothing. Refused while a placement is in progress.
    private func acceptSharedPlace(from url: URL) {
        // stdout for `devicectl … --console`, the log for Console.app.
        #if DEBUG
        print("\(AppName.display): onOpenURL \(url.absoluteString)")
        #endif
        PippinLog.share.notice("app: onOpenURL \(url.absoluteString, privacy: .public)")

        guard let place = PlaceLink.place(inCallback: url) else {
            PippinLog.share.error("app: that URL is not a place callback")
            return
        }
        guard pickingIndex == nil, placingPoint == nil else {
            PippinLog.share.notice("app: refused — a placement is in progress")
            return
        }

        editing = nil
        sheetShown = false
        infoShown = false
        // Turn the layer on before the add so "Added …" is the notice that
        // survives, not the layer's first-time hint.
        model.setPointsVisible(true)
        model.acceptSharedPlace(place) { id in
            // Open the sheet only after the model has published the new
            // selection; earlier and `pointInfoSheet` would close it again.
            guard id != 0 else { return }
            infoShown = true
        }
    }

    // MARK: - Menu

    /// Names for the size menu. The pack owns the numbers; a step beyond
    /// these names shows its multiplier.
    private static let symbolSizeNames = ["Small", "Medium", "Large"]

    private var symbolStepLabels: [String] {
        let steps = model.symbolZoomSteps
        return steps.indices.map { index in
            index < Self.symbolSizeNames.count
                ? Self.symbolSizeNames[index]
                : String(format: "%.2gx", steps[index])
        }
    }

    private var problemSheet: some View {
        ReportProblemSheet(
            // The map centre, to five decimals (about a metre).
            coordinateText: model.viewport.map {
                String(format: "%.5f, %.5f", $0.center.latitude,
                       $0.center.longitude)
            } ?? "—",
            onClose: { problemShown = false })
    }

    // MARK: - Rides

    private var ridesSheet: some View {
        RideSheet(
            rides: model.rides,
            isRecording: model.isRecording,
            recordingPointCount: model.recordedPointCount,
            onToggleRecording: { model.toggleRecording() },
            onPollCount: { model.refreshRecordedCount() },
            onDelete: { model.deleteRide($0) },
            onClose: { ridesShown = false })
    }

    // MARK: - Map layers and preview transform

    /// The transform that places a layer drawn at one viewport under the live
    /// one: a scale, a turn and an offset. The offset is computed by asking
    /// the live viewport where the layer's centre is now, so it is the
    /// projection's own answer and does not drift. The same transform serves
    /// the cached base map, which is drawn larger than the screen.
    private struct Preview {
        var k: Double
        var turn: Double
        var dx: Double
        var dy: Double
        var size: CGSize
        var scale: CGFloat
        /// The layer is already exactly where it belongs, pixel for pixel.
        var isLive: Bool
    }

    private static func preview(of drawn: PPViewport,
                                in live: PPViewport) -> Preview {
        let k = drawn.scaleDenominator / live.scaleDenominator
        // Both points come from the projection, so the centre-pixel
        // convention ((w-1)/2, not w/2) cancels.
        let was = live.point(forGeo: drawn.center)
        let now = live.point(forGeo: live.center)
        let turn = live.rotationDegrees - drawn.rotationDegrees
        let dx = was.x - now.x
        let dy = was.y - now.y
        // A layer larger than the screen is not live even at zero offset: its
        // pixels land between the screen's. This is why the settle frame asks
        // for no band.
        let sameSize = drawn.sizeInPoints == live.sizeInPoints
        let isLive = sameSize && abs(k - 1) < 1e-9 && abs(dx) < 0.01
            && abs(dy) < 0.01 && abs(turn) < 1e-9
        return Preview(k: k, turn: turn, dx: dx, dy: dy,
                       size: drawn.sizeInPoints, scale: drawn.displayScale,
                       isLive: isLive)
    }

    @ViewBuilder
    private static func layer(_ image: CGImage, _ p: Preview) -> some View {
        // Live: one rendered pixel per device pixel, so no interpolation.
        // Under a preview transform the pixels do not line up and smoothing
        // looks better than blocks.
        Image(decorative: image, scale: p.scale)
            .interpolation(p.isLive ? .none : .medium)
            .resizable()
            .frame(width: p.size.width, height: p.size.height)
            .scaleEffect(p.k)
            .rotationEffect(.degrees(p.turn))
            .offset(x: p.dx, y: p.dy)
    }

    @ViewBuilder
    private func mapLayer(size: CGSize) -> some View {
        if let frame = model.frame, let live = model.viewport {
            // Two layers with separate transforms: on a cache hit the base
            // was drawn at an older camera than the overlay.
            //
            // The stack is pinned to the screen size before clipping. A
            // ZStack takes the size of its largest child, and the base is a
            // band 1.5x the surface; without the frame, `.clipped()` clips to
            // the oversized bounds and the GeometryReader's top-leading
            // alignment shifts the whole stack by a quarter of the surface.
            ZStack {
                Self.layer(frame.baseImage, Self.preview(of: frame.baseViewport,
                                                         in: live))
                Self.layer(frame.overlayImage, Self.preview(of: frame.viewport,
                                                            in: live))
            }
            .frame(width: size.width, height: size.height)
            .clipped()
        } else {
            // Placeholder for the moment before the first render.
            Color(white: 0.92)
        }
    }

    // MARK: - Controls

    private func controls(top: CGFloat) -> some View {
        VStack {
            // Top-right compass, shown only when the map is following or the
            // chart is turned.
            if compassShown {
                HStack {
                    Spacer()
                    CompassButton(
                        northAngleDegrees: model.northScreenAngleDegrees,
                        isActive: model.gpsMode && model.courseUp,
                        label: compassLabel) {
                            model.toggleCompass()
                        }
                }
                .padding(.horizontal, ControlMetrics.side)
                // Drops below the ride bar's right cluster when there is one.
                .padding(.top, rideBarTrailingHeight > 0
                         ? top + rideBarTrailingHeight + 12 : top)
            }
            Spacer()
            // Bottom-aligned: both sides are columns of different heights.
            HStack(alignment: .bottom, spacing: ControlMetrics.spacing) {
                // Left column: the menu above the row of things a rider puts
                // on the map. `.leading` puts the menu over the route button.
                VStack(alignment: .leading, spacing: ControlMetrics.spacing) {
                    MapMenuButton(
                        symbolStep: Binding(get: { model.symbolStep },
                                            set: { model.setSymbolStep($0) }),
                        symbolStepLabels: symbolStepLabels,
                        units: $display.units,
                        onExportRide: {
                            model.refreshRides()
                            ridesShown = true
                        },
                        onAbout: { aboutShown = true },
                        onReportProblem: { problemShown = true })
                    HStack(spacing: ControlMetrics.spacing) {
                    // Route button. With a route on the map the sheet opens
                    // pre-filled. No `isActive`: this symbol has no `.fill`
                    // variant and the button would go blank.
                    CircleButton(
                        systemName: "point.topleft.down.curvedto.point.bottomright.up",
                        label: "Route") {
                            draft.load(from: model.route)
                            if draft.profile.isEmpty {
                                draft.profile = model.routeDefaultProfile
                            }
                            sheetShown = true
                        }
                    // Points button. Tap toggles visibility; a long press adds
                    // a point, offered only while the points are shown. The
                    // same glyph in both states with the tint as the state;
                    // `mappin.and.ellipse` has no `.fill` twin, so the active
                    // name is given explicitly.
                    CircleButton(
                        systemName: "mappin.and.ellipse",
                        label: model.pointsVisible ? "Hide pins" : "Show pins",
                        isActive: model.pointsVisible,
                        activeSystemName: "mappin.and.ellipse",
                        onLongPress: model.pointsVisible ? { beginNewPoint() } : nil,
                        longPressLabel: "Add a pin") {
                            model.togglePoints()
                        }
                    }
                }
                Spacer()
                // Right column: record above GPS mode, the two buttons pressed
                // mid-ride under the same thumb.
                VStack(spacing: ControlMetrics.spacing) {
                    // Tap starts/stops; hold opens the rides sheet. Red while
                    // recording because that is what red means.
                    CircleButton(
                        systemName: "record.circle",
                        label: model.isRecording ? "Stop recording" : "Record a ride",
                        isActive: model.isRecording,
                        activeSystemName: "stop.circle.fill",
                        activeTint: .red,
                        onLongPress: {
                            model.refreshRides()
                            ridesShown = true
                        },
                        longPressLabel: "Saved rides") {
                            model.toggleRecording()
                        }
                    CircleButton(
                        systemName: "location",
                        label: model.gpsMode ? "Exit GPS mode" : "GPS mode",
                        isActive: model.gpsMode) {
                            model.toggleGpsMode()
                        }
                }
            }
            .padding(.horizontal, ControlMetrics.side)
            .padding(.bottom, ControlMetrics.bottom)
        }
        .ignoresSafeArea(edges: .horizontal)
    }

    /// Top margin for floating furniture: the safe-area inset (floor 20 for a
    /// device that reports none) plus a gap.
    private static func topMargin(_ inset: CGFloat) -> CGFloat {
        max(inset, 20) + 8
    }

    /// Bottom margin: the home indicator's inset, or a thumb's worth on
    /// devices without one.
    private static func bottomMargin(_ inset: CGFloat) -> CGFloat {
        max(inset, 20)
    }

    /// The key window's safe-area insets. See `safeArea`.
    private static func windowSafeArea() -> UIEdgeInsets {
        UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
            .first { $0.isKeyWindow }?
            .safeAreaInsets ?? .zero
    }

    /// Shown when the map is following (tap toggles course-up) or the chart
    /// is turned (tap returns to north).
    private var compassShown: Bool { model.gpsMode || model.isChartTurned }

    private var compassLabel: String {
        guard model.gpsMode else { return "Face north" }
        return model.courseUp ? "North up" : "Course up"
    }

    /// The ride bar shown in GPS mode: elapsed, speed and odometer on the
    /// left; distance to go and ETA on the right when there is a route. Each
    /// cluster stacks vertically so a growing number moves only itself. An
    /// invalid field shows an en-dash, never a zero, so a stopped bike and a
    /// silent receiver read differently.
    @ViewBuilder
    private func rideBar(top: CGFloat) -> some View {
        VStack {
            HStack(alignment: .top) {
                rideCluster(alignment: .leading) {
                    Label(Self.elapsedText(model.trip), systemImage: "stopwatch")
                    Label(speedText(model.trip), systemImage: "speedometer")
                    Label(odometerText(model.trip), systemImage: "arrow.forward")
                }
                Spacer(minLength: 8)
                if model.trip?.hasRemaining == true {
                    rideCluster(alignment: .trailing) {
                        Label(remainingText(model.trip), systemImage: "flag.checkered")
                        Label(Self.etaText(model.trip), systemImage: "clock")
                    }
                    .measuredHeight(RideBarTrailingHeightKey.self)
                }
            }
            .padding(.horizontal, 12)
            // Measured before the top padding so the height is the bar alone.
            .measuredHeight(RideBarHeightKey.self)
            .padding(.top, top)
            Spacer()
        }
        .allowsHitTesting(false)
    }

    /// One cluster of the ride bar. 26 pt type: read at arm's length on a
    /// moving bicycle.
    private func rideCluster<Content: View>(
        alignment: HorizontalAlignment,
        @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: alignment, spacing: 6) {
            content()
        }
        .font(.system(size: 26, weight: .medium, design: .rounded))
        // Digits only, so a changing speed does not shuffle the labels.
        .monospacedDigit()
        .labelStyle(.titleAndIcon)
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 20,
                                                        style: .continuous))
    }

    private static func elapsedText(_ trip: PPTrip?) -> String {
        guard let trip, trip.hasElapsed else { return "—" }
        let total = Int(trip.elapsedSeconds.rounded())
        let (h, m, sec) = (total / 3600, (total % 3600) / 60, total % 60)
        // Hours appear only once there are some.
        if h > 0 { return String(format: "%d:%02d:%02d", h, m, sec) }
        return String(format: "%d:%02d", m, sec)
    }

    /// Instance methods because they read `display.units`. The underlying
    /// numbers are metres and metres per second.
    private func speedText(_ trip: PPTrip?) -> String {
        guard let trip, trip.hasSpeed else { return "—" }
        return display.units.speed(trip.speedMetersPerSecond)
    }

    private func odometerText(_ trip: PPTrip?) -> String {
        guard let trip, trip.hasOdometer else { return "—" }
        return display.units.distance(trip.odometerMeters)
    }

    private func remainingText(_ trip: PPTrip?) -> String {
        guard let trip, trip.hasRemaining else { return "—" }
        return display.units.distance(trip.remainingMeters)
    }

    /// Arrival as a time of day rather than a countdown. The trip computer
    /// withdraws it below half walking pace.
    private static func etaText(_ trip: PPTrip?) -> String {
        guard let trip, trip.hasEta else { return "—" }
        let date = Date(timeIntervalSince1970: trip.etaTimestamp)
        return date.formatted(date: .omitted, time: .shortened)
    }

    /// The notice banner. Body type, since it carries the sentences that most
    /// need reading. Hit-testable only when the notice opens Settings, so it
    /// is not a hole in the chart the rest of the time.
    private func noticeBanner(_ notice: MapNotice) -> some View {
        VStack {
            Spacer()
            Group {
                if notice.opensSettings {
                    Button(action: openSettings) {
                        noticeLabel(notice.text)
                    }
                    .buttonStyle(.plain)
                } else {
                    noticeLabel(notice.text)
                }
            }
            .padding(.horizontal, 32)
            // Above both rows of controls, from the same constants they are
            // laid out with.
            .padding(.bottom, Self.noticeBottom)
        }
        .allowsHitTesting(notice.opensSettings)
        .transition(.opacity)
    }

    /// The capsule, shared so a tappable notice looks like any other.
    private func noticeLabel(_ text: String) -> some View {
        Text(text)
            .font(.body)
            .multilineTextAlignment(.center)
            // A `Button` would tint this with the accent colour, which on a
            // sentence reads as a link.
            .foregroundStyle(.primary)
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(.regularMaterial, in: Capsule())
    }

    private static let noticeBottom =
        ControlMetrics.bottom + ControlMetrics.diameter * 2
            + ControlMetrics.spacing + 16

    /// Opens this app's own page in Settings.
    private func openSettings() {
        guard let url = URL(string: UIApplication.openSettingsURLString) else {
            return
        }
        openURL(url)
    }

    // Developer readout, DEBUG only.
    #if DEBUG

    private func statsOverlay(top: CGFloat) -> some View {
        VStack {
            // The receiver tier goes on the status line rather than the
            // ownship line: it is a fact about the receiver, and it is true
            // when no fix has arrived for minutes.
            Text(model.status + (model.receiverIsCoarse ? " · coarse" : "")
                 + Self.ownshipLine(model.ownship))
                .font(.system(size: 11, design: .monospaced))
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(.thinMaterial, in: Capsule())
                // Below the ride bar when there is one.
                .padding(.top, rideBarHeight > 0 ? top + rideBarHeight + 12 : top)
            Spacer()
        }
        .allowsHitTesting(false)
    }

    /// The ownship line: position, the stamped screen angle (not the heading;
    /// on a turned chart they differ by the turn), whether that angle was
    /// reported or derived, speed, and the road snap.
    private static func ownshipLine(_ ownship: PPOwnship?) -> String {
        guard let ownship else { return "\nno fix" }
        let snap = ownship.isSnappedToRoad
            ? " · snap\(ownship.roadName.isEmpty ? "" : " \(ownship.roadName)")"
            : ""
        return String(
            format: "\n%.5f,%.5f · %03.0f° %@%@%@", ownship.coordinate.latitude,
            ownship.coordinate.longitude, ownship.screenAngleDegrees,
            ownship.hasHeading ? (ownship.headingIsReported ? "rep" : "der") : "—",
            ownship.hasSpeed
                ? String(format: " · %.1f m/s", ownship.speedMetersPerSecond) : "",
            snap)
    }

    #endif  // DEBUG

    private func failureOverlay(_ text: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle")
                .font(.largeTitle)
            Text(text)
                .multilineTextAlignment(.center)
                .font(.callout)
        }
        .padding(24)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
        .padding(32)
    }
}

// MARK: - Layout constants

/// Geometry of the floating controls, in one place so views that must clear
/// one another (the notice above the control stack, say) share the numbers.
enum ControlMetrics {
    /// Diameter of every round control.
    static let diameter: CGFloat = 56
    /// Gap between two controls, in either direction.
    static let spacing: CGFloat = 12
    /// From the bottom of the screen to the bottom row.
    static let bottom: CGFloat = 28
    /// From either side of the screen to the outermost control.
    static let side: CGFloat = 24
}

/// Heights of the ride bar and of its right-hand cluster. Two keys because
/// the developer readout clears the whole bar and the compass clears only the
/// right cluster. `max` so the result does not depend on traversal order.
private struct RideBarHeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

private struct RideBarTrailingHeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

private extension View {
    /// Reports this view's height up the tree under `key`. A `background`
    /// rather than an `overlay` so the measuring view never takes a touch.
    func measuredHeight<K: PreferenceKey>(_ key: K.Type) -> some View
    where K.Value == CGFloat {
        background(
            GeometryReader { proxy in
                Color.clear.preference(key: key, value: proxy.size.height)
            })
    }
}

/// The compass control. Its own view rather than a `CircleButton` because
/// the glyph turns. The needle points at north, not the heading, so one
/// control serves both modes: it swings in course-up and sits still in
/// north-up.
struct CompassButton: View {
    /// Where north is on screen, degrees clockwise from up.
    let northAngleDegrees: Double
    let isActive: Bool
    let label: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: "location.north.line.fill")
                .font(.system(size: 22, weight: .medium))
                // Smooths the compass appearing mid-turn and the one-frame
                // steps a slow render leaves.
                .rotationEffect(.degrees(northAngleDegrees))
                .animation(.linear(duration: 0.15), value: northAngleDegrees)
                .frame(width: ControlMetrics.diameter,
                       height: ControlMetrics.diameter)
                .background(.regularMaterial, in: Circle())
                .overlay(Circle().strokeBorder(.separator, lineWidth: 0.5))
        }
        .tint(isActive ? .accentColor : .primary)
        .accessibilityLabel(label)
        .shadow(color: .black.opacity(0.18), radius: 6, y: 2)
    }
}

/// The face shared by every floating control: 56 pt of system material with a
/// hairline and a 22 pt symbol. Separate from `CircleButton` because
/// `MapMenuButton` is a `Menu`, not a `Button`. `CompassButton` keeps its own
/// copy because its rotation sits between the image and the frame.
struct CircleControlFace: View {
    let systemName: String

    var body: some View {
        Image(systemName: systemName)
            .font(.system(size: 22, weight: .medium))
            .frame(width: ControlMetrics.diameter,
                   height: ControlMetrics.diameter)
            .background(.regularMaterial, in: Circle())
            .overlay(Circle().strokeBorder(.separator, lineWidth: 0.5))
    }
}

/// A round floating map control with an optional active state and long
/// press. `activeSystemName` exists because appending `.fill` is only a
/// convention: a symbol without a filled twin would render blank.
struct CircleButton: View {
    let systemName: String
    let label: String
    var isActive: Bool = false
    /// The symbol to show while active. Nil means `"\(systemName).fill"`.
    var activeSystemName: String? = nil
    /// The tint while active. Nil is the accent colour.
    var activeTint: Color? = nil
    /// Long-press action, or nil for a tap-only button.
    var onLongPress: (() -> Void)? = nil
    /// VoiceOver name for the long press, published as an accessibility action.
    var longPressLabel: String? = nil
    let action: () -> Void

    /// Whether the current press has already been handled as a hold.
    @State private var longPressHandled = false
    /// Whether a finger is down; scopes `longPressHandled` to one press.
    @State private var pressActive = false

    var body: some View {
        core
            .tint(isActive ? (activeTint ?? .accentColor) : .primary)
            .accessibilityLabel(label)
            .shadow(color: .black.opacity(0.18), radius: 6, y: 2)
    }

    /// Tap and hold on one control. A `Button` fires on touch-up regardless
    /// of what came before, so a hold raises `longPressHandled` and the tap
    /// steps over it. The flag is cleared on press-begin rather than on a
    /// timer: a timed flag let a hold longer than the timeout fall through to
    /// the tap. `DragGesture(minimumDistance: 0)` is how a touch-down is
    /// observed in SwiftUI; `pressActive` limits the reset to the first
    /// change of a press.
    @ViewBuilder
    private var core: some View {
        let button = Button {
            if longPressHandled { return }
            action()
        } label: {
            CircleControlFace(systemName: isActive
                                ? (activeSystemName ?? "\(systemName).fill")
                                : systemName)
        }
        // Simultaneous rather than `.onLongPressGesture`, which would replace
        // the button's own gesture and its press highlight. 0.4 s is the
        // system long-press duration; Haptic Touch arrives the same way.
        .simultaneousGesture(
            DragGesture(minimumDistance: 0)
                .onChanged { _ in
                    guard !pressActive else { return }
                    pressActive = true
                    longPressHandled = false
                }
                .onEnded { _ in pressActive = false })
        .simultaneousGesture(
            LongPressGesture(minimumDuration: 0.4).onEnded { _ in
                guard let onLongPress else { return }
                longPressHandled = true
                // The haptic is the only immediate feedback that the hold fired.
                UIImpactFeedbackGenerator(style: .medium).impactOccurred()
                onLongPress()
            })

        if let onLongPress, let longPressLabel {
            button.accessibilityAction(named: Text(longPressLabel), onLongPress)
        } else {
            button
        }
    }
}
