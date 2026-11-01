// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI

/// The app's settings in three tabs: the chart's depths and detail, the
/// units and bearing reference, and the symbol size and colours. Each change
/// is applied to the map as it is made.
struct SettingsSheet: View {
    let chart: ChartSettings
    let onChartChange: (ChartSettings) -> Void
    @Binding var symbolStep: Int
    let symbolStepLabels: [String]
    let onClose: () -> Void

    @ObservedObject private var display = DisplayUnits.shared
    @State private var tab = Tab.chart

    private enum Tab: String, CaseIterable, Identifiable {
        case chart = "Chart"
        case units = "Units"
        case display = "Display"
        var id: String { rawValue }
    }

    var body: some View {
        NavigationStack {
            Form {
                switch tab {
                case .chart: chartTab
                case .units: unitsTab
                case .display: displayTab
                }
            }
            // Pinned above the form so the tabs stay put when a tab scrolls.
            .safeAreaInset(edge: .top, spacing: 0) {
                Picker("Settings", selection: $tab) {
                    ForEach(Tab.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .padding(.horizontal)
                .padding(.vertical, 8)
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }

    @ViewBuilder private var chartTab: some View {
        Section {
            depthPicker("Safety Contour", value: \.safetyContour)
            depthPicker("Safety Depth", value: \.safetyDepth)
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
            Text("Detail")
        } footer: {
            Text("Base shows land, depths and hazards only; Standard adds buoys, lights and routes; All adds soundings and the remaining detail.")
        }
        Section {
            Toggle("Hide Buoy and Beacon Names", isOn: binding(\.hideNavaidNames))
        } footer: {
            Text("A tap on a buoy or beacon still shows its name.")
        }
    }

    @ViewBuilder private var unitsTab: some View {
        Section {
            Picker("Distance", selection: $display.units) {
                ForEach(AppFlavor.current.offeredUnits) { Text($0.settingsName).tag($0) }
            }
            .pickerStyle(.segmented)
        } header: {
            Text("Distance and Speed")
        } footer: {
            Text("Nautical gives nautical miles and knots; Standard, miles and mph; Metric, kilometres and km/h.")
        }
        Section {
            Picker("Depth", selection: binding(\.depthUnit, convert: true)) {
                ForEach(DepthUnit.allCases) { Text($0.menuName).tag($0) }
            }
            .pickerStyle(.segmented)
        } header: {
            Text("Depth")
        } footer: {
            Text("The safety contour and depth move to the nearest choice in the new unit. Soundings stay in metres.")
        }
        Section {
            Picker("Bearings", selection: $display.bearings) {
                ForEach(BearingReference.allCases) { Text($0.menuName).tag($0) }
            }
            .pickerStyle(.segmented)
        } header: {
            Text("Bearings")
        } footer: {
            Text("Magnetic courses use the World Magnetic Model at the boat's position. Without a position, courses are shown true.")
        }
    }

    @ViewBuilder private var displayTab: some View {
        if !symbolStepLabels.isEmpty {
            Section("Symbol Size") {
                Picker("Symbol Size", selection: $symbolStep) {
                    ForEach(symbolStepLabels.indices, id: \.self) { index in
                        Text(symbolStepLabels[index]).tag(index)
                    }
                }
                .pickerStyle(.segmented)
            }
        }
        Section {
            Picker("Colours", selection: binding(\.palette)) {
                ForEach(ChartPalette.allCases) { Text($0.menuName).tag($0) }
            }
            .pickerStyle(.segmented)
        } header: {
            Text("Colours")
        } footer: {
            Text("Automatic uses the night colours in Dark Mode.")
        }
    }

    private func depthPicker(_ title: String,
                             value: WritableKeyPath<ChartSettings, Double>) -> some View {
        Picker(title, selection: binding(value)) {
            ForEach(chart.depthUnit.depthChoices, id: \.self) { metres in
                Text(chart.depthUnit.label(metres: metres)).tag(metres)
            }
        }
    }

    /// A binding that writes through `onChange`; `convert` re-snaps the depths
    /// when the unit changes.
    private func binding<T>(_ path: WritableKeyPath<ChartSettings, T>,
                            convert: Bool = false) -> Binding<T> {
        Binding(get: { chart[keyPath: path] }, set: { value in
            var next = chart
            next[keyPath: path] = value
            if convert { next = next.converted(to: next.depthUnit) }
            onChartChange(next)
        })
    }
}

/// The charted features under a tap, topmost first: each with its chart
/// notation and, expanded, every attribute the cell carries.
struct ChartFeatureSheet: View {
    let features: [PPChartFeatureInfo]
    let onClose: () -> Void

    /// "AIS Target" when every entry is a vessel, else the chart wording.
    private var title: String {
        let ais = features.allSatisfy { $0.acronym == "AIS" }
        let noun = ais ? "AIS Target" : "Chart Feature"
        return features.count == 1 ? noun : "\(features.count) \(noun)s"
    }

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
            .navigationTitle(title)
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
                    term("DTK", "Desired track: the leg's course, in degrees true (T) or magnetic (M).")
                    term("BRG", "Bearing from you to the active waypoint, in degrees true (T) or magnetic (M).")
                    term("TTG", "Time to go to the active waypoint at your current speed over ground.")
                    term("ETA", "Clock time you will reach the active waypoint at that speed.")
                } header: {
                    Text("Course Card")
                } footer: {
                    Text("TTG and ETA show a dash until you are under way. Settings › Units chooses true or magnetic for every course and bearing.")
                }
                Section {
                    EmptyView()
                } footer: {
                    Text("Not for navigation. Chart data may be out of date.")
                }
            }
            .navigationTitle("Help")
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
