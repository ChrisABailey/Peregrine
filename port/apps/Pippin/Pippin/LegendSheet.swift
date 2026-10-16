// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import Combine
import PippinKit
import SwiftUI

/// The map key: one section per legend group, each row a swatch drawn by the
/// style at the map's current scale, a short label and the router's tier.
/// Expanding a row shows the style's description and the router's sentence.
///
/// Presented at a medium detent over a live map. By default it lists only the
/// rows the map is drawing; the rows and swatches are refreshed when the
/// camera settles or the colour scheme changes.
struct LegendSheet: View {
    let model: MapModel
    let onClose: () -> Void

    @Environment(\.colorScheme) private var colorScheme
    @State private var groups: [PPLegendGroup] = []
    @State private var scope: Scope = .onMap

    /// Which rows the sheet lists.
    enum Scope: Hashable { case onMap, all }

    /// Swatch size in points. An authored pixel is one point, so the widest
    /// line (a z19 interstate casing, 30 pt) fits the height.
    static let swatchSize = CGSize(width: 56, height: 36)

    var body: some View {
        NavigationStack {
            List {
                Picker("Show", selection: $scope) {
                    Text("On this map").tag(Scope.onMap)
                    Text("All").tag(Scope.all)
                }
                .pickerStyle(.segmented)
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets())
                ForEach(visibleGroups, id: \.group.title) { group, entries in
                    Section {
                        ForEach(entries, id: \.identifier) { entry in
                            LegendRow(entry: entry)
                        }
                    } header: {
                        Text(group.title)
                    } footer: {
                        if !group.subtitle.isEmpty { Text(group.subtitle) }
                    }
                }
                if scope == .onMap && visibleGroups.isEmpty && !groups.isEmpty {
                    Text("Nothing in the key is on this part of the map.")
                        .foregroundStyle(.secondary)
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
        .onReceive(Self.settledCamera(model.live)) { _ in
            Task { await reload() }
        }
        .onChange(of: colorScheme) { _, _ in
            Task { await reload() }
        }
    }

    /// Each group with the rows the scope lists, in display order; groups
    /// left with no rows are dropped.
    private var visibleGroups: [(group: PPLegendGroup, entries: [PPLegendEntry])] {
        groups.compactMap { group in
            let entries = scope == .onMap
                ? group.entries.filter(\.isOnMap) : Self.ordered(group.entries)
            return entries.isEmpty ? nil : (group, entries)
        }
    }

    private func reload() async {
        groups = await model.legend(swatchSize: Self.swatchSize)
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

/// One legend row: swatch, label and tier, expanding to the detail text.
private struct LegendRow: View {
    let entry: PPLegendEntry

    var body: some View {
        DisclosureGroup {
            VStack(alignment: .leading, spacing: 6) {
                if !entry.detail.isEmpty { Text(entry.detail) }
                if !entry.routerSentence.isEmpty { Text(entry.routerSentence) }
            }
            .font(.callout)
            .padding(.vertical, 2)
        } label: {
            HStack(spacing: 12) {
                swatch
                VStack(alignment: .leading, spacing: 2) {
                    Text(entry.label)
                    if !entry.isShown, let from = Self.fromZoom(entry.minZoom) {
                        Text(from)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                Spacer(minLength: 4)
                if !entry.tierWord.isEmpty {
                    TierCapsule(word: entry.tierWord, rank: entry.tierRank)
                }
            }
            .opacity(entry.isShown ? 1 : 0.45)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(accessibilityText)
        }
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
        if !entry.tierWord.isEmpty { parts.append(entry.tierWord) }
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
