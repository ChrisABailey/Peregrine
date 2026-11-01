// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI
import UIKit
import UniformTypeIdentifiers

/// The app's one screen: a full-bleed map with controls floating in the safe
/// area over it.
///
/// The screen has three mutually exclusive states for the route flow (map,
/// map with sheet, map with crosshair) and the same three again for the point
/// flow. The crosshair state exists because "Pick on map" must dismiss the
/// sheet, so the drafts are owned here rather than by the sheets. The rides,
/// problem-report and about sheets are about the app rather than the map and
/// do not join that exclusion.
///
/// At regular width (an iPad) the route, point and legend views sit in one
/// floating card at the top leading corner instead of in sheets, sized to
/// its content and collapsible to one line. The card stays up through a
/// pick and a waypoint drag. See `toolCard`.
struct MapScreen: View {
    @StateObject private var model = MapModel()
    @StateObject private var draft = RouteDraft()
    @Environment(\.displayScale) private var displayScale
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.colorScheme) private var colorScheme
    @Environment(\.horizontalSizeClass) private var sizeClass
    /// Opens Settings for a notice that offers it. See `openSettings()`.
    @Environment(\.openURL) private var openURL

    /// Measured heights of the ride bar and of its right-hand cluster,
    /// reported by the bar via preference keys. The developer readout clears
    /// the whole bar; the compass clears only the right cluster. Zero when
    /// there is no bar.
    @State private var rideBarHeight: CGFloat = 0
    @State private var rideBarTrailingHeight: CGFloat = 0
    @State private var guidanceBannerHeight: CGFloat = 0

    /// Built once in `onAppear` from the pack's fixed step list.
    @State private var symbolStepLabels: [String] = []

    /// The window's safe-area insets. Read from UIKit rather than the
    /// `GeometryProxy`, which reports zero here because the reader has
    /// `.ignoresSafeArea()` on it (the map surface must be the whole screen).
    /// Re-read on every size change, which covers rotation on the iPad.
    @State private var safeArea: UIEdgeInsets = .zero
    /// The tool card is folded to its summary line.
    @State private var cardCollapsed = false
    /// The card content's natural height, measured where the OS can (iOS 18);
    /// nil means the card takes its maximum height.
    @State private var cardContentHeight: CGFloat?
    /// A waypoint drag began with the route card up; the card's stops are
    /// reloaded from the replanned route when it lands.
    @State private var draftFollowsDrag = false
    /// A received route waiting for the rider to agree to replace theirs:
    /// the local copy `RouteShare.receive` made.
    @State private var pendingRoute: URL?

    @State private var sheetShown = false
    /// Which draft row the crosshair is filling, or nil when it is not up.
    @State private var pickingIndex: Int?
    /// The tap pick running in place of the crosshair, or nil.
    @State private var tapPick: TapPick?

    /// Point flow state: `infoShown` is a tap on a marker, `editing` is the
    /// form, `placingPoint` is the crosshair the form dismisses into. The
    /// draft lives here because "Move on map" destroys the sheet.
    @State private var infoShown = false
    @State private var editing: PointDraft?
    @State private var placingPoint: PointDraft?

    @State private var ridesShown = false
    /// The rides sheet opened from Export, so it leads with the route.
    @State private var exportShown = false
    @State private var problemShown = false
    @State private var aboutShown = false
    @State private var tidesShown = false
    @State private var legendShown = false
    /// The rows the map key opens on.
    @State private var legendScope: LegendSheet.Scope = .onMap
    /// The map key over the route sheet, opened from its preferences chip.
    @State private var preferencesShown = false
    @State private var navigationNoticeShown = NavigationNotice.isNeeded(flavor: .current)
    @State private var chartImporterShown = false
    @State private var settingsShown = false
    @State private var settingsDetent: PresentationDetent = .medium
    /// Natural height of the settings form, measured on first display.
    @State private var settingsFit: CGFloat?

    private static let isPad = UIDevice.current.userInterfaceIdiom == .pad

    /// Medium and large, plus on iPad a stop at the form's natural height.
    private var settingsDetents: Set<PresentationDetent> {
        guard Self.isPad, let fit = settingsFit else { return [.medium, .large] }
        return [.medium, .height(fit), .large]
    }
    @State private var chartHelpShown = false
    @State private var chartDownloadShown = false
    @State private var aisSettingsShown = false
    /// The chart features under the last tap; non-empty while their sheet is up.
    @State private var chartFeatures: [PPChartFeatureInfo] = []

    /// The unit preference. `@ObservedObject` because this view does not own
    /// it; `DisplayUnits.shared` is also observed from views across sheet
    /// boundaries.
    @ObservedObject private var display = DisplayUnits.shared

    var body: some View {
        GeometryReader { geo in
            // The map is full-bleed, so everything floating over it keeps
            // clear of the status bar and home indicator by hand.
            let top = Self.topMargin(safeArea.top)
            let bottom = Self.bottomMargin(safeArea.bottom)
            ZStack {
                // Everything that draws per-frame state reads it through a
                // `LiveMapReader`, so a frame does not re-run this body.
                LiveMapReader(model.live) { mapLayer(size: geo.size) }
                MapGestureView(
                    onBegan: { model.gestureBegan() },
                    onEnded: { model.gestureEnded(releaseVelocity: $0) },
                    isCoasting: { model.isCoasting },
                    onStopCoast: { model.stopCoast() },
                    onPan: { model.pan(by: $0, velocity: $1) },
                    onZoom: { model.zoom(by: $0, about: $1) },
                    onTap: { handleTap(at: $0) },
                    onRotate: { model.rotate(by: $0, about: $1) },
                    onHoldBegan: { point, grabbed in
                        handleHold(at: point, then: grabbed)
                    },
                    // At most one of each pair holds anything; the other
                    // returns at its guard.
                    onHoldMoved: {
                        model.dragRouteWaypoint(to: $0)
                        model.dragPoint(to: $0)
                    },
                    onHoldEnded: {
                        draftFollowsDrag = (sheetShown || tapPick != nil)
                            && model.draggingWaypoint != nil
                        model.dropRouteWaypoint(at: $0)
                        model.dropPoint(at: $0)
                    },
                    onHoldCancelled: {
                        model.cancelRouteWaypointDrag()
                        model.cancelPointDrag()
                    },
                    onHoldReleased: { beginPressedPoint(at: $0) },
                    holdSeconds: model.routeWaypointHoldSeconds)
                if let index = pickingIndex {
                    pickOverlay(index: index, top: top, bottom: bottom)
                } else if let pick = tapPick {
                    tapPickOverlay(pick, top: top, bottom: bottom)
                } else if let draft = placingPoint {
                    pointPlaceOverlay(draft: draft, top: top, bottom: bottom)
                } else {
                    controls(top: top)
                }
                if model.gpsMode {
                    LiveMapReader(model.live) { rideBar(top: top) }
                }
                LiveMapReader(model.live) {
                    if let course = model.courseDeviation {
                        courseIndicator(course, top: top)
                    } else if let guidance = model.guidance {
                        guidanceBanner(guidance, top: top)
                    }
                }
                if cardShown { toolCard(top: top, size: geo.size) }
                if let progress = model.chartImportProgress {
                    noticeBanner(MapNotice(text: "Importing charts… \(Int(progress * 100))%"))
                } else if let download = model.chartDownload {
                    noticeBanner(MapNotice(text: ChartDownloadText.progress(download.region,
                                                                            fraction: download.fraction)))
                } else if let notice = model.notice {
                    noticeBanner(notice)
                }
                #if DEBUG
                if MapModel.showsStats {
                    LiveMapReader(model.live) { statsOverlay(top: top) }
                }
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
            .onPreferenceChange(GuidanceBannerHeightKey.self) { height in
                guidanceBannerHeight = height
            }
            .onAppear {
                safeArea = Self.windowSafeArea()
                symbolStepLabels = Self.symbolStepLabels(model.symbolZoomSteps)
                model.resize(to: geo.size, scale: displayScale)
                model.recoverInterruptedRide()
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
            .onChange(of: scenePhase, initial: true) { _, phase in
                model.setScenePhase(SceneVisibility(phase))
            }
            // The drag replans on the render queue; the card's stops follow
            // once the new route is published.
            .onChange(of: model.isPlanning) { _, planning in
                guard !planning, draftFollowsDrag else { return }
                draftFollowsDrag = false
                draft.load(from: model.route)
            }
            .onChange(of: cardShown) { _, shown in
                if !shown { cardCollapsed = false }
            }
            // Another tool in the card opens expanded and is measured afresh.
            .onChange(of: cardSummaryKind) { _, _ in
                cardCollapsed = false
                cardContentHeight = nil
            }
            .onChange(of: colorScheme, initial: true) { _, scheme in
                model.setDarkMap(scheme == .dark)
            }
            .onChange(of: geo.size) { _, size in
                // A size change is the one moment the insets can change too.
                safeArea = Self.windowSafeArea()
                model.resize(to: size, scale: displayScale)
            }
            // `pippin://place?lat=…&lon=…` from the share extension, or a
            // `.fvrte` route from another device, whether the app was running
            // or launched by it.
            .onOpenURL { url in
                if RouteShare.isRouteFile(url) {
                    receiveRoute(from: url)
                } else {
                    acceptSharedPlace(from: url)
                }
            }
            .alert("Replace your route?",
                   isPresented: Binding(get: { pendingRoute != nil },
                                        set: { if !$0 { pendingRoute = nil } }),
                   presenting: pendingRoute) { local in
                Button("Replace", role: .destructive) { importRoute(local) }
                Button("Cancel", role: .cancel) {
                    try? FileManager.default.removeItem(at: local)
                }
            } message: { _ in
                Text("The route you received replaces the one on the map.")
            }
            .alert(NavigationNotice.title, isPresented: $navigationNoticeShown) {
                Button(NavigationNotice.acknowledge) {
                    UserDefaults.standard.set(true, forKey: NavigationNotice.acknowledgedKey)
                }
            } message: {
                Text(NavigationNotice.body)
            }
            // A `.sheet` is a new presentation and does not inherit this
            // view's environment values, so each one gets them explicitly.
            .sheet(isPresented: sheetOnly($sheetShown)) {
                routeSheet
                    .environment(\.describePlace, model.describePlace)
                    .environment(\.searchPlaces, model.search(for:))
            }
            // Two sheet modifiers rather than one `item:`: the info sheet is
            // bound to the model's selection (re-read after edits) and the
            // editor to a draft this view owns.
            .sheet(isPresented: sheetOnly($infoShown)) { pointInfoSheet }
            .sheet(isPresented: $ridesShown) { ridesSheet }
            .fileImporter(isPresented: $chartImporterShown,
                          allowedContentTypes: [.zip]) { result in
                if case .success(let url) = result { model.importCharts(from: url) }
            }
            .sheet(isPresented: $settingsShown) {
                SettingsSheet(chart: model.chartSettings,
                              onChartChange: { model.setChartSettings($0) },
                              symbolStep: Binding(get: { model.symbolStep },
                                                  set: { model.setSymbolStep($0) }),
                              symbolStepLabels: symbolStepLabels,
                              onClose: { settingsShown = false })
                    .modifier(ContentHeightReader(height: $settingsFit))
                    .presentationDetents(settingsDetents, selection: $settingsDetent)
                    .presentationBackgroundInteraction(.enabled(upThrough: .medium))
                    .onChange(of: settingsFit) { old, new in
                        // Follow the measurement while the card sits at its fitted
                        // height; the first one replaces the provisional .large.
                        guard Self.isPad, let new else { return }
                        let atFit = old.map { settingsDetent == .height($0) }
                            ?? (settingsDetent == .large)
                        if atFit { settingsDetent = .height(new) }
                    }
            }
            .sheet(isPresented: $chartDownloadShown) {
                ChartDownloadSheet(active: model.chartDownload?.region,
                                   resumable: model.resumableChartRegions,
                                   updateRegion: model.chartUpdateRegion,
                                   busy: model.chartImportProgress != nil,
                                   onChoose: { region in
                                       model.downloadCharts(region)
                                       chartDownloadShown = false
                                   },
                                   onCancel: { model.cancelChartDownload() },
                                   onClose: { chartDownloadShown = false })
                    .presentationDetents([.medium, .large])
            }
            .sheet(isPresented: $aisSettingsShown) {
                AisSettingsSheet(receiver: model.aisReceiver,
                                 feed: model.aisFeed,
                                 trafficOn: model.aisTraffic,
                                 openWaters: Binding(get: { model.aisOpenWaters },
                                                     set: { model.setAisOpenWaters($0) }),
                                 onApply: { model.setAisReceiver($0) },
                                 onClose: { aisSettingsShown = false })
                    .presentationDetents([.medium, .large])
            }
            .sheet(isPresented: $chartHelpShown) {
                ChartHelpSheet(onClose: { chartHelpShown = false })
                    .presentationDetents([.medium, .large])
            }
            // Medium with the map live above, so another tap re-queries in place.
            .sheet(isPresented: Binding(get: { !chartFeatures.isEmpty },
                                        set: { if !$0 { chartFeatures = [] } })) {
                ChartFeatureSheet(features: chartFeatures,
                                  onClose: { chartFeatures = [] })
                    .presentationDetents([.medium, .large])
                    .presentationBackgroundInteraction(.enabled(upThrough: .medium))
            }
            .sheet(isPresented: $problemShown) { problemSheet }
            .sheet(isPresented: $aboutShown) {
                AboutSheet(onClose: { aboutShown = false })
            }
            .sheet(isPresented: $tidesShown) {
                if let tide = model.tide {
                    TideCard(tide: tide, onClose: { tidesShown = false })
                }
            }
            // Medium with the map live above it, so a pinch redraws the swatches.
            .sheet(isPresented: sheetOnly($legendShown)) {
                LegendSheet(model: model, scope: legendScope, onClose: { legendShown = false })
                    .presentationDetents([.medium, .large])
                    .presentationBackgroundInteraction(.enabled(upThrough: .medium))
            }
            .sheet(item: editingSheet) { draft in
                pointEditSheet(draft)
                    .environment(\.describePlace, model.describePlace)
                    .environment(\.searchPlaces, model.search(for:))
            }
        }
        .ignoresSafeArea()
    }

    // MARK: - Tool card

    /// Card width: a phone sheet's width, less on a narrow window.
    private static let cardWidth: CGFloat = 360
    /// Room kept below the card for the two rows of round buttons.
    private static let cardBottomClearance =
        ControlMetrics.bottom + 2 * ControlMetrics.diameter + 2 * ControlMetrics.spacing

    /// True when the map tools use the floating card rather than sheets.
    private var usesToolCard: Bool { sizeClass == .regular }

    /// Up while any map tool is open, including through a pick.
    private var cardShown: Bool {
        usesToolCard && (sheetShown || infoShown || editing != nil
            || legendShown || pickingIndex != nil || tapPick != nil
            || placingPoint != nil)
    }

    /// `shown`, but false while the card is in use, so a tool is presented
    /// by exactly one of the card and its sheet.
    private func sheetOnly(_ shown: Binding<Bool>) -> Binding<Bool> {
        Binding(get: { !usesToolCard && shown.wrappedValue },
                set: { shown.wrappedValue = $0 })
    }

    /// `editing` for the editor's sheet; nil while the card is in use.
    private var editingSheet: Binding<PointDraft?> {
        Binding(get: { usesToolCard ? nil : editing },
                set: { editing = $0 })
    }

    /// The floating card: below any top bars, clear of the bottom buttons,
    /// and as tall as its content within that.
    private func toolCard(top: CGFloat, size: CGSize) -> some View {
        let cardTop = Self.belowBars(top: top, model.gpsMode ? rideBarHeight : 0,
                                     guidanceBannerHeight)
        let maxHeight = max(160, size.height - cardTop - Self.cardBottomClearance)
        let height = min(cardContentHeight ?? maxHeight, maxHeight)
        let shape = RoundedRectangle(cornerRadius: 20, style: .continuous)
        return VStack(spacing: 0) {
            if cardCollapsed {
                Button { cardCollapsed = false } label: {
                    HStack {
                        Text(cardSummary).lineLimit(1)
                        Spacer()
                        Image(systemName: "chevron.compact.down")
                    }
                    .padding(.horizontal, 16)
                    .padding(.vertical, 14)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Expand \(cardSummary)")
            } else {
                cardContent
                    .environment(\.describePlace, model.describePlace)
                    .environment(\.searchPlaces, model.search(for:))
                    .modifier(ContentHeightReader(height: $cardContentHeight))
                    .frame(height: height)
                Button { cardCollapsed = true } label: {
                    Image(systemName: "chevron.compact.up")
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 6)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Collapse")
            }
        }
        .frame(width: min(Self.cardWidth, size.width - 2 * ControlMetrics.side))
        .background(Color(.systemGroupedBackground), in: shape)
        .clipShape(shape)
        .shadow(color: .black.opacity(0.18), radius: 10, y: 2)
        .padding(.leading, ControlMetrics.side)
        .padding(.top, cardTop)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    /// Which tool the card shows, for re-measuring when it changes.
    private var cardSummaryKind: Int {
        if editing != nil || placingPoint != nil { return 1 }
        if infoShown { return 2 }
        if sheetShown || pickingIndex != nil || tapPick != nil { return 3 }
        return legendShown ? 4 : 0
    }

    /// The collapsed card's one line: the route's status, or the open tool.
    private var cardSummary: String {
        if editing != nil || placingPoint != nil { return "Edit pin" }
        if infoShown { return model.selectedPoint?.name ?? "Pin" }
        if sheetShown || pickingIndex != nil || tapPick != nil {
            guard let route = model.route, route.exists else { return "Route" }
            return RouteSheet.statusText(route, units: display.units)
        }
        return "Map key"
    }

    /// The topmost open tool. A point opened over the route covers it, and
    /// closing the point uncovers the route. A tool waiting on a pick is
    /// shown but disabled; the crosshair's own buttons finish the pick.
    @ViewBuilder
    private var cardContent: some View {
        // Empty while the sheets are in use, so no tool is built twice.
        if !usesToolCard {
            EmptyView()
        } else if let draft = placingPoint {
            pointEditSheet(draft).id(draft.id).disabled(true)
        } else if let draft = editing {
            pointEditSheet(draft).id(draft.id)
        } else if infoShown {
            pointInfoSheet
        } else if sheetShown || pickingIndex != nil || tapPick != nil {
            routeSheet.disabled(pickingIndex != nil || tapPick != nil)
        } else if legendShown {
            LegendSheet(model: model, scope: legendScope, onClose: { legendShown = false })
        }
    }

    /// Opens the map key. In the card it replaces the route and point
    /// info, which would otherwise cover it.
    private func showLegend(scope: LegendSheet.Scope = .onMap) {
        if usesToolCard, editing == nil {
            sheetShown = false
            infoShown = false
        }
        legendScope = scope
        legendShown = true
    }

    // MARK: - Route flow

    private var routeSheet: some View {
        RouteSheet(
            draft: draft,
            route: model.route,
            isPlanning: model.isPlanning,
            currentLocation: model.currentLocation,
            profileNames: model.routeProfileNames,
            beachAvailable: model.beachAvailable,
            elevationAvailable: model.elevationAvailable,
            tide: model.tide,
            straightLegs: model.routeLegsAreStraight,
            search: model.search(for:),
            initialSearchText: model.lastSearchText,
            onSearched: { model.rememberSearch($0) },
            onPick: { index in
                sheetShown = false
                if AppFlavor.current.placesStopsByTap {
                    tapPick = .stop(index)
                } else {
                    pickingIndex = index
                }
            },
            onAddOnMap: AppFlavor.current.placesStopsByTap ? {
                sheetShown = false
                tapPick = .append(route: model.route, stops: draft.stops)
            } : nil,
            onFrame: { result in
                model.frame(result)
            },
            onSubmit: {
                model.setRoute(waypoints: draft.waypoints(), profile: draft.profile,
                               beachUse: draft.beachUse, avoidHills: draft.avoidHills)
                // Apply in the card: the route redraws beside the open tool.
                if !usesToolCard { sheetShown = false }
            },
            // Walk/Cycle replans the live route's waypoints on the spot, not
            // the draft's: a half-edited draft must not reach the document.
            // No-op with no route on the map.
            onProfileChange: { profile in
                guard let route = model.route, route.exists else { return }
                model.setRoute(waypoints: route.waypoints, profile: profile)
            },
            // Replans the live route like the mode picker, and becomes the
            // setting a new route starts with.
            onBeachChange: { beachUse in
                RouteDraft.lastBeachUse = beachUse
                guard let route = model.route, route.exists else { return }
                model.setRoute(waypoints: route.waypoints, profile: route.profile,
                               beachUse: beachUse)
            },
            onHillsChange: { avoidHills in
                RouteDraft.lastAvoidHills = avoidHills
                guard let route = model.route, route.exists else { return }
                model.setRoute(waypoints: route.waypoints, profile: route.profile,
                               beachUse: route.beachUse, avoidHills: avoidHills)
            },
            slopeColors: model.routeSlopeColors,
            onSlopeColorsChange: { model.setRouteSlopeColors($0) },
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
            },
            appliesInPlace: usesToolCard,
            adjustedCount: model.adjustedSettingCount(profile: draft.profile),
            // In the card the key replaces the route tool; on a phone it opens
            // over the route sheet, which stays put for the re-planned route.
            onOpenPreferences: model.hasLegend ? {
                if usesToolCard { showLegend(scope: .adjusted) } else { preferencesShown = true }
            } : nil,
            shareRoute: {
                guard let document = MapModel.routeDocumentURL() else { return nil }
                return RouteShare.payload(document: document,
                                          destination: draft.stops.last?.name)
            })
        .presentationDetents([.medium, .large])
        .sheet(isPresented: $preferencesShown) {
            LegendSheet(model: model, scope: .adjusted, onClose: { preferencesShown = false })
                .presentationDetents([.medium, .large])
                .presentationBackgroundInteraction(.enabled(upThrough: .medium))
        }
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

    /// The tap-pick state for route stops: the map keeps its gestures and a
    /// tap places. Appended stops go on the live route as they land, so the
    /// legs are drawn while the next one is chosen.
    private func tapPickOverlay(_ pick: TapPick, top: CGFloat,
                                bottom: CGFloat) -> some View {
        switch pick {
        case .stop(let index):
            return TapPickOverlay(
                title: "Tap the map to place \(draft.label(at: index))",
                summary: nil, topMargin: top, bottomMargin: bottom,
                onUndo: nil, canUndo: false, onDone: nil,
                onCancel: {
                    tapPick = nil
                    sheetShown = true
                })
        case .append(let route, let stops):
            return TapPickOverlay(
                title: "Tap the map to add stops",
                summary: model.route.flatMap { r in
                    r.exists ? RouteSheet.statusText(r, units: display.units) : nil
                },
                topMargin: top, bottomMargin: bottom,
                onUndo: {
                    draft.removeLastPlaced()
                    applyTappedDraft()
                },
                canUndo: draft.hasAnything,
                onDone: {
                    tapPick = nil
                    sheetShown = true
                },
                onCancel: {
                    tapPick = nil
                    draft.stops = stops
                    if let route, route.exists {
                        model.setRoute(waypoints: route.waypoints, profile: route.profile)
                    } else {
                        model.clearRoute()
                    }
                    sheetShown = true
                })
        }
    }

    /// Places a tap for the running tap pick.
    private func placeByTap(_ pick: TapPick, at location: CGPoint) {
        model.tapTarget(at: location) { coordinate, name in
            // The answer comes a hop later; the pick may have ended.
            guard let coordinate, tapPick == pick else { return }
            UISelectionFeedbackGenerator().selectionChanged()
            switch pick {
            case .stop(let index):
                if draft.stops.indices.contains(index) {
                    draft.stops[index].coordinate = coordinate
                    draft.stops[index].name = name
                }
                tapPick = nil
                sheetShown = true
            case .append:
                draft.appendTapped(coordinate, named: name)
                applyTappedDraft()
            }
        }
    }

    /// Puts the draft's placed stops on the map as the route.
    private func applyTappedDraft() {
        let waypoints = draft.waypoints()
        if waypoints.isEmpty {
            model.clearRoute()
        } else {
            model.setRoute(waypoints: waypoints, profile: draft.profile)
        }
    }

    // MARK: - Point flow

    /// A tap on the map: a marker under the finger selects it and opens the
    /// info sheet; a miss with a sheet open closes it. A miss with nothing
    /// open lists the chart features there on a chart pack, and otherwise
    /// does nothing.
    private func handleTap(at location: CGPoint) {
        if let pick = tapPick {
            placeByTap(pick, at: location)
            return
        }
        // During a placement the map is being dragged, not tapped.
        guard pickingIndex == nil, placingPoint == nil, editing == nil else {
            return
        }
        model.point(under: location) { hit in
            if let hit {
                chartFeatures = []
                model.select(hit)
                infoShown = true
            } else if infoShown {
                infoShown = false
                model.select(nil)
            } else if model.importsCharts {
                showChartFeatures(at: location)
            }
        }
    }

    /// Lists the chart features under a tap that hit no marker; a tap on
    /// uncharted ground closes the list.
    private func showChartFeatures(at location: CGPoint) {
        model.chartFeatures(under: location) { features in
            guard pickingIndex == nil, placingPoint == nil, editing == nil else { return }
            chartFeatures = features
        }
    }

    /// A press held on the map. Tries to grab a route waypoint, then a pin,
    /// and tells the recognizer whether the rest of the touch belongs to the
    /// grabbed thing; a press that grabs nothing starts a new point on
    /// release. The waypoint wins because waypoints snap onto pins and the
    /// route is the thing being edited. Refused during a placement and while
    /// the route sheet (the other editor of the same waypoints) covers the
    /// map. The card leaves the map in reach, so a drag is allowed there and
    /// the card's stops reload from the result, discarding edits not yet
    /// applied.
    private func handleHold(at location: CGPoint,
                            then grabbed: @escaping (Bool) -> Void) {
        guard pickingIndex == nil, placingPoint == nil, editing == nil,
              !sheetShown || usesToolCard else {
            grabbed(false)
            return
        }
        if case .stop = tapPick {
            grabbed(false)
            return
        }
        model.grabRouteWaypoint(at: location) { onWaypoint in
            // While stops are being tapped on, only the route is editable.
            guard !onWaypoint, tapPick == nil else { return grabbed(onWaypoint) }
            model.grabPoint(at: location, then: grabbed)
        }
    }

    /// Opens the editor on a new point at a long-pressed spot, prefilled from
    /// the named map feature drawn there. Nothing is added until Save.
    private func beginPressedPoint(at location: CGPoint) {
        model.chartFeature(under: location) { coordinate, feature in
            // The answer comes a hop later; the state may have moved on.
            guard let coordinate, pickingIndex == nil, tapPick == nil,
                  placingPoint == nil, editing == nil,
                  !sheetShown || usesToolCard else { return }
            UIImpactFeedbackGenerator(style: .medium).impactOccurred()
            infoShown = false
            model.select(nil)
            let pin = model.pointSymbols.first {
                $0.name == MapModel.sharedPlaceSymbol
            }
            let draft = PointDraft(newAt: coordinate, feature: feature,
                                   symbolId: pin?.symbolId ?? 0,
                                   sizePx: model.points.first?.sizePx ?? 22)
            editing = draft
            // The tiles carry road names in a label layer the pick index does
            // not hold, so a press on a road names it through the road graph.
            guard feature?.name.isEmpty ?? true else { return }
            Task {
                guard let place = await model.describePlace(at: coordinate),
                      place.isNamed, draft.name.isEmpty else { return }
                draft.name = place.name
            }
        }
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
        guard pickingIndex == nil, tapPick == nil, placingPoint == nil,
              editing == nil else {
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

    // MARK: - Shared routes

    /// Takes a `.fvrte` handed to the app. Refused during a placement; asks
    /// first when there is already a route to lose.
    private func receiveRoute(from url: URL) {
        guard pickingIndex == nil, tapPick == nil, placingPoint == nil else {
            model.post("Finish placing the point first, then open the route again.")
            return
        }
        guard let local = RouteShare.receive(url) else {
            model.post("Could not open that route file.")
            return
        }
        if model.route?.exists == true {
            pendingRoute = local
        } else {
            importRoute(local)
        }
    }

    /// Imports a received route and opens the route tool on it, so the rider
    /// sees what arrived.
    private func importRoute(_ local: URL) {
        pendingRoute = nil
        model.importRoute(from: local) { imported in
            try? FileManager.default.removeItem(at: local)
            guard imported else { return }
            editing = nil
            infoShown = false
            legendShown = false
            draft.load(from: model.route)
            if draft.profile.isEmpty { draft.profile = model.routeDefaultProfile }
            sheetShown = true
        }
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
        guard pickingIndex == nil, tapPick == nil, placingPoint == nil else {
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

    /// The pack's steps are fixed for the process, so the labels are built
    /// once rather than on every body pass.
    private static func symbolStepLabels(_ steps: [Double]) -> [String] {
        steps.indices.map { index in
            index < symbolSizeNames.count
                ? symbolSizeNames[index]
                : String(format: "%.2gx", steps[index])
        }
    }

    private var problemSheet: some View {
        ReportProblemSheet(
            // The map centre, to five decimals (about a metre).
            coordinateText: model.viewport
                .map { LocationText.describe($0.center) } ?? "—",
            onClose: { problemShown = false })
    }

    // MARK: - Rides

    private var ridesSheet: some View {
        RideSheet(
            route: exportShown ? routeExport : nil,
            rides: model.rides,
            isRecording: model.isRecording,
            recordingPointCount: model.recordedPointCount,
            onToggleRecording: { model.toggleRecording() },
            onPollCount: { model.refreshRecordedCount() },
            onDelete: { model.deleteRide($0) },
            onClose: { ridesShown = false })
    }

    /// The route row of the export sheet. Without a searched destination the
    /// name carries the date, so routes on a watch do not all read "Route".
    private var routeExport: RouteExport {
        let title = draft.stops.last?.name.map { RouteShare.title(destination: $0) }
            ?? "Route " + Date().formatted(.dateTime.month(.abbreviated).day())
        return RouteExport(title: title, isReady: model.routeExportsGpx) { done in
            model.exportRouteGpx(title: title, done: done)
        }
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
        /// The layer's pixels land on the screen's, so it is shown unfiltered.
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
        // A band grows by whole pixels on each side, so one drawn at the live
        // camera, or panned from it by whole pixels, lands on the screen's.
        // A tilted screen resamples every layer whatever the band.
        return Preview(k: k, turn: turn, dx: dx, dy: dy,
                       size: drawn.sizeInPoints, scale: drawn.displayScale,
                       isLive: drawn.isPixelAligned(to: live) && live.pitchDegrees == 0)
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

    /// The ownship sprite at its position on the live camera, turned by the
    /// screen angle it was computed with plus any turn of the camera since.
    /// Placed by offset from the centre: the stack is band-sized, so its
    /// origin is not the screen's, but its centre is. On a tilted screen it is
    /// placed through the perspective, its heading follows the ground, and it
    /// shrinks with depth.
    @ViewBuilder
    private static func ship(_ ship: PPOwnship, in live: PPViewport,
                             screen: CGSize) -> some View {
        if let image = ship.symbolImage {
            let flat = live.point(forGeo: ship.coordinate)
            let flatAngle = ship.screenAngleDegrees + live.rotationDegrees
                - ship.viewRotationDegrees
            let at = live.screenPoint(forFlat: flat)
            if at.x.isFinite, at.y.isFinite {
                Image(decorative: image, scale: ship.symbolPixelsPerPoint)
                    .interpolation(.medium)
                    .rotationEffect(.degrees(
                        Self.screenAngle(flatAngle, at: flat, in: live)))
                    .scaleEffect(live.depthScale(atScreen: at))
                    .offset(x: at.x - screen.width / 2, y: at.y - screen.height / 2)
                    .allowsHitTesting(false)
            }
        }
    }

    /// The COG vector: a line from the ship to where it will be in
    /// `Course.vectorSeconds` at its present course and speed, drawn through
    /// the perspective. Nothing when the receiver reports no course or speed,
    /// or the speed is below `Course.vectorMinimumSpeed`.
    @ViewBuilder
    private static func courseVector(_ ship: PPOwnship, in live: PPViewport,
                                     screen: CGSize) -> some View {
        if ship.hasCourse, ship.hasSpeed,
           let end = Course.vectorEnd(latitude: ship.coordinate.latitude,
                                      longitude: ship.coordinate.longitude,
                                      courseDegrees: ship.courseDegrees,
                                      speedMetersPerSecond: ship.speedMetersPerSecond) {
            let from = live.screenPoint(forFlat: live.point(forGeo: ship.coordinate))
            let to = live.screenPoint(forFlat: live.point(
                forGeo: PPGeoPointMake(end.latitude, end.longitude)))
            if from.x.isFinite, from.y.isFinite, to.x.isFinite, to.y.isFinite {
                let line = Path { path in
                    path.move(to: from)
                    path.addLine(to: to)
                }
                // A white casing keeps the line readable on dark chart fills.
                ZStack {
                    line.stroke(Color.white, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                    line.stroke(Color.black, style: StrokeStyle(lineWidth: 2, lineCap: .round))
                }
                // Framed to the screen and centred, so its origin is the screen's.
                .frame(width: screen.width, height: screen.height)
                .allowsHitTesting(false)
            }
        }
    }

    /// An upright marker on a tilted map: placed at its ground point through
    /// the perspective and shrunk with depth, but never foreshortened. Placed
    /// by offset from the centre, as the ship is; off-screen ones are skipped.
    @ViewBuilder
    private static func billboard(_ board: PPBillboard, in live: PPViewport,
                                  screen: CGSize) -> some View {
        let at = live.screenPoint(forFlat: live.point(forGeo: board.coordinate))
        let w = CGFloat(board.image.width) / board.pixelsPerPoint
        let h = CGFloat(board.image.height) / board.pixelsPerPoint
        if at.x.isFinite, at.y.isFinite,
           at.x > -w, at.x < screen.width + w, at.y > -h, at.y < screen.height + h {
            let d = live.depthScale(atScreen: at)
            Image(decorative: board.image, scale: board.pixelsPerPoint)
                .interpolation(.medium)
                .scaleEffect(d)
                .offset(x: at.x + board.centerOffset.x * d - screen.width / 2,
                        y: at.y + board.centerOffset.y * d - screen.height / 2)
                .allowsHitTesting(false)
        }
    }

    /// A clockwise-from-up angle on the flat surface at `flat`, as it shows on
    /// the screen. Unchanged when the view is flat.
    private static func screenAngle(_ degrees: Double, at flat: CGPoint,
                                    in live: PPViewport) -> Double {
        guard live.pitchDegrees > 0 else { return degrees }
        let r = degrees * .pi / 180
        let ahead = CGPoint(x: flat.x + 10 * sin(r), y: flat.y - 10 * cos(r))
        let a = live.screenPoint(forFlat: flat)
        let b = live.screenPoint(forFlat: ahead)
        guard a.x.isFinite, b.x.isFinite else { return degrees }
        return atan2(Double(b.x - a.x), Double(a.y - b.y)) * 180 / .pi
    }

    /// The haze over a tilted view's far ground, in the map's background colour.
    @ViewBuilder
    private func farFade(_ live: PPViewport, size: CGSize) -> some View {
        let fade = live.farFade
        if fade.endY > 0 {
            LinearGradient(colors: [mapBackground.opacity(fade.topAlpha),
                                    mapBackground.opacity(0)],
                           startPoint: .top, endPoint: .bottom)
                .frame(width: size.width, height: fade.endY)
                .frame(width: size.width, height: size.height, alignment: .top)
                .allowsHitTesting(false)
        }
    }

    @ViewBuilder
    private func mapLayer(size: CGSize) -> some View {
        if let frame = model.frame, let live = model.viewport {
            // Layers with separate transforms: on a cache hit the base was
            // drawn at an older camera than the overlay, and the underlay at
            // an older camera still, four times further out. Under them all
            // the style's background, so nothing past the underlay is white.
            //
            // The stack is pinned to the screen size before clipping. A
            // ZStack takes the size of its largest child, and the base is a
            // band 1.5x the surface; without the frame, `.clipped()` clips to
            // the oversized bounds and the GeometryReader's top-leading
            // alignment shifts the whole stack by a quarter of the surface.
            //
            // The map layers are flat, top-down pictures, and one projection
            // tilts them together (identity when flat). The background, the
            // billboards, the fade and the ship are in screen space.
            ZStack {
                mapBackground
                ZStack {
                    if let underlay = model.underlay {
                        Self.layer(underlay.image,
                                   Self.preview(of: underlay.viewport, in: live))
                    }
                    Self.layer(frame.baseImage, Self.preview(of: frame.baseViewport,
                                                             in: live))
                    Self.layer(frame.overlayImage, Self.preview(of: frame.overlayViewport,
                                                                in: live))
                }
                .modifier(Tilt(live: live, screen: size))
                ForEach(Array(frame.billboards.enumerated()), id: \.offset) { _, board in
                    Self.billboard(board, in: live, screen: size)
                }
                farFade(live, size: size)
                if let ship = model.ownship {
                    if AppFlavor.current.showsCourseOverGround {
                        Self.courseVector(ship, in: live, screen: size)
                    }
                    Self.ship(ship, in: live, screen: size)
                }
            }
            .frame(width: size.width, height: size.height)
            .clipped()
        } else {
            // Placeholder for the moment before the first render.
            mapBackground
        }
    }

    /// The style's background once an underlay has reported it, and a neutral
    /// grey before that.
    private var mapBackground: Color {
        model.mapBackground.map { Color(cgColor: $0) } ?? Color(white: 0.92)
    }

    // MARK: - Controls

    private func controls(top: CGFloat) -> some View {
        VStack {
            LiveMapReader(model.live) {
                // Top-right compass, shown only when the map is following or
                // the chart is turned.
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
                    .padding(.top, compassTop(top: top))
                }
            }
            Spacer()
            AisCreditButton(feed: model.aisFeed)
                .padding(.horizontal, ControlMetrics.side)
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
                        aisTraffic: AppFlavor.current.offersAisTraffic
                            ? Binding(get: { model.aisTraffic }, set: { model.setAisTraffic($0) })
                            : nil,
                        onAisSettings: AppFlavor.current.offersAisTraffic
                            ? { aisSettingsShown = true } : nil,
                        onExport: {
                            model.refreshRides()
                            exportShown = true
                            ridesShown = true
                        },
                        onTides: model.tide == nil ? nil : { tidesShown = true },
                        onMapKey: model.hasLegend ? { showLegend() } : nil,
                        onDownloadCharts: model.importsCharts ? { chartDownloadShown = true } : nil,
                        onImportCharts: model.importsCharts && model.chartDownload == nil
                            ? { chartImporterShown = true } : nil,
                        onCheckChartUpdates: model.hasInstalledCharts && model.chartDownload == nil
                            && model.chartImportProgress == nil && !model.checkingChartUpdates
                            ? { model.checkForChartUpdates() } : nil,
                        onSettings: model.importsCharts ? {
                            // iPad opens at the form's own height: at medium the cut falls
                            // on a section header and the options below it are easy to miss.
                            settingsDetent = Self.isPad
                                ? settingsFit.map { .height($0) } ?? .large : .medium
                            settingsShown = true
                        } : nil,
                        onChartHelp: model.importsCharts ? { chartHelpShown = true } : nil,
                        onAbout: { aboutShown = true },
                        onReportProblem: { problemShown = true })
                        .equatable()
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
                            // In the card a point's info would cover the route.
                            if usesToolCard, infoShown {
                                infoShown = false
                                model.select(nil)
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
                        label: model.isRecording ? "Stop recording" : "Record a \(AppFlavor.current.trackNoun)",
                        isActive: model.isRecording,
                        activeSystemName: "stop.circle.fill",
                        activeTint: .red,
                        onLongPress: {
                            model.refreshRides()
                            exportShown = false
                            ridesShown = true
                        },
                        longPressLabel: "Saved \(AppFlavor.current.trackNoun)s") {
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

    /// Below the ride bar's right cluster, or with the compact trip card below
    /// both top cards, which span the width.
    private func compassTop(top: CGFloat) -> CGFloat {
        guard AppFlavor.current.compactTripCard else {
            return Self.belowBars(top: top, rideBarTrailingHeight)
        }
        return Self.belowBars(top: top, model.gpsMode ? rideBarHeight : 0, guidanceBannerHeight)
    }

    private var compassLabel: String {
        guard model.gpsMode else { return "Face north" }
        return model.courseUp ? "North up" : "Course up"
    }

    /// The trip readout shown in GPS mode. An invalid field shows an en-dash,
    /// never a zero, so a stopped vehicle and a silent receiver read
    /// differently.
    @ViewBuilder
    private func rideBar(top: CGFloat) -> some View {
        if AppFlavor.current.compactTripCard {
            tripCard(top: top)
        } else {
            rideClusters(top: top)
        }
    }

    /// The trip readout as one card: elapsed time, speed and course always;
    /// distance to go and arrival with a route, the log without one.
    private func tripCard(top: CGFloat) -> some View {
        let trip = model.trip
        return VStack {
            // On the labels' baseline: a value shrunk to fit is shorter and
            // would otherwise drop its label below the others.
            // One size for every cell, small enough that the arrival time fits
            // unshrunk; units sit against their numbers to save width.
            HStack(alignment: .firstTextBaseline, spacing: 0) {
                courseReadout("TIME", Self.elapsedText(trip), valueSize: Self.tripValueSize)
                courseReadout("SOG", Self.tight(speedText(trip)), valueSize: Self.tripValueSize)
                courseReadout("COG", courseText(model.ownship), valueSize: Self.tripValueSize)
                if trip?.hasRemaining == true {
                    courseReadout("DIST", Self.tight(remainingText(trip)),
                                  valueSize: Self.tripValueSize)
                    courseReadout("ARR", Self.etaText(trip), valueSize: Self.tripValueSize)
                } else {
                    courseReadout("LOG", Self.tight(odometerText(trip)),
                                  valueSize: Self.tripValueSize)
                }
            }
            .readoutCard()
            .measuredHeight(RideBarHeightKey.self)
            .padding(.top, top)
            Spacer()
        }
        .allowsHitTesting(false)
    }

    private static let tripValueSize: CGFloat = 16

    /// "9.7 kn" as "9.7kn": the number and its unit without the space between.
    private static func tight(_ value: String) -> String {
        value.replacingOccurrences(of: " ", with: "")
    }

    /// Elapsed, speed and odometer on the left; distance to go and ETA on the
    /// right when there is a route. Each cluster stacks vertically so a
    /// growing number moves only itself.
    private func rideClusters(top: CGFloat) -> some View {
        VStack {
            HStack(alignment: .top) {
                rideCluster(alignment: .leading) {
                    Label(Self.elapsedText(model.trip), systemImage: "stopwatch")
                    Label(speedText(model.trip), systemImage: "speedometer")
                    Label(odometerText(model.trip), systemImage: "arrow.forward")
                    if AppFlavor.current.showsCourseOverGround {
                        Label(courseText(model.ownship), systemImage: "safari")
                    }
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

    /// COG as reported by the receiver, "047°T" or "055°M"; an en-dash when
    /// it reports none or the boat is slower than the vector's minimum speed.
    private func courseText(_ ship: PPOwnship?) -> String {
        guard let ship, ship.hasCourse, ship.hasSpeed,
              ship.speedMetersPerSecond >= Course.vectorMinimumSpeed else { return "—" }
        return display.courseText(ship.courseDegrees, at: ship.coordinate)
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

    /// The turn banner: an arrow, the road being joined, and the distance
    /// counting down.
    ///
    /// Below the ride bar, because a rider glancing down for the next corner
    /// looks where the ride numbers are. Not hit-testable — there is nothing
    /// to press, and a banner that swallowed taps would be a hole in the
    /// chart where the map is.
    ///
    /// Off route it says so instead of naming a corner. That is the whole of
    /// the guidance's silence made visible: `fv::nav::Guidance` stops
    /// counting down rather than counting down to a corner the rider is not
    /// approaching, and a banner that simply vanished would read as a bug.
    private func guidanceBanner(_ guidance: PPGuidance, top: CGFloat) -> some View {
        VStack {
            HStack(spacing: 12) {
                if guidance.onRoute {
                    Image(systemName: Self.maneuverSymbol(guidance.maneuver))
                        .font(.system(size: 30, weight: .semibold))
                    if guidance.hasThen {
                        // A staggered junction: "right, then immediately
                        // left". The second glyph is smaller because it is
                        // the corner after the one being counted down.
                        Image(systemName: Self.maneuverSymbol(guidance.thenManeuver))
                            .font(.system(size: 20, weight: .semibold))
                            .foregroundStyle(.secondary)
                    }
                    VStack(alignment: .leading, spacing: 2) {
                        Text(display.units.countdown(guidance.distanceMeters))
                            .font(.system(size: 26, weight: .semibold, design: .rounded))
                            .monospacedDigit()
                        // Dropped rather than drawn blank: a good deal of what
                        // a bicycle rides on Kiawah has no name in OSM.
                        if !guidance.road.isEmpty {
                            Text(guidance.road)
                                .font(.system(size: 16, weight: .medium, design: .rounded))
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                        }
                    }
                } else {
                    Image(systemName: "point.topleft.down.to.point.bottomright.curvepath")
                        .font(.system(size: 24, weight: .semibold))
                        .foregroundStyle(.secondary)
                    Text("Off route")
                        .font(.system(size: 20, weight: .medium, design: .rounded))
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 20,
                                                            style: .continuous))
            .measuredHeight(GuidanceBannerHeightKey.self)
            .padding(.top, Self.belowBars(top: top, rideBarHeight))
            Spacer()
        }
        .allowsHitTesting(false)
    }

    /// The course deviation indicator for a straight-leg route: the point
    /// steered for, a needle on a cross-track scale, and the leg and bearing
    /// readouts. Sits where the turn banner does and, like it, takes no taps.
    private func courseIndicator(_ course: PPCourseDeviation, top: CGFloat) -> some View {
        let units = display.units
        let limit = course.offCourseLimitMeters
        let xte = course.crossTrackMeters
        let alarm = course.offCourse
        let name = course.waypointName.isEmpty
            ? "Point \(course.waypointIndex)" : course.waypointName
        return VStack {
            VStack(spacing: 6) {
                HStack(alignment: .firstTextBaseline) {
                    Image(systemName: "mappin.and.ellipse")
                        .foregroundStyle(.secondary)
                    Text(name)
                        .font(.system(size: 17, weight: .semibold, design: .rounded))
                        .lineLimit(1)
                    Text("\(course.waypointIndex)/\(course.waypointCount - 1)")
                        .font(.system(size: 13, weight: .medium, design: .rounded))
                        .foregroundStyle(.secondary)
                    Spacer(minLength: 8)
                    Text(units.distance(course.distanceToWaypointMeters))
                        .font(.system(size: 22, weight: .semibold, design: .rounded))
                        .monospacedDigit()
                }
                CourseNeedle(position: Course.needle(crossTrackMeters: xte, limitMeters: limit),
                             alarm: alarm)
                    .frame(height: 26)
                HStack {
                    Text("XTE \(Course.crossTrackText(xte, units: units))")
                        .foregroundStyle(alarm ? Color.red : Color.primary)
                    Spacer()
                    Text(alarm ? "Off course · \(Course.steerText(crossTrackMeters: xte, limitMeters: limit))"
                               : Course.steerText(crossTrackMeters: xte, limitMeters: limit))
                        .foregroundStyle(alarm ? Color.red : Color.secondary)
                }
                .font(.system(size: 14, weight: .medium, design: .rounded))
                .monospacedDigit()
                HStack(spacing: 0) {
                    courseReadout("DTK", display.courseText(course.legCourseDegrees,
                                                             at: model.ownship?.coordinate))
                    courseReadout("BRG", display.courseText(course.bearingToWaypointDegrees,
                                                             at: model.ownship?.coordinate))
                    courseReadout("TTG", course.hasTimeToGo
                                  ? Course.timeToGoText(course.secondsToWaypoint) : "–")
                    courseReadout("ETA", course.hasTimeToGo
                                  ? Date().addingTimeInterval(course.secondsToWaypoint)
                                      .formatted(date: .omitted, time: .shortened)
                                  : "–")
                }
            }
            .readoutCard()
            .measuredHeight(GuidanceBannerHeightKey.self)
            .padding(.top, Self.belowBars(top: top, rideBarHeight))
            Spacer()
        }
        .allowsHitTesting(false)
    }

    /// One labelled readout cell, "BRG" over "047°T".
    private func courseReadout(_ label: String, _ value: String,
                               valueSize: CGFloat = 16) -> some View {
        VStack(spacing: 1) {
            Text(label)
                .font(.system(size: 11, weight: .semibold, design: .rounded))
                .foregroundStyle(.secondary)
            Text(value)
                .font(.system(size: valueSize, weight: .semibold, design: .rounded))
                .monospacedDigit()
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
    }

    /// The SF Symbol for a turn; the table is shared with the Live Activity.
    private static func maneuverSymbol(_ maneuver: PPManeuver) -> String {
        RideActivityPolicy.symbol(TurnNotifications.turn(maneuver))
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

    /// The top inset for anything that sits under one or more of the top
    /// bars. A measured height of zero means absent rather than a bar of no
    /// height, so it contributes nothing — not even the gap.
    private static func belowBars(top: CGFloat, _ bars: CGFloat...) -> CGFloat {
        bars.reduce(top) { $1 > 0 ? $0 + $1 + ControlMetrics.barGap : $0 }
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
                // Below the ride bar and the turn banner when there are any.
                .padding(.top, Self.belowBars(top: top, rideBarHeight,
                                              guidanceBannerHeight))
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
/// Reports the natural height of the scroll view inside `content`: its
/// content plus the insets around it (the navigation bar). iOS 18 only;
/// earlier, `height` stays nil and the caller uses its maximum.
private struct ContentHeightReader: ViewModifier {
    @Binding var height: CGFloat?

    private static let minimumNatural: CGFloat = 100

    func body(content: Content) -> some View {
        if #available(iOS 18.0, *) {
            content.onScrollGeometryChange(for: CGFloat.self) { geometry in
                geometry.contentSize.height + geometry.contentInsets.top
                    + geometry.contentInsets.bottom
            } action: { _, natural in
                // Before the first layout the form reports almost nothing;
                // a card sized to that would never lay out its rows.
                if natural > Self.minimumNatural { height = natural }
            }
        } else {
            content
        }
    }
}

enum ControlMetrics {
    /// Diameter of every round control.
    static let diameter: CGFloat = 56
    /// Gap between two controls, in either direction.
    static let spacing: CGFloat = 12
    /// From the bottom of the screen to the bottom row.
    static let bottom: CGFloat = 28
    /// From either side of the screen to the outermost control.
    static let side: CGFloat = 24
    /// Between one top bar and whatever drops below it.
    static let barGap: CGFloat = 12
}

/// Reports one measured height up the tree. `Tag` separates the measurements
/// that must not merge: the developer readout clears the whole ride bar, the
/// compass clears only its right cluster. `max` so the result does not depend
/// on traversal order.
private struct HeightKey<Tag>: PreferenceKey {
    static var defaultValue: CGFloat { 0 }
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

private enum RideBarTag {}
private enum GuidanceBannerTag {}
private enum RideBarTrailingTag {}

private typealias RideBarHeightKey = HeightKey<RideBarTag>
private typealias GuidanceBannerHeightKey = HeightKey<GuidanceBannerTag>
private typealias RideBarTrailingHeightKey = HeightKey<RideBarTrailingTag>

private extension View {
    /// The material card the course deviation and trip readouts sit on.
    func readoutCard() -> some View {
        padding(.horizontal, 16)
            .padding(.vertical, 10)
            .frame(maxWidth: 420)
            .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
            .padding(.horizontal, 12)
    }

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

/// The CDI scale: five dots each side of a centre mark, and the needle (the
/// course line) at `position`, -1 full left to 1 full right. The needle is
/// red at full scale or when `alarm` is set.
struct CourseNeedle: View {
    let position: Double
    let alarm: Bool

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let h = geo.size.height
            let step = (w - 16) / 10
            ZStack {
                ForEach(-5...5, id: \.self) { i in
                    if i == 0 {
                        Triangle()
                            .fill(Color.primary)
                            .frame(width: 12, height: 10)
                            .position(x: w / 2, y: h - 5)
                    } else {
                        Circle()
                            .stroke(Color.secondary, lineWidth: 1.5)
                            .frame(width: 7, height: 7)
                            .position(x: w / 2 + CGFloat(i) * step, y: h / 2)
                    }
                }
                Capsule()
                    .fill(alarm || abs(position) >= 1 ? Color.red : Color.green)
                    .frame(width: 5, height: h)
                    .position(x: w / 2 + CGFloat(position) * step * 5, y: h / 2)
            }
        }
        .accessibilityElement()
        .accessibilityLabel("Course deviation")
    }
}

/// An upward-pointing triangle filling its frame.
private struct Triangle: Shape {
    func path(in rect: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: rect.midX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        p.closeSubpath()
        return p
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
        // Whole degrees: course-up recomputes the angle on every camera step,
        // and an animation keyed on the raw value restarts at 20 Hz without
        // end, which SwiftUI keeps merging at a growing CPU cost.
        let shown = northAngleDegrees.rounded()
        Button(action: action) {
            glyph
                // Smooths the compass appearing mid-turn and the one-frame
                // steps a slow render leaves.
                .rotationEffect(.degrees(shown))
                .animation(.linear(duration: 0.15), value: shown)
                .foregroundStyle(.tint)
                .frame(width: ControlMetrics.diameter,
                       height: ControlMetrics.diameter)
                .background(.regularMaterial, in: Circle())
                .overlay(Circle().strokeBorder(.separator, lineWidth: 0.5))
        }
        // Plain, or the automatic style draws its own tinted capsule behind
        // the circle: a halo wider than the control. The tint then has to
        // reach the glyph as a foreground style.
        .buttonStyle(.circleControl)
        .tint(isActive ? .accentColor : .primary)
        .accessibilityLabel(label)
        .shadow(color: .black.opacity(0.18), radius: 6, y: 2)
    }

    @ViewBuilder
    private var glyph: some View {
        if AppFlavor.current.drawsCompassRose {
            CompassRose().frame(width: 42, height: 42)
        } else {
            Image(systemName: "location.north.line.fill")
                .font(.system(size: 22, weight: .medium))
        }
    }
}

/// An eight-point compass rose, north up: long cardinal points, short
/// intercardinal ones, each split light and dark, with a red north point.
/// The other points take the foreground style, so the button's tint reaches
/// them.
struct CompassRose: View {
    var body: some View {
        Canvas { context, size in
            let c = CGPoint(x: size.width / 2, y: size.height / 2)
            let r = min(size.width, size.height) / 2
            // Intercardinals first, so the cardinals cover their bases.
            for (index, degrees) in [45.0, 135, 225, 315, 0, 90, 180, 270].enumerated() {
                let cardinal = index >= 4
                let length = cardinal ? r : r * 0.6
                let half = cardinal ? r * 0.2 : r * 0.14
                let north = degrees == 0
                let style: GraphicsContext.Shading = north ? .color(.red) : .style(.foreground)
                let shade: GraphicsContext.Shading = north
                    ? .color(Color(red: 0.6, green: 0, blue: 0)) : .style(.foreground.opacity(0.45))
                context.fill(point(c, degrees, length, half, side: -1), with: style)
                context.fill(point(c, degrees, length, half, side: 1), with: shade)
            }
            context.fill(Path(ellipseIn: CGRect(x: c.x - 2, y: c.y - 2, width: 4, height: 4)),
                         with: .style(.background))
        }
        .accessibilityHidden(true)
    }

    /// One half of a point: centre, tip at `length` along `degrees`
    /// (clockwise from up), and the base corner `half` to one side.
    private func point(_ c: CGPoint, _ degrees: Double, _ length: CGFloat,
                       _ half: CGFloat, side: CGFloat) -> Path {
        let a = degrees * .pi / 180
        let tip = CGPoint(x: c.x + length * sin(a), y: c.y - length * cos(a))
        let base = CGPoint(x: c.x + side * half * cos(a), y: c.y + side * half * sin(a))
        var p = Path()
        p.move(to: c)
        p.addLine(to: tip)
        p.addLine(to: base)
        p.closeSubpath()
        return p
    }
}

/// The wind now at the pack's beach: an arrow pointing where the wind blows,
/// turned with the chart, and the speed. Hidden when the pack has no `[wind]`,
/// the forecast was fetched 6 h ago or more, or it does not cover now. Not
/// placed on the map: turn alerts cover it and riders feel the wind.
struct WindBadge: View {
    /// Where north is on screen, degrees clockwise from up.
    let northAngleDegrees: Double
    /// Opens the tide card; nil when the pack has no tides.
    let onTap: (() -> Void)?

    @ObservedObject private var feed = WindFeed.shared
    @ObservedObject private var display = DisplayUnits.shared

    var body: some View {
        // The forecast is hourly; a minute is fine-grained enough to roll over.
        TimelineView(.periodic(from: .now, by: 60)) { context in
            if let sample = currentSample(at: context.date) {
                badge(sample)
            }
        }
    }

    private func currentSample(at date: Date) -> PPWindSample? {
        guard feed.settings != nil,
              WindText.badgeShown(fetchedAt: feed.fetchedAt?.timeIntervalSince1970,
                                  now: date.timeIntervalSince1970) else { return nil }
        return feed.forecast?.sample(at: TideClock.override ?? date.timeIntervalSince1970)
    }

    @ViewBuilder
    private func badge(_ sample: PPWindSample) -> some View {
        let text = WindText.badge(speed: sample.speedMetersPerSecond, units: display.units)
        let face = HStack(spacing: 4) {
            if text != "Calm" {
                Image(systemName: "arrow.up")
                    .font(.system(size: 13, weight: .bold))
                    // Whole degrees, as `CompassButton` does and for its reason.
                    .rotationEffect(.degrees(northAngleDegrees.rounded() + sample.fromDegrees + 180))
                    .animation(.linear(duration: 0.15), value: northAngleDegrees.rounded())
            }
            Text(text)
                .font(.subheadline.weight(.semibold).monospacedDigit())
        }
        .foregroundStyle(.primary)
        .padding(.horizontal, 10)
        .frame(height: 32)
        .background(.regularMaterial, in: Capsule())
        .overlay(Capsule().strokeBorder(.separator, lineWidth: 0.5))
        .shadow(color: .black.opacity(0.18), radius: 6, y: 2)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(WindText.badgeLabel(speed: sample.speedMetersPerSecond,
                                                fromDegrees: sample.fromDegrees,
                                                units: display.units))
        if let onTap {
            Button(action: onTap) { face }
                .buttonStyle(.circleControl)
                .accessibilityHint("Opens the tides card.")
        } else {
            face
        }
    }
}

/// Button style for the floating map controls: no system background, dimmed
/// while pressed. The automatic style draws a tinted capsule behind the label
/// that reads as an oval halo around the round face.
struct CircleControlButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .opacity(configuration.isPressed ? 0.55 : 1)
            .animation(.easeOut(duration: 0.1), value: configuration.isPressed)
    }
}

extension ButtonStyle where Self == CircleControlButtonStyle {
    static var circleControl: CircleControlButtonStyle { .init() }
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
            // The controls are plain-styled, so the tint reaches the glyph
            // here rather than through the button style.
            .foregroundStyle(.tint)
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
            .buttonStyle(.circleControl)
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

/// Shows flat map layers through the live viewport's perspective.
///
/// `projectionEffect` draws only what lies inside the view's own frame, and a
/// tilted screen shows flat ground far above the screen. So the layers get a
/// frame grown about the screen's centre to reach `flatBounds`, and the
/// transform is conjugated by that frame's offset from the screen's origin.
private struct Tilt: ViewModifier {
    let live: PPViewport
    let screen: CGSize

    func body(content: Content) -> some View {
        let r = live.flatBounds
        let ex = max(0, -r.minX, r.maxX - screen.width)
        let ey = max(0, -r.minY, r.maxY - screen.height)
        let toFlat = CATransform3DMakeTranslation(-ex, -ey, 0)
        let fromScreen = CATransform3DMakeTranslation(ex, ey, 0)
        let m = CATransform3DConcat(CATransform3DConcat(toFlat, live.perspectiveTransform),
                                    fromScreen)
        content
            .frame(width: screen.width + 2 * ex, height: screen.height + 2 * ey)
            .projectionEffect(ProjectionTransform(m))
    }
}

/// Evaluates its content under an observation of the per-frame map state, so
/// a new frame redraws the content without invalidating the view that holds it.
struct LiveMapReader<Content: View>: View {
    @ObservedObject private var live: LiveMapState
    private let content: () -> Content

    init(_ live: LiveMapState, @ViewBuilder content: @escaping () -> Content) {
        self.live = live
        self.content = content
    }

    var body: some View { content() }
}

extension SceneVisibility {
    /// Maps SwiftUI's phase; an unknown future phase is treated as visible.
    init(_ phase: ScenePhase) {
        switch phase {
        case .active: self = .active
        case .background: self = .background
        default: self = .inactive
        }
    }
}

/// A small button over the chart, shown while AIS sources are being heard,
/// that opens the card of credit lines those sources require. The feed
/// re-publishes many sources, so the full list does not fit on the map.
private struct AisCreditButton: View {
    @ObservedObject var feed: AisFeed
    @State private var showing = false

    var body: some View {
        if !feed.credits.isEmpty {
            Button {
                showing = true
            } label: {
                Label("AIS Sources", systemImage: "info.circle")
                    .font(.caption2)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 3)
                    .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 6))
            }
            .buttonStyle(.plain)
            .frame(maxWidth: .infinity, alignment: .leading)
            .sheet(isPresented: $showing) {
                AisCreditsSheet(feed: feed) { showing = false }
                    .presentationDetents([.medium, .large])
            }
        }
    }
}

/// Every credit line of the AIS sources heard in the last half hour.
private struct AisCreditsSheet: View {
    @ObservedObject var feed: AisFeed
    let onClose: () -> Void

    var body: some View {
        NavigationStack {
            List {
                Section {
                    ForEach(feed.credits, id: \.self) { Text(Self.linked($0)) }
                } footer: {
                    Text("Sources heard in the last half hour.")
                }
            }
            .navigationTitle("AIS Sources")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }

    /// `text` with each web address as a link; a tap opens it in the browser.
    static func linked(_ text: String) -> AttributedString {
        var out = AttributedString(text)
        guard let detector = try? NSDataDetector(types: NSTextCheckingResult.CheckingType.link.rawValue)
        else { return out }
        let ns = text as NSString
        for match in detector.matches(in: text, range: NSRange(location: 0, length: ns.length)) {
            guard let url = match.url, url.scheme == "http" || url.scheme == "https",
                  let r = Range(match.range, in: text),
                  let lower = AttributedString.Index(r.lowerBound, within: out),
                  let upper = AttributedString.Index(r.upperBound, within: out)
            else { continue }
            out[lower..<upper].link = url
        }
        return out
    }
}
