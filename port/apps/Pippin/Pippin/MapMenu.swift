// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

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

// MARK: - Distance units

/// The unit distances are shown in. A display preference only: the trip
/// computer, router and search all work in metres.
enum DistanceUnits: String, CaseIterable, Identifiable {
    case kilometres
    case miles

    var id: String { rawValue }

    var menuName: String {
        switch self {
        case .kilometres: return "Kilometres"
        case .miles: return "Miles"
        }
    }

    /// Formats a distance for reading at arm's length: the small unit close
    /// in, the big one further out, one decimal at most. The changeover is
    /// where the big unit gets its first digit, since "0.06 mi" is not a
    /// number anyone can picture.
    func distance(_ meters: Double) -> String {
        switch self {
        case .kilometres:
            if meters < 1000 { return String(format: "%.0f m", meters) }
            return String(format: "%.1f km", meters / 1000.0)
        case .miles:
            let feet = meters / Self.metersPerFoot
            if feet < Self.feetPerMile / 10 {
                return String(format: "%.0f ft", feet)
            }
            return String(format: "%.1f mi", feet / Self.feetPerMile)
        }
    }

    /// Whole units per hour, which is all the precision a bicycle has.
    func speed(_ metersPerSecond: Double) -> String {
        switch self {
        case .kilometres:
            return String(format: "%.0f km/h", metersPerSecond * 3.6)
        case .miles:
            let mph = metersPerSecond / Self.metersPerFoot / Self.feetPerMile
                * 3600.0
            return String(format: "%.0f mph", mph)
        }
    }

    /// The international foot and the statute mile, kept separate so the feet
    /// conversion is exact too.
    private static let metersPerFoot = 0.3048
    private static let feetPerMile = 5280.0
}

/// The app's unit preference, persisted in user defaults.
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

    @Published var units: DistanceUnits {
        didSet {
            guard units != oldValue else { return }
            UserDefaults.standard.set(units.rawValue, forKey: Self.key)
        }
    }

    private init() {
        let stored = UserDefaults.standard.string(forKey: Self.key)
        // Miles is the default because this app's riders are on Kiawah. Only
        // the default: a stored choice is read first and wins.
        units = stored.flatMap(DistanceUnits.init(rawValue:)) ?? .miles
    }
}

// MARK: - The menu button

/// The menu button above the route button, and its pop-out.
struct MapMenuButton: View {
    /// Which symbol-size step the map is drawn at. The factors live in the
    /// pack (`MapModel.symbolZoomSteps`); this view only names them, so a
    /// pack can offer four sizes without a change here.
    @Binding var symbolStep: Int
    let symbolStepLabels: [String]
    @Binding var units: DistanceUnits
    /// Opens the rides sheet, the same one the record button's long press
    /// opens, under the name a rider would look for.
    let onExportRide: () -> Void
    let onAbout: () -> Void
    let onReportProblem: () -> Void

    var body: some View {
        Menu {
            // `.menu` puts each picker behind its own title. The default
            // flattens a picker into an unheaded section, leaving "Small /
            // Medium / Large" with nothing to say what it sizes.
            Picker("Symbol Size", selection: $symbolStep) {
                ForEach(symbolStepLabels.indices, id: \.self) { index in
                    Text(symbolStepLabels[index]).tag(index)
                }
            }
            .pickerStyle(.menu)
            Picker("Distance Units", selection: $units) {
                ForEach(DistanceUnits.allCases) { unit in
                    Text(unit.menuName).tag(unit)
                }
            }
            .pickerStyle(.menu)
            Button(action: onExportRide) {
                Label("Export Ride Tracks", systemImage: "square.and.arrow.up")
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
        .tint(.primary)
        .accessibilityLabel("Menu")
        .shadow(color: .black.opacity(0.18), radius: 6, y: 2)
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
                    TextField("A path that is not there, a gate that is, a name that is wrong\u{2026}",
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
                    Text("The report is saved on your phone. You can "
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
/// credit is required by its licence. The licence block is LGPL-3.0 §4c: an
/// application linking FvKit must carry the FalconView and Peregrine
/// copyright notices wherever it displays its own, and the row above displays
/// Chris Bailey's, so deleting the Georgia Tech notice means deleting both.
/// §4a and §4b are met by `COPYING` and `COPYING.LESSER` shipping in the
/// bundle; the non-endorsement sentence is `NOTICE.md` §7, owed because the
/// paragraph names the mark. The disclaimer covers the route as well as the
/// map: a wrong route puts a rider on a road they should not be on.
struct AboutSheet: View {
    let onClose: () -> Void

    /// These are `LocalizedStringKey` constants because the markdown-parsing
    /// `Text` initialiser takes a key, and only a literal becomes one.
    /// Splitting a sentence with `+` to fit the column silently selects the
    /// verbatim `String` overload, which prints the brackets and the URL.
    private static let credit: LocalizedStringKey =
        "Map and Routing data from [OpenStreetMap](https://www.openstreetmap.org/copyright)"

    private static let licence: LocalizedStringKey =
        "Built on [Peregrine](https://github.com/ChrisABailey/Peregrine), used under the GNU Lesser General Public License, version 3 or later. Copies of the licence ship inside this app and are available on the website."

    private static let upstream: LocalizedStringKey =
        "Peregrine is a cross-platform port of FalconView\u{2122}, Copyright \u{00A9} 1994\u{2013}2011 Georgia Tech Research Corporation. It is an independent port, and is neither endorsed by nor affiliated with Georgia Tech Research Corporation."

    private static let disclaimer: LocalizedStringKey =
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
                    Text(Self.credit)
                }
                // One section, because §4c binds the two paragraphs together.
                Section {
                    Text(Self.licence)
                    Text(Self.upstream)
                }
                .font(.footnote)
                .foregroundStyle(.secondary)
                Section {
                    Text(Self.disclaimer)
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
