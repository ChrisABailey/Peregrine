// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI

/// The map menu and the settings behind it.
///
/// The other map controls are things a rider presses while riding; these are
/// settings changed once and forgotten, so they go behind one button rather
/// than more 56-point circles.
///
/// A `Menu` rather than a sheet: it is anchored to its button, dismisses on
/// the next tap, and costs no state in `MapScreen`. A `Picker` inside one
/// renders as a submenu showing its current value, so a row is both the
/// control and its readout.

/// The app's unit and bearing preferences, persisted in user defaults.
///
/// A singleton because the two views that format distances sit on opposite
/// sides of a sheet boundary: the ride bar owns the model, and the search
/// rows are presented in a `.sheet` and inherit none of its environment.
/// Both observe this object directly rather than threading a third
/// environment key through every presentation.
@MainActor
final class DisplayUnits: ObservableObject {
    static let shared = DisplayUnits()

    private static let key = "PPDistanceUnits"
    private static let bearingsKey = "PPBearingReference"

    @Published var units: DistanceUnits {
        didSet {
            guard units != oldValue else { return }
            UserDefaults.standard.set(units.rawValue, forKey: Self.key)
        }
    }

    @Published var bearings: BearingReference {
        didSet {
            guard bearings != oldValue else { return }
            UserDefaults.standard.set(bearings.rawValue, forKey: Self.bearingsKey)
        }
    }

    private init() {
        let stored = UserDefaults.standard.string(forKey: Self.key)
        // A stored choice wins only if this app offers it.
        let offered = AppFlavor.current.offeredUnits
        units = stored.flatMap(DistanceUnits.init(rawValue:)).flatMap {
            offered.contains($0) ? $0 : nil
        } ?? AppFlavor.current.defaultUnits
        bearings = UserDefaults.standard.string(forKey: Self.bearingsKey)
            .flatMap(BearingReference.init(rawValue:)) ?? .trueNorth
    }

    /// A course for display in the chosen reference, using the variation at
    /// `position`. With no position a magnetic choice falls back to true,
    /// labelled T, rather than guess a variation.
    func courseText(_ degrees: Double, at position: PPGeoPoint?) -> String {
        guard bearings == .magnetic, let position else { return Course.text(degrees) }
        let variation = PPMap.magneticVariation(latitude: position.latitude,
                                                longitude: position.longitude)
        return Course.text(degrees, reference: .magnetic, variation: variation)
    }
}

// MARK: - The menu button

/// The menu button above the route button, and its pop-out.
struct MapMenuButton: View, Equatable {
    /// Which symbol-size step the map is drawn at. The factors live in the
    /// pack (`MapModel.symbolZoomSteps`); this view only names them, so a
    /// pack can offer four sizes without a change here.
    @Binding var symbolStep: Int
    let symbolStepLabels: [String]
    @Binding var units: DistanceUnits
    /// Shows live AIS traffic; nil hides the item.
    let aisTraffic: Binding<Bool>?
    /// Opens the AIS source settings; nil hides the item.
    let onAisSettings: (() -> Void)?
    /// Opens the rides sheet, the same one the record button's long press
    /// opens, under the name a rider would look for.
    let onExport: () -> Void
    /// Opens the tide card; nil hides the item, for a pack with no table.
    let onTides: (() -> Void)?
    /// Opens the map key; nil hides the item, for a style with no legend.
    let onMapKey: (() -> Void)?
    /// Opens the NOAA region picker; nil hides the item.
    let onDownloadCharts: (() -> Void)?
    /// Opens the Files picker for a chart zip; nil hides the item.
    let onImportCharts: (() -> Void)?
    /// Checks NOAA for chart updates; nil hides the item.
    let onCheckChartUpdates: (() -> Void)?
    /// Opens the settings sheet; nil hides the item. When present, the sheet
    /// owns symbol size and units and the menu leaves them out.
    let onSettings: (() -> Void)?
    /// Opens the chart help; nil hides the item.
    let onChartHelp: (() -> Void)?
    let onAbout: () -> Void
    let onReportProblem: () -> Void

    /// Whether the Charts submenu has anything in it.
    private var hasChartItems: Bool {
        onDownloadCharts != nil || onCheckChartUpdates != nil || onImportCharts != nil
    }

    var body: some View {
        Menu {
            // `.menu` puts each picker behind its own title. The default
            // flattens a picker into an unheaded section, leaving "Small /
            // Medium / Large" with nothing to say what it sizes.
            if onSettings == nil {
                Picker("Symbol Size", selection: $symbolStep) {
                    ForEach(symbolStepLabels.indices, id: \.self) { index in
                        Text(symbolStepLabels[index]).tag(index)
                    }
                }
                .pickerStyle(.menu)
                Picker("Distance Units", selection: $units) {
                    ForEach(AppFlavor.current.offeredUnits) { unit in
                        Text(unit.menuName).tag(unit)
                    }
                }
                .pickerStyle(.menu)
            }
            if let aisTraffic {
                Toggle(isOn: aisTraffic) {
                    Label("AIS Traffic", systemImage: "ferry")
                }
            }
            if let onAisSettings {
                Button(action: onAisSettings) {
                    Label("AIS Settings…", systemImage: "antenna.radiowaves.left.and.right")
                }
            }
            if let onTides {
                Button(action: onTides) {
                    Label("Tides", systemImage: "water.waves")
                }
            }
            if let onMapKey {
                Button(action: onMapKey) {
                    Label("Map Key", systemImage: "list.bullet.rectangle")
                }
            }
            if let onSettings {
                Button(action: onSettings) {
                    Label("Settings", systemImage: "gearshape")
                }
            }
            if hasChartItems {
                Menu {
                    if let onDownloadCharts {
                        Button(action: onDownloadCharts) {
                            Label("Download Charts", systemImage: "icloud.and.arrow.down")
                        }
                    }
                    if let onCheckChartUpdates {
                        Button(action: onCheckChartUpdates) {
                            Label("Check for Chart Updates",
                                  systemImage: "arrow.triangle.2.circlepath")
                        }
                    }
                    if let onImportCharts {
                        Button(action: onImportCharts) {
                            Label("Import Charts", systemImage: "square.and.arrow.down")
                        }
                    }
                } label: {
                    Label("Charts", systemImage: "map")
                }
            }
            if let onChartHelp {
                Button(action: onChartHelp) {
                    Label("Help", systemImage: "questionmark.circle")
                }
            }
            Button(action: onExport) {
                Label("Export\u{2026}", systemImage: "square.and.arrow.up")
            }
            Button(action: onAbout) {
                Label("About", systemImage: "info.circle")
            }
            Button(action: onReportProblem) {
                Label("Report a Problem", systemImage: "exclamationmark.bubble")
            }
        } label: {
            CircleControlFace(systemName: "line.3.horizontal")
        }
        // `.fixed`, or the list arrives upside down. The default `.priority`
        // puts the first item nearest the button, and this button is at the
        // bottom of the screen, so the menu opens upward.
        .menuOrder(.fixed)
        // Same reason as the round buttons: the automatic style puts a tinted
        // capsule behind the label, wider than the circle it sits under.
        .menuStyle(.button)
        .buttonStyle(.circleControl)
        .tint(.primary)
        .accessibilityLabel("Menu")
        .shadow(color: .black.opacity(0.18), radius: 6, y: 2)
    }

    /// Compared on what the menu draws, so a redraw of the map behind it does
    /// not rebuild it. Needs `.equatable()` at the call site.
    ///
    /// A position feed redraws the map screen several times a second, and
    /// rebuilding the items of an already-open menu flickers and swallows the
    /// tap in flight — a picker that takes several taps to change. The action
    /// closures are deliberately not compared: each captures the model and
    /// `@State` wrappers, whose identity outlives a body pass, so an older
    /// closure does what a newer one would.
    static func == (a: MapMenuButton, b: MapMenuButton) -> Bool {
        a.symbolStep == b.symbolStep
            && a.symbolStepLabels == b.symbolStepLabels
            && a.units == b.units
            && a.aisTraffic?.wrappedValue == b.aisTraffic?.wrappedValue
            && (a.onAisSettings == nil) == (b.onAisSettings == nil)
            && (a.onTides == nil) == (b.onTides == nil)
            && (a.onMapKey == nil) == (b.onMapKey == nil)
            && (a.onImportCharts == nil) == (b.onImportCharts == nil)
            && (a.onDownloadCharts == nil) == (b.onDownloadCharts == nil)
            && (a.onCheckChartUpdates == nil) == (b.onCheckChartUpdates == nil)
            && (a.onSettings == nil) == (b.onSettings == nil)
            && (a.onChartHelp == nil) == (b.onChartHelp == nil)
    }
}

// MARK: - Report a problem

/// Where a rider says what is wrong with the map, and sends it.
///
/// Save and Send are two acts: Save only writes a file (see
/// `ReportLibrary`), which cannot fail for want of a network, and the list is
/// the queue. Send opens the rider's own mail app with everything filled in;
/// the share button beside it covers AirDrop, Messages and Files.
///
/// A sent report is not deleted. `openURL` never reports whether the rider
/// pressed Send or Cancel, so deleting on the way out would discard reports
/// that were never sent. Deleting is the rider's own act, by the row's trash
/// button or a swipe.
struct ReportProblemSheet: View {
    /// Where the map is centred, captured with the report. Five decimals is
    /// about a metre.
    let coordinateText: String
    let onClose: () -> Void

    @State private var text = ""
    @State private var reports: [SavedReport] = []
    @State private var sharing: SharePayload?
    @Environment(\.openURL) private var openURL

    private var canSave: Bool {
        !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    // `axis: .vertical` grows the field with what is typed
                    // instead of scrolling one line sideways.
                    TextField(AppFlavor.current.reportPrompt,
                              text: $text, axis: .vertical)
                        .lineLimit(3...8)
                    LabeledContent("Map Center") {
                        Text(coordinateText)
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                    }
                    Button("Save Report") {
                        if let saved = ReportLibrary.save(
                            text: text, coordinateText: coordinateText) {
                            text = ""
                            reports.insert(saved, at: 0)
                        }
                    }
                    .disabled(!canSave)
                } header: {
                    Text("Description of Problem")
                } footer: {
                    Text("The report is saved on this device. You can "
                         + "review the email contents before choosing to "
                         + "send it.")
                }

                if !reports.isEmpty {
                    Section("Saved Reports") {
                        ForEach(reports) { report in
                            reportRow(report)
                        }
                        .onDelete { offsets in
                            offsets.map { reports[$0] }.forEach(delete)
                        }
                    }
                }

                // With no address configured, point the rider at the share
                // button rather than naming the Info.plist key, which is a
                // build instruction they cannot act on.
                if ReportMail.address == nil {
                    Section {
                        Text("Mail isn't set up in this version. Use the "
                             + "share button beside a report to send it "
                             + "whichever way you like.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("Report a Problem")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
            .sheet(item: $sharing) { payload in
                ShareSheet(items: payload.items, subject: payload.subject)
            }
            .onAppear { reports = ReportLibrary.reports() }
        }
    }

    @ViewBuilder
    private func reportRow(_ report: SavedReport) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(report.summary.isEmpty ? "Report" : report.summary)
                    .lineLimit(2)
                Text(report.subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            // `.borderless` on both, or the row takes the tap and any tap in
            // it fires one of them.
            Button {
                if let url = ReportMail.mailURL(for: report) { openURL(url) }
            } label: {
                Image(systemName: "envelope")
            }
            .buttonStyle(.borderless)
            .disabled(ReportMail.address == nil)
            .accessibilityLabel("Send \(report.subtitle)")
            Button {
                sharing = SharePayload(
                    items: [report.url],
                    subject: "\(AppName.display) report \(report.subtitle)")
            } label: {
                Image(systemName: "square.and.arrow.up")
            }
            .buttonStyle(.borderless)
            .accessibilityLabel("Share \(report.subtitle)")
            // The swipe is not discoverable, and a report that has been sent
            // has to be got rid of somehow.
            Button(role: .destructive) {
                delete(report)
            } label: {
                Image(systemName: "trash")
            }
            .buttonStyle(.borderless)
            .accessibilityLabel("Delete \(report.subtitle)")
        }
    }

    /// Removes one report from disk and from the list.
    private func delete(_ report: SavedReport) {
        ReportLibrary.delete(report)
        reports.removeAll { $0 == report }
    }
}

// MARK: - About

/// The app's version, read from the bundle rather than held as a literal so
/// it matches what the phone believes it installed. The bundle keys come from
/// the targets' `MARKETING_VERSION` and `CURRENT_PROJECT_VERSION` build
/// settings, which must agree across Pippin, PippinKit and PippinShare or the
/// share extension is rejected at install.
enum AppVersion {
    /// "1.0.0", or "—" in a SwiftUI preview outside a built app.
    static var marketing: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
            as? String ?? "—"
    }

    static var build: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion")
            as? String ?? "—"
    }
}

/// What the app is, whose data it draws, and what it does not promise.
///
/// The three legal items are obligations, not decoration. The OpenStreetMap
/// credit is required by its licence; Osprey credits NOAA, whose charts it
/// draws instead. The licence block is LGPL-3.0 §4c: an
/// application linking FvKit must carry the FalconView and Peregrine
/// copyright notices wherever it displays its own, and the row above displays
/// Chris Bailey's, so deleting the Georgia Tech notice means deleting both.
/// §4a and §4b are met by `COPYING` and `COPYING.LESSER` shipping in the
/// bundle; the non-endorsement sentence is `NOTICE.md` §7, owed because the
/// paragraph names the mark. Pippin's disclaimer covers the route as well as
/// the map: a wrong route puts a rider on a road they should not be on.
/// Osprey's follows the first-launch notice and adds AIS.
struct AboutSheet: View {
    let onClose: () -> Void

    /// These are `LocalizedStringKey` constants because the markdown-parsing
    /// `Text` initialiser takes a key, and only a literal becomes one.
    /// Splitting a sentence with `+` to fit the column silently selects the
    /// verbatim `String` overload, which prints the brackets and the URL.
    private static let osmCredit: LocalizedStringKey =
        "Map and Routing data from [OpenStreetMap](https://www.openstreetmap.org/copyright)"

    private static let encCredit: LocalizedStringKey =
        "Maps are rendered from NOAA Electronic Navigational Charts, available from [charts.noaa.gov](https://charts.noaa.gov)."

    private static let licence: LocalizedStringKey =
        "Built on [Peregrine](https://github.com/ChrisABailey/Peregrine), used under the GNU Lesser General Public License, version 3 or later. Copies of the licence ship inside this app and are available on the website."

    private static let upstream: LocalizedStringKey =
        "Peregrine is a cross-platform port of FalconView\u{2122}, Copyright \u{00A9} 1994\u{2013}2011 Georgia Tech Research Corporation. It is an independent port, and is neither endorsed by nor affiliated with Georgia Tech Research Corporation."

    private static var encDisclaimer: LocalizedStringKey {
        "**\(AppName.display) is not a certified ECDIS or chart plotter.** Positions, depths, routes and AIS reports may be wrong or out of date. Do not rely on this app to navigate; use official charts and your own judgement."
    }

    private static let osmDisclaimer: LocalizedStringKey =
        "**The map may be wrong.** A path shown here may not exist, may be private, or may be impassable. A path that exists may be missing. A suggested route may not be safe or legal for the way you are travelling. Always observe local laws and regulations, trust what is in front of you over what is on the screen, and watch out for alligators."

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text("\(AppName.display) Version \(AppVersion.marketing)")
                        .monospacedDigit()
                    Text("Copyright \u{00A9} 2026 Chris Bailey")
                }
                Section {
                    // Markdown, so the link renders inside the sentence
                    // rather than as a URL bolted underneath.
                    Text(AppFlavor.current == .osprey ? Self.encCredit : Self.osmCredit)
                }
                // One section, because §4c binds the two paragraphs together.
                Section {
                    Text(Self.licence)
                    Text(Self.upstream)
                }
                .font(.footnote)
                .foregroundStyle(.secondary)
                Section {
                    Text(AppFlavor.current == .osprey ? Self.encDisclaimer : Self.osmDisclaimer)
                }
                .font(.footnote)
            }
            .navigationTitle("About")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done", action: onClose)
                }
            }
        }
    }
}
