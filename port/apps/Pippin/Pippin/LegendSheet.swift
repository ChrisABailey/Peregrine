// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Combine
import PippinKit
import SwiftUI

/// The map key: road types first, then lanes, routes and surfaces, then the
/// style's other groups. Each row is a swatch drawn by the style at the map's
/// current scale, a short label and the router's tier. Rows are an accordion;
/// an expanded row shows the router's sentence and
/// the slider that adjusts its setting for the pack's profile.
///
/// Presented at a medium detent over a live map. By default it lists only the
/// rows the map is drawing; the rows and swatches are refreshed when the
/// camera settles, the colour scheme changes or a slider moves.
struct LegendSheet: View {
    let model: MapModel
    let onClose: () -> Void

    @Environment(\.colorScheme) private var colorScheme
    @State private var groups: [PPLegendGroup] = []
    @State private var scope: Scope
    /// The one expanded row's identifier.
    @State private var expanded: String?
    /// Modifier rows whose result just changed because a road type moved.
    @State private var flashing: Set<String> = []

    /// Which rows the sheet lists.
    enum Scope: Hashable { case onMap, all, adjusted }

    init(model: MapModel, scope: Scope = .onMap, onClose: @escaping () -> Void) {
        self.model = model
        self.onClose = onClose
        _scope = State(initialValue: scope)
    }

    /// Swatch size in points. An authored pixel is one point, so the widest
    /// line (a z19 interstate casing, 30 pt) fits the height.
    static let swatchSize = CGSize(width: 56, height: 36)

    /// One titled list of rows.
    struct KeySection: Identifiable {
        let title: String
        let footer: String
        let entries: [PPLegendEntry]
        var id: String { title }
    }

    var body: some View {
        NavigationStack {
            ScrollViewReader { proxy in
                List {
                    Picker("Show", selection: $scope) {
                        Text("On this map").tag(Scope.onMap)
                        Text("All").tag(Scope.all)
                        Text("Adjusted").tag(Scope.adjusted)
                    }
                    .pickerStyle(.segmented)
                    .listRowBackground(Color.clear)
                    .listRowInsets(EdgeInsets())
                    if hasSliders { adjustmentHeader }
                    ForEach(sections) { section in
                        Section {
                            ForEach(section.entries, id: \.identifier) { entry in
                                row(entry, proxy: proxy)
                                    .id(entry.identifier)
                                    .listRowBackground(flashing.contains(entry.identifier)
                                        ? Color.accentColor.opacity(0.18)
                                        : Color(.secondarySystemGroupedBackground))
                            }
                        } header: {
                            Text(section.title)
                        } footer: {
                            if !section.footer.isEmpty { Text(section.footer) }
                        }
                    }
                    if sections.isEmpty && !groups.isEmpty {
                        Text(scope == .adjusted
                            ? "No routing preferences are adjusted."
                            : "Nothing in the key is on this part of the map.")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("Map Key")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
        .task { await reload() }
        // A route on screen is re-planned once the key goes, not per slider move.
        .onAppear { model.mapKeyOpened() }
        .onDisappear { model.mapKeyClosed() }
        .onReceive(Self.settledCamera(model.live)) { _ in
            Task { await reload() }
        }
        .onChange(of: colorScheme) { _, _ in
            Task { await reload() }
        }
    }

    private func row(_ entry: PPLegendEntry, proxy: ScrollViewProxy) -> some View {
        LegendRow(
            entry: entry,
            isExpanded: Binding(
                get: { expanded == entry.identifier },
                set: { expanded = $0 ? entry.identifier : nil }),
            settingLabels: settingLabels,
            onChoose: { choose(entry, stop: $0) },
            onOpenSetting: { open(setting: $0, proxy: proxy) })
    }

    /// "Routing for Cycling · 2 adjusted" and Reset all.
    private var adjustmentHeader: some View {
        let count = adjustedSettings.count
        return HStack {
            Text(count == 0
                ? "Routing for \(RouteSheet.modeName(profile))"
                : "Routing for \(RouteSheet.modeName(profile)) · \(count) adjusted")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Button("Reset all") { resetAll() }
                .font(.subheadline)
                .disabled(count == 0)
        }
    }

    private var profile: String { model.routeDefaultProfile }

    private var allEntries: [PPLegendEntry] { groups.flatMap(\.entries) }

    private var hasSliders: Bool { allEntries.contains { !$0.stopWords.isEmpty } }

    /// Settings moved off their shipped stop; rows sharing one count once.
    private var adjustedSettings: Set<String> {
        Set(allEntries.filter(\.isAdjusted).map(\.settingId))
    }

    /// The first row's label for each setting, for "Set by …" links.
    private var settingLabels: [String: String] {
        var labels: [String: String] = [:]
        for entry in allEntries where !entry.settingId.isEmpty && labels[entry.settingId] == nil {
            labels[entry.settingId] = entry.label
        }
        return labels
    }

    /// Road types, then rows the router prices by anything else, then the
    /// style's groups for rows it does not price; empty sections dropped.
    private var sections: [KeySection] {
        let priced = scoped(allEntries.filter { !$0.tierWord.isEmpty })
        var out: [KeySection] = []
        let roads = priced.filter { $0.settingKind == .roadType }
        let others = priced.filter { $0.settingKind != .roadType }
        if !roads.isEmpty { out.append(KeySection(title: "Road types", footer: "", entries: roads)) }
        if !others.isEmpty {
            out.append(KeySection(title: "Lanes, routes and surfaces", footer: "", entries: others))
        }
        guard scope != .adjusted else { return out }
        for group in groups {
            let rest = scoped(group.entries.filter { $0.tierWord.isEmpty })
            if !rest.isEmpty {
                out.append(KeySection(title: group.title, footer: group.subtitle, entries: rest))
            }
        }
        return out
    }

    /// `entries` as the scope lists them: on the map, adjusted, or all with
    /// the rows drawn at this scale first.
    private func scoped(_ entries: [PPLegendEntry]) -> [PPLegendEntry] {
        switch scope {
        case .onMap: return entries.filter(\.isOnMap)
        case .adjusted: return entries.filter(\.isAdjusted)
        case .all: return Self.ordered(entries)
        }
    }

    private func reload() async {
        groups = await model.legend(swatchSize: Self.swatchSize)
    }

    /// Moves `entry`'s setting to `stop`, reprices the key and flashes the
    /// modifier rows a road-type change moved.
    private func choose(_ entry: PPLegendEntry, stop: Int) {
        guard stop != entry.stop else { return }
        let before = Self.modifierResults(groups)
        model.setRouteAdjustment(profile: profile, setting: entry.settingId, stop: stop,
                                 defaultStop: entry.defaultStop)
        Task {
            await reload()
            if entry.settingKind == .roadType { flash(changedFrom: before) }
        }
    }

    private func resetAll() {
        model.resetRouteAdjustments(profile: profile)
        Task { await reload() }
    }

    /// Expands the first row for `setting`, widening the scope when the row
    /// is not listed.
    private func open(setting: String, proxy: ScrollViewProxy) {
        guard let target = allEntries.first(where: { $0.settingId == setting }) else { return }
        if !sections.contains(where: { $0.entries.contains { $0.identifier == target.identifier } }) {
            scope = .all
        }
        expanded = target.identifier
        withAnimation { proxy.scrollTo(target.identifier, anchor: .center) }
    }

    private func flash(changedFrom before: [String: [String]]) {
        let after = Self.modifierResults(groups)
        let changed = Set(after.compactMap { id, words in
            before[id].map { $0 != words ? id : nil } ?? nil
        })
        guard !changed.isEmpty else { return }
        withAnimation(.easeIn(duration: 0.15)) { flashing = changed }
        Task {
            try? await Task.sleep(for: .seconds(1.2))
            withAnimation(.easeOut(duration: 0.4)) { flashing = [] }
        }
    }

    /// Each modifier row's tier and its "On the map this becomes" tiers.
    static func modifierResults(_ groups: [PPLegendGroup]) -> [String: [String]] {
        var out: [String: [String]] = [:]
        for entry in groups.flatMap(\.entries) where entry.settingKind == .modifier {
            out[entry.identifier] = [entry.tierWord] + entry.onRoads.map(\.tierWord)
        }
        return out
    }

    /// Rows the map draws at this scale first, then the dimmed ones, each in
    /// the style's order.
    static func ordered(_ entries: [PPLegendEntry]) -> [PPLegendEntry] {
        entries.filter(\.isShown) + entries.filter { !$0.isShown }
    }

    /// Fires when the camera has rested for a quarter second after moving.
    /// A pan changes which rows are on the map; swatches are cached per
    /// quarter zoom, so a reload after a pan redraws none.
    private static func settledCamera(_ live: LiveMapState) -> AnyPublisher<Void, Never> {
        live.$viewport
            .compactMap { $0 }
            .filter(\.hasSurface)
            .dropFirst()
            .debounce(for: .milliseconds(250), scheduler: RunLoop.main)
            .map { _ in () }
            .eraseToAnyPublisher()
    }
}

extension PPLegendEntry {
    /// The row's slider is off its shipped stop.
    var isAdjusted: Bool { stop >= 0 && stop != defaultStop }
}

/// One legend row: swatch, label and pill, expanding to the router sentence and
/// the row's slider, lock or "Set by" links.
private struct LegendRow: View {
    let entry: PPLegendEntry
    @Binding var isExpanded: Bool
    let settingLabels: [String: String]
    let onChoose: (Int) -> Void
    let onOpenSetting: (String) -> Void

    var body: some View {
        // A row the router does not price has nothing to expand.
        if entry.routerSentence.isEmpty && entry.tierWord.isEmpty {
            label
        } else {
            DisclosureGroup(isExpanded: $isExpanded) {
                VStack(alignment: .leading, spacing: 8) {
                    if !entry.routerSentence.isEmpty { Text(entry.routerSentence) }
                    control
                }
                .font(.callout)
                .padding(.vertical, 2)
            } label: {
                label
            }
        }
    }

    /// Swatch, name and pill on one line; at large text sizes, where the name would
    /// wrap beside the pill, the pill drops below the name.
    private var label: some View {
        ViewThatFits(in: .horizontal) {
            HStack(spacing: 12) {
                swatch
                name
                Spacer(minLength: 4)
                trailing
            }
            HStack(alignment: .top, spacing: 12) {
                swatch
                VStack(alignment: .leading, spacing: 4) {
                    name
                    trailing
                }
                Spacer(minLength: 0)
            }
        }
        .opacity(entry.isShown ? 1 : 0.45)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilityText)
    }

    private var name: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 6) {
                Text(entry.label)
                if entry.isAdjusted {
                    Circle()
                        .fill(Color.accentColor)
                        .frame(width: 7, height: 7)
                        .accessibilityHidden(true)
                }
            }
            if !entry.isShown, let from = Self.fromZoom(entry.minZoom) {
                Text(from)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    /// The lock and the tier pill.
    private var trailing: some View {
        HStack(spacing: 6) {
            if entry.isLocked {
                Image(systemName: "lock.fill")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            if let pill {
                TierCapsule(word: pill.word, rank: pill.rank)
            }
        }
    }

    /// The slider, the lock and its reason, or the settings behind a row
    /// with several.
    @ViewBuilder
    private var control: some View {
        if entry.isLocked {
            if !entry.lockReason.isEmpty {
                Label(entry.lockReason, systemImage: "lock.fill")
                    .foregroundStyle(.secondary)
            }
        } else if !entry.stopWords.isEmpty, entry.stop >= 0 {
            StopSlider(words: entry.stopWords, stop: entry.stop, defaultStop: entry.defaultStop,
                       onCommit: onChoose)
            if entry.settingKind == .roadType && entry.stop == entry.stopWords.count - 1 {
                Label("A ride that needs this road ends in a straight leg.",
                      systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.orange)
            }
            if !entry.onRoads.isEmpty { onRoadsLine }
        } else if !entry.termSettingIds.isEmpty {
            setByLine
            if !entry.termsLine.isEmpty {
                Text(entry.termsLine)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
        }
    }

    /// The modifier on each road class, as the road-type sliders now stand.
    private var onRoadsLine: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("On the map this becomes")
                .font(.caption)
                .foregroundStyle(.secondary)
            ForEach(entry.onRoads, id: \.label) { road in
                HStack {
                    Text(road.label).font(.subheadline)
                    Spacer()
                    TierCapsule(word: road.tierWord, rank: road.tierRank)
                }
                .accessibilityElement(children: .combine)
            }
        }
    }

    /// "Set by Rough surface and Local route", each name opening its row.
    private var setByLine: some View {
        let named = entry.termSettingIds.compactMap { id in settingLabels[id].map { (id, $0) } }
        return HStack(spacing: 4) {
            Text(named.isEmpty ? "Set by more than one setting" : "Set by")
                .foregroundStyle(.secondary)
            ForEach(Array(named.enumerated()), id: \.element.0) { index, item in
                if index > 0 {
                    Text(index == named.count - 1 ? "and" : ",").foregroundStyle(.secondary)
                }
                Button(item.1) { onOpenSetting(item.0) }
                    .buttonStyle(.borderless)
            }
        }
    }

    /// Road types show the tier word; modifiers show their slider word; a
    /// locked or slider-less row shows the tier word.
    private var pill: (word: String, rank: Int)? {
        if entry.settingKind == .modifier, !entry.isLocked,
           entry.stop >= 0, entry.stop < entry.stopWords.count {
            return (entry.stopWords[entry.stop], Self.modifierRank(entry.stop))
        }
        return entry.tierWord.isEmpty ? nil : (entry.tierWord, entry.tierRank)
    }

    /// The relative scale's colour, Much better (0) green to Much worse red.
    static func modifierRank(_ stop: Int) -> Int {
        [0, 0, 1, 2, 3, 4][min(max(stop, 0), 5)]
    }

    @ViewBuilder
    private var swatch: some View {
        let size = LegendSheet.swatchSize
        if let image = entry.swatch {
            Image(decorative: image, scale: entry.swatchPixelsPerPoint)
                .frame(width: size.width, height: size.height)
                .clipShape(RoundedRectangle(cornerRadius: 4))
        } else {
            Color.clear.frame(width: size.width, height: size.height)
        }
    }

    private var accessibilityText: String {
        var parts = [entry.label]
        if let pill { parts.append(pill.word) }
        if entry.isLocked { parts.append("locked") }
        if entry.isAdjusted { parts.append("adjusted") }
        if !entry.isShown, let from = Self.fromZoom(entry.minZoom) { parts.append(from) }
        return parts.joined(separator: ", ")
    }

    /// "From zoom 15" for a row the map starts drawing at `minZoom`.
    static func fromZoom(_ minZoom: Double) -> String? {
        guard minZoom >= 0 else { return nil }
        let rounded = (minZoom * 10).rounded() / 10
        let text = rounded == rounded.rounded() ? String(Int(rounded)) : String(rounded)
        return "From zoom \(text)"
    }
}

/// A slider over a setting's stops with the current word above it, the end
/// words below and a "Default" mark over the shipped stop. Commits when a
/// drag ends or an accessibility adjustment lands.
private struct StopSlider: View {
    let words: [String]
    let stop: Int
    let defaultStop: Int
    let onCommit: (Int) -> Void

    @State private var value: Double = 0
    @State private var editing = false

    /// Half the system thumb: where the first and last stops sit inside the track.
    private static let thumbInset: CGFloat = 18

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(words[current]).font(.callout.weight(.semibold))
            defaultMark
            Slider(value: $value, in: 0...Double(max(words.count - 1, 1)), step: 1) {
                Text("Preference")
            } onEditingChanged: { isEditing in
                editing = isEditing
                if !isEditing { onCommit(current) }
            }
            .accessibilityValue(words[current])
            .accessibilityHint(defaultStop >= 0 ? "Default: \(words[defaultStop])" : "")
            HStack {
                Text(words.first ?? "")
                Spacer()
                Text(words.last ?? "")
            }
            .font(.caption2)
            .foregroundStyle(.secondary)
        }
        .onAppear { value = Double(stop) }
        .onChange(of: stop) { _, new in value = Double(new) }
        .onChange(of: value) { _, _ in
            if !editing { onCommit(current) }
        }
    }

    private var current: Int { min(max(Int(value.rounded()), 0), words.count - 1) }

    /// "Default" over a down arrow, centred on the shipped stop.
    @ViewBuilder
    private var defaultMark: some View {
        if defaultStop >= 0, words.count > 1 {
            GeometryReader { geo in
                let track = geo.size.width - 2 * Self.thumbInset
                let x = Self.thumbInset + track * CGFloat(defaultStop) / CGFloat(words.count - 1)
                VStack(spacing: 0) {
                    Text("Default")
                    Image(systemName: "arrowtriangle.down.fill").imageScale(.small)
                }
                .font(.caption2)
                .foregroundStyle(.secondary)
                .fixedSize()
                .position(x: x, y: 13)
            }
            .frame(height: 26)
            .accessibilityHidden(true)
        }
    }
}

/// The router's tier word on a system colour, so it never reads as a map line.
private struct TierCapsule: View {
    let word: String
    let rank: Int

    var body: some View {
        Text(word)
            .font(.caption.weight(.medium))
            .padding(.horizontal, 8)
            .padding(.vertical, 3)
            .foregroundStyle(color)
            .background(color.opacity(0.15), in: Capsule())
            .fixedSize()
    }

    /// Cheapest first; "Never" (rank -1) shares the last resort's red.
    private var color: Color {
        switch rank {
        case 0: return .green
        case 1: return .blue
        case 2: return .gray
        case 3: return .orange
        default: return .red
        }
    }
}
