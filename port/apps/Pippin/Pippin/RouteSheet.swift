// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI

/// The route sheet, the draft behind it, and the crosshair it dismisses into.
///
/// The draft is not the route. `PPRoute` is what the map draws; `RouteDraft`
/// is what the user is typing and may be incomplete or abandoned. OK turns one
/// into the other; Cancel discards the draft. The draft is owned by
/// `MapScreen` because "Pick on map" dismisses the sheet.

// MARK: - Draft

/// One stop being edited. `id` is the identity while it is a draft; the
/// positional label becomes the identity once it reaches the document
/// (`fv::RouteDoc` selects and deletes by label).
struct RouteStop: Identifiable, Equatable {
    let id = UUID()
    var coordinate: PPGeoPoint?

    /// The name the rider chose when the stop came from a search, or nil.
    /// Display only: document labels stay positional ("Start", "Via 1",
    /// "End") because two waypoints sharing a label would delete as a pair.
    /// Filling the row any other way clears this.
    var name: String?

    static func == (a: RouteStop, b: RouteStop) -> Bool { a.id == b.id }
}

/// What the sheet edits: an ordered list of stops and a profile. Start is the
/// first and End is the last; both always exist.
@MainActor
final class RouteDraft: ObservableObject {
    /// Which of the two sheets is showing. The dialog opens as a search box
    /// and becomes the stops form once it has a destination. Lives on the
    /// draft because "Pick on map" destroys the sheet.
    enum Phase {
        case search
        case stops
    }

    @Published var phase: Phase = .search
    @Published var stops: [RouteStop] = [RouteStop(), RouteStop()]
    @Published var profile: String = "bicycle"

    /// Every stop has a position and there are at least two. Enables OK.
    var isRoutable: Bool {
        stops.count >= 2 && stops.allSatisfy { $0.coordinate != nil }
    }

    var hasAnything: Bool { stops.contains { $0.coordinate != nil } }

    /// The positional label a stop reaches the document under. Must be unique.
    func label(at index: Int) -> String {
        if index == 0 { return "Start" }
        if index == stops.count - 1 { return "End" }
        return "Via \(index)"
    }

    /// Inserts a via before the End.
    func addVia() {
        stops.insert(RouteStop(), at: max(1, stops.count - 1))
    }

    /// Only vias can be removed.
    func canRemove(at index: Int) -> Bool {
        index > 0 && index < stops.count - 1
    }

    func remove(at index: Int) {
        guard canRemove(at: index) else { return }
        stops.remove(at: index)
    }

    /// The waypoints for the document. Stops with no position are dropped.
    func waypoints() -> [PPWaypoint] {
        stops.enumerated().compactMap { index, stop in
            guard let coordinate = stop.coordinate else { return nil }
            return PPWaypoint(label: label(at: index), coordinate: coordinate)
        }
    }

    /// Loads the draft from the route on the map. An existing route opens on
    /// the stops form; nothing to edit opens on the search box.
    func load(from route: PPRoute?) {
        guard let route, route.waypoints.count > 0 else {
            stops = [RouteStop(), RouteStop()]
            phase = .search
            return
        }
        stops = route.waypoints.map { RouteStop(coordinate: $0.coordinate) }
        if stops.count == 1 { stops.append(RouteStop()) }
        if !route.profile.isEmpty { profile = route.profile }
        phase = .stops
    }

    /// Accepts a searched destination: it becomes the End, the phone's fix
    /// (if any) becomes the Start, and the phase moves to the form. Writes
    /// the last stop rather than `stops[1]` so a draft with vias is handled.
    func accept(destination: PPGeoPoint, named name: String,
                start: PPGeoPoint?) {
        if stops.count < 2 { stops = [RouteStop(), RouteStop()] }
        stops[stops.count - 1].coordinate = destination
        stops[stops.count - 1].name = name
        if let start, stops[0].coordinate == nil {
            stops[0].coordinate = start
            // From the phone, not the list: the row names the road under it.
            stops[0].name = nil
        }
        phase = .stops
    }
}

// MARK: - Sheet

struct RouteSheet: View {
    @ObservedObject var draft: RouteDraft

    /// The live route, for the status line and for whether Clear is offered.
    let route: PPRoute?
    let isPlanning: Bool

    /// Observed directly because a `.sheet` inherits none of the map screen's
    /// environment. See `DisplayUnits`.
    @ObservedObject private var display = DisplayUnits.shared
    /// Nil when no fix has arrived; "Current location" is then offered disabled.
    let currentLocation: PPGeoPoint?
    let profileNames: [String]

    /// Search callback, handed in so this view never sees `MapModel`.
    let search: (String) async -> [PPSearchResult]
    /// What the first phase's box opens on.
    let initialSearchText: String
    /// Reports a term that produced an accepted result. Only the first
    /// phase's box records; a stop's search sheet does not.
    let onSearched: (String) -> Void

    let onPick: (Int) -> Void
    /// A search result was chosen. The sheet has already written the
    /// coordinate into the draft; the caller moves the camera.
    let onFrame: (PPSearchResult) -> Void
    let onSubmit: () -> Void

    /// The mode picker changed. The caller replans the live route's waypoints
    /// with the new profile, never the draft's. Not a submit: Cancel leaves a
    /// mode change standing.
    let onProfileChange: (String) -> Void
    let onClear: () -> Void
    let onCancel: () -> Void

    var body: some View {
        Group {
            switch draft.phase {
            case .search:
                searchPhase
            case .stops:
                stopsPhase
            }
        }
        // On the group, not per branch: a sheet has one presentation, and
        // detents applied inside one branch do not resize a sheet presented
        // on the other.
        .presentationDetents([.medium, .large])
    }

    // MARK: - Phase one: search

    /// The search box, given the whole sheet. "Pick on Map" skips to the form.
    private var searchPhase: some View {
        NavigationStack {
            SearchView(
                prompt: "Where to?",
                initialText: initialSearchText,
                search: search,
                onChoose: { result in
                    draft.accept(destination: result.coordinate,
                                 named: result.title,
                                 start: currentLocation)
                    onFrame(result)
                },
                onSearched: onSearched)
                .navigationTitle("Search")
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Cancel", action: onCancel)
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Pick on Map") { draft.phase = .stops }
                    }
                }
        }
    }

    // MARK: - Phase two: stops

    private var stopsPhase: some View {
        NavigationStack {
            Form {
                Section {
                    ForEach(Array(draft.stops.enumerated()), id: \.element.id) {
                        index, stop in
                        stopRow(index: index, stop: stop)
                    }
                    Button {
                        draft.addVia()
                    } label: {
                        Label("Add via", systemImage: "plus.circle")
                    }
                } header: {
                    Text("Stops")
                } footer: {
                    // A footer rather than its own section so it is above the
                    // fold at the medium detent.
                    if let line = statusLine {
                        Text(line)
                    }
                }

                Section("Mode") {
                    Picker("Mode", selection: $draft.profile) {
                        ForEach(Self.offeredModes(profileNames), id: \.profile) { mode in
                            Text(mode.title).tag(mode.profile)
                        }
                    }
                    .pickerStyle(.segmented)
                    // Inside the stops phase so it exists only while the
                    // control is on screen; `load(from:)` and `onAppear` also
                    // write `profile` and are not taps.
                    .onChange(of: draft.profile) { _, profile in
                        onProfileChange(profile)
                    }
                }

            }
            .navigationTitle("Route")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel", action: onCancel)
                }
                // Clear is in the toolbar so it is reachable at the medium
                // detent, shown only when there is a route, and does not
                // dismiss (clearing is usually the first half of starting a
                // different route).
                if route?.exists == true {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button(role: .destructive, action: onClear) {
                            Image(systemName: "trash")
                        }
                        .accessibilityLabel("Clear Route")
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    if isPlanning {
                        ProgressView()
                    } else {
                        Button("OK", action: onSubmit).disabled(!draft.isRoutable)
                    }
                }
            }
        }
    }

    /// One stop as a `LocationRow`, the same control the point editor uses,
    /// plus Remove for vias.
    private func stopRow(index: Int, stop: RouteStop) -> some View {
        LocationRow(
            title: draft.label(at: index),
            coordinate: stop.coordinate,
            chosenName: stop.name,
            currentLocation: currentLocation,
            onUseCurrent: { fix in
                // A menu outlives the tap that opened it; check the index here.
                guard draft.stops.indices.contains(index) else { return }
                draft.stops[index].coordinate = fix
                draft.stops[index].name = nil
            },
            onPickOnMap: { onPick(index) },
            onSearchResult: { result in
                guard draft.stops.indices.contains(index) else { return }
                draft.stops[index].coordinate = result.coordinate
                draft.stops[index].name = result.title
                onFrame(result)
            }) {
                if draft.canRemove(at: index) {
                    Divider()
                    Button(role: .destructive) {
                        draft.remove(at: index)
                    } label: {
                        Label("Remove", systemImage: "trash")
                    }
                }
            }
    }

    private var statusLine: String? {
        if currentLocation == nil && !draft.isRoutable {
            return "No position yet — pick both ends on the map."
        }
        guard let route, !route.statusText.isEmpty else { return nil }
        return Self.statusText(route, units: display.units)
    }

    /// The planner's status line in the rider's units. The core's line
    /// ("bicycle: 11.1 km, 45 min" plus any warnings) is metric and shared
    /// with every shell, so the head is rebuilt here from the route's own
    /// numbers and only the tail after " min" is kept. A line with no " min"
    /// is not a plan and is passed through untouched.
    private static func statusText(_ route: PPRoute,
                                   units: DistanceUnits) -> String {
        let text = route.statusText
        guard route.isCalculated,
              let minutes = text.range(of: " min") else { return text }
        let what = route.profile.isEmpty ? "roads" : route.profile
        let head = "\(what): \(units.distance(route.lengthMeters)), "
            + String(format: "%.0f min", route.seconds / 60.0)
        return head + text[minutes.upperBound...]
    }

    /// The modes with a control, filtered by what the rule file defines.
    private static func offeredModes(_ names: [String]) -> [(profile: String, title: String)] {
        let known = [(profile: "foot", title: "Walk"), (profile: "bicycle", title: "Cycle")]
        let offered = known.filter { names.isEmpty || names.contains($0.profile) }
        return offered.isEmpty ? known : offered
    }
}

// MARK: - Location text (shared by route stops and points)

/// How a place is described in words. A row shows the nearest rideable road
/// ("Flyaway Drive", or "cycleway" when unnamed) because that is what a rider
/// can check; the coordinate is the fallback when no road is near. The
/// wording lives here rather than in C++ so the row and the button can share it.
enum LocationText {
    static func describe(_ coordinate: PPGeoPoint?,
                         place: PPPlace? = nil,
                         chosen: String? = nil) -> String {
        guard let coordinate else { return "Not set" }
        // The most specific known fact wins: a chosen name over the road.
        if let chosen, !chosen.isEmpty { return chosen }
        if let place, place.isUsable { return place.name }
        return String(format: "%.5f, %.5f", coordinate.latitude,
                      coordinate.longitude)
    }

    /// The pick button's label. Nil `place` is "no answer yet" and reads as
    /// the plain confirmation. A snap outranks the road: the button names the
    /// feature whose coordinate it is about to store.
    static func useButton(_ place: PPPlace?,
                          snap: PPSnapTarget? = nil) -> String {
        if let snap { return "Use \(snap.name)" }
        guard let place else { return "Use this point" }
        return place.isUsable ? "Use \(place.name)" : "No usable path"
    }
}

/// A place and the ways to set it: Search, Current location, Pick on map.
/// One control for both a route stop and a point. "Current location" is
/// offered disabled when there is no fix rather than hidden.
struct LocationRow<Extra: View>: View {
    /// Coordinate-to-road lookup, from the environment so the route sheet and
    /// point editor need not thread it through their initialisers. The
    /// default answers nil and the row falls back to the numbers.
    @Environment(\.describePlace) private var describePlace

    /// Search, from the environment for the same reason. The default finds
    /// nothing.
    @Environment(\.searchPlaces) private var searchPlaces

    /// Result of the lookup. Numbers are shown until it returns.
    @State private var place: PPPlace?

    /// The search sheet is up. A sheet over a sheet, so the draft need not
    /// survive a dismissal.
    @State private var searching = false

    let title: String
    let coordinate: PPGeoPoint?
    /// The name the rider chose, or nil. Non-nil replaces the road lookup.
    let chosenName: String?
    /// The last fix, or nil.
    let currentLocation: PPGeoPoint?
    /// Handed the fix, so a caller cannot act on a stale capture.
    let onUseCurrent: (PPGeoPoint) -> Void
    let onPickOnMap: () -> Void
    /// A place was found by name. Nil hides the Search item.
    let onSearchResult: ((PPSearchResult) -> Void)?
    /// Caller-specific menu items (the route's Remove).
    @ViewBuilder let extraItems: () -> Extra

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.subheadline)
                Text(LocationText.describe(coordinate, place: place,
                                           chosen: chosenName))
                    .font(.caption)
                    .monospacedDigit()
                    .foregroundStyle(coordinate == nil ? .secondary : .primary)
            }
            Spacer()
            Menu {
                // Search first: the shortest question when the rider knows
                // the name.
                if onSearchResult != nil {
                    Button {
                        searching = true
                    } label: {
                        Label("Search", systemImage: "magnifyingglass")
                    }
                }

                Button {
                    if let currentLocation { onUseCurrent(currentLocation) }
                } label: {
                    Label("Current location", systemImage: "location")
                }
                .disabled(currentLocation == nil)

                Button(action: onPickOnMap) {
                    Label("Pick on map", systemImage: "mappin.and.ellipse")
                }

                extraItems()
            } label: {
                Image(systemName: "ellipsis.circle").font(.title3)
            }
        }
        .padding(.vertical, 2)
        // Keyed on the coordinate so the lookup reruns when the place
        // changes and not when the sheet merely redraws.
        .task(id: PlaceKey(coordinate)) {
            // A row with a chosen name skips the lookup; the answer would not
            // be shown and the hop may load the road graph.
            guard let coordinate, chosenName == nil else {
                place = nil
                return
            }
            place = await describePlace(coordinate)
        }
        .sheet(isPresented: $searching) {
            NavigationStack {
                SearchView(
                    prompt: title,
                    search: searchPlaces,
                    onChoose: { result in
                        searching = false
                        onSearchResult?(result)
                    })
                    .navigationTitle(title)
                    .navigationBarTitleDisplayMode(.inline)
                    .toolbar {
                        ToolbarItem(placement: .cancellationAction) {
                            Button("Cancel") { searching = false }
                        }
                    }
            }
            // `searchPlaces` was read in this view and captured, so nothing
            // needs to cross the sheet's environment boundary.
            .presentationDetents([.large])
        }
    }
}

/// A coordinate as something `.task(id:)` can compare. `PPGeoPoint` is a C
/// struct and not `Equatable`. Six decimals is about a tenth of a metre.
private struct PlaceKey: Equatable {
    let lat: Int64
    let lon: Int64

    init(_ coordinate: PPGeoPoint?) {
        lat = Int64(((coordinate?.latitude ?? 0) * 1e6).rounded())
        lon = Int64(((coordinate?.longitude ?? 0) * 1e6).rounded())
    }
}

/// Environment key for the place namer. See `LocationRow.describePlace`.
private struct DescribePlaceKey: EnvironmentKey {
    static let defaultValue: (PPGeoPoint) async -> PPPlace? = { _ in nil }
}

extension EnvironmentValues {
    var describePlace: (PPGeoPoint) async -> PPPlace? {
        get { self[DescribePlaceKey.self] }
        set { self[DescribePlaceKey.self] = newValue }
    }
}

/// Environment key for search. See `LocationRow.searchPlaces`.
private struct SearchPlacesKey: EnvironmentKey {
    static let defaultValue: (String) async -> [PPSearchResult] = { _ in [] }
}

extension EnvironmentValues {
    var searchPlaces: (String) async -> [PPSearchResult] {
        get { self[SearchPlacesKey.self] }
        set { self[SearchPlacesKey.self] = newValue }
    }
}

extension LocationRow where Extra == EmptyView {
    init(title: String,
         coordinate: PPGeoPoint?,
         chosenName: String? = nil,
         currentLocation: PPGeoPoint?,
         onUseCurrent: @escaping (PPGeoPoint) -> Void,
         onPickOnMap: @escaping () -> Void,
         onSearchResult: ((PPSearchResult) -> Void)? = nil) {
        self.init(title: title, coordinate: coordinate, chosenName: chosenName,
                  currentLocation: currentLocation, onUseCurrent: onUseCurrent,
                  onPickOnMap: onPickOnMap, onSearchResult: onSearchResult,
                  extraItems: { EmptyView() })
    }
}

// MARK: - Pick on map

/// The picking state: a crosshair pinned at the screen centre while the map
/// moves under it, and two buttons. Knows only which row it is filling. The
/// pick is whatever is under the centre pixel, which `PPViewport` answers
/// exactly.
struct PickOverlay: View {
    let title: String
    let coordinate: PPGeoPoint?
    /// What is under the crosshair, or nil until the first answer.
    let place: PPPlace?
    /// The feature the pick has snapped to, or nil. Non-nil means
    /// `coordinate` is that feature's position, not the centre pixel's.
    let snap: PPSnapTarget?
    /// Safe-area margins, handed down from `MapScreen`.
    let topMargin: CGFloat
    let bottomMargin: CGFloat
    let onConfirm: () -> Void
    let onCancel: () -> Void

    var body: some View {
        ZStack {
            crosshair
            VStack {
                Text(title)
                    .font(.subheadline)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(.thinMaterial, in: Capsule())
                    .padding(.top, topMargin)
                Spacer()
                HStack(spacing: 12) {
                    Button("Cancel", action: onCancel)
                        .buttonStyle(.bordered)
                    Button(action: onConfirm) {
                        Text(readout)
                    }
                    .buttonStyle(.borderedProminent)
                }
                .padding(.bottom, bottomMargin)
            }
        }
    }

    /// The confirm button's label. "No usable path" is a label, not a block:
    /// the button stays enabled.
    private var readout: String {
        guard coordinate != nil else { return "Use this point" }
        return LocationText.useButton(place, snap: snap)
    }

    /// Drawn in SwiftUI rather than by the map because it belongs to the
    /// screen centre regardless of the preview transform. The ring grows and
    /// takes the accent colour when snapped; a fill alone was invisible
    /// against a pale chart. It never moves to the feature, since the
    /// crosshair is pinned by construction.
    private var crosshair: some View {
        ZStack {
            Circle()
                .strokeBorder(isSnapped ? Color.accentColor : .white, lineWidth: 3)
                .background(Circle().strokeBorder(.black.opacity(0.6), lineWidth: 1))
                .frame(width: isSnapped ? 34 : 26, height: isSnapped ? 34 : 26)
            Rectangle().fill(.white).frame(width: 1.5, height: 44)
            Rectangle().fill(.white).frame(width: 44, height: 1.5)
            if isSnapped {
                Circle()
                    .fill(Color.accentColor)
                    .frame(width: 10, height: 10)
                    .transition(.scale.combined(with: .opacity))
            }
        }
        .animation(.easeOut(duration: 0.12), value: isSnapped)
        .shadow(color: .black.opacity(0.5), radius: 2)
        .allowsHitTesting(false)
    }

    private var isSnapped: Bool { snap != nil }
}
