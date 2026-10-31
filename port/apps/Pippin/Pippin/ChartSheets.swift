// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI

/// The chart settings: safety contour and depth, display category, palette
/// and depth units. Each change is applied to the map as it is made.
struct ChartSettingsSheet: View {
    let settings: ChartSettings
    let onChange: (ChartSettings) -> Void
    let onClose: () -> Void

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    depthPicker("Safety Contour", value: \.safetyContour)
                    depthPicker("Safety Depth", value: \.safetyDepth)
                    Picker("Depth Units", selection: binding(\.depthUnit, convert: true)) {
                        ForEach(DepthUnit.allCases) { Text($0.menuName).tag($0) }
                    }
                } header: {
                    Text("Depths")
                } footer: {
                    Text("Water shallower than the safety contour is shaded, and the contour is drawn bold. Soundings at or shallower than the safety depth print black.")
                }
                Section {
                    Picker("Detail", selection: binding(\.category)) {
                        ForEach(ChartCategory.allCases) { Text($0.menuName).tag($0) }
                    }
                    .pickerStyle(.segmented)
                } header: {
                    Text("Display")
                } footer: {
                    Text("Base shows land, depths and hazards only; Standard adds buoys, lights and routes; All adds soundings and the remaining detail.")
                }
                Section {
                    Picker("Colours", selection: binding(\.palette)) {
                        ForEach(ChartPalette.allCases) { Text($0.menuName).tag($0) }
                    }
                } footer: {
                    Text("Automatic uses the night colours in Dark Mode.")
                }
            }
            .navigationTitle("Chart Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }

    private func depthPicker(_ title: String,
                             value: WritableKeyPath<ChartSettings, Double>) -> some View {
        Picker(title, selection: binding(value)) {
            ForEach(settings.depthUnit.depthChoices, id: \.self) { metres in
                Text(settings.depthUnit.label(metres: metres)).tag(metres)
            }
        }
    }

    /// A binding that writes through `onChange`; `convert` re-snaps the depths
    /// when the unit changes.
    private func binding<T>(_ path: WritableKeyPath<ChartSettings, T>,
                            convert: Bool = false) -> Binding<T> {
        Binding(get: { settings[keyPath: path] }, set: { value in
            var next = settings
            next[keyPath: path] = value
            if convert { next = next.converted(to: next.depthUnit) }
            onChange(next)
        })
    }
}

/// The charted features under a tap, topmost first: each with its chart
/// notation and, expanded, every attribute the cell carries.
struct ChartFeatureSheet: View {
    let features: [PPChartFeatureInfo]
    let onClose: () -> Void

    var body: some View {
        NavigationStack {
            List {
                ForEach(features.indices, id: \.self) { index in
                    ChartFeatureRow(feature: features[index], expanded: index == 0)
                }
                Section {
                    EmptyView()
                } footer: {
                    Text("Not for navigation. Chart data may be out of date.")
                }
            }
            .navigationTitle(features.count == 1 ? "Chart Feature" : "\(features.count) Chart Features")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }
}

/// One feature: name, kind and notation, with its attributes behind a disclosure.
private struct ChartFeatureRow: View {
    let feature: PPChartFeatureInfo
    @State private var expanded: Bool

    init(feature: PPChartFeatureInfo, expanded: Bool) {
        self.feature = feature
        _expanded = State(initialValue: expanded)
    }

    var body: some View {
        DisclosureGroup(isExpanded: $expanded) {
            ForEach(feature.attributeNames.indices, id: \.self) { i in
                LabeledContent(feature.attributeNames[i], value: feature.attributeValues[i])
                    .font(.footnote)
            }
            Text(feature.source)
                .font(.caption2)
                .foregroundStyle(.secondary)
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(feature.title).font(.headline)
                if feature.kind != feature.title {
                    Text(feature.kind).font(.subheadline).foregroundStyle(.secondary)
                }
                if !feature.summary.isEmpty {
                    Text(feature.summary).font(.body.monospacedDigit())
                }
            }
        }
    }
}

/// Osprey's help: how soundings read and what the CDI card's abbreviations mean.
struct ChartHelpSheet: View {
    let onClose: () -> Void

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text("Soundings are always in metres, whatever the depth unit setting. A smaller digit after the number is tenths of a metre: 4 with a small 7 is 4.7 m. Charts have no symbols for soundings in feet.")
                    Text("Soundings at or shallower than the safety depth print black; deeper ones print grey.")
                } header: {
                    Text("Depth Soundings")
                }
                Section {
                    term("XTE", "Cross-track error: how far you are off the leg, and to which side. The card says which way to steer to get back on it.")
                    term("DTK", "Desired track: the leg's course, in degrees true.")
                    term("BRG", "Bearing from you to the active waypoint, in degrees true.")
                    term("TTG", "Time to go to the active waypoint at your current speed over ground.")
                    term("ETA", "Clock time you will reach the active waypoint at that speed.")
                } header: {
                    Text("Course Card")
                } footer: {
                    Text("TTG and ETA show a dash until you are under way.")
                }
                Section {
                    EmptyView()
                } footer: {
                    Text("Not for navigation. Chart data may be out of date.")
                }
            }
            .navigationTitle("Chart Help")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }

    private func term(_ name: String, _ meaning: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(name).font(.headline.monospaced())
            Text(meaning)
        }
    }
}

/// NOAA region picker: states, then Coast Guard districts. A choice replaces
/// the installed chart set once its zip has downloaded and imported.
struct ChartDownloadSheet: View {
    /// The region downloading now, if any.
    let active: ChartRegion?
    /// Region ids a failed download can resume.
    let resumable: Set<String>
    /// Region id NOAA has changed charts for since the installed set.
    let updateRegion: String?
    /// An import is running; nothing can start until it ends.
    let busy: Bool
    let onChoose: (ChartRegion) -> Void
    let onCancel: () -> Void
    let onClose: () -> Void

    var body: some View {
        NavigationStack {
            List {
                if let active {
                    Section {
                        Button("Cancel \(active.name) Download", role: .destructive, action: onCancel)
                    }
                }
                Section {
                    ForEach(ChartRegion.states) { row($0) }
                } header: {
                    Text("States")
                } footer: {
                    Text("A state's zip holds every NOAA chart cell that touches it, from overview to harbour.")
                }
                Section {
                    ForEach(ChartRegion.districts) { row($0) }
                } header: {
                    Text("Coast Guard Districts")
                } footer: {
                    Text("Charts come from NOAA's Office of Coast Survey. The new set replaces the installed one. Not for navigation.")
                }
            }
            .navigationTitle("Download Charts")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }

    private func row(_ region: ChartRegion) -> some View {
        Button { onChoose(region) } label: {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text(region.name).foregroundStyle(.primary)
                    Text(region.sizeText).font(.footnote).foregroundStyle(.secondary)
                }
                Spacer()
                if active == region {
                    ProgressView()
                } else if resumable.contains(region.id) {
                    Text("Resume").font(.footnote).foregroundStyle(.tint)
                } else if updateRegion == region.id {
                    Text("Update").font(.footnote).foregroundStyle(.tint)
                }
            }
        }
        .disabled(active != nil || busy)
    }
}
