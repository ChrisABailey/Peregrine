// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI

/// The point sheets and the draft behind them.
///
/// Two sheets, not one: a tap on a marker asks what the place is, and the
/// answer is a few lines with two tappable things in it, so the info sheet is
/// small and opens at the `.medium` detent. Editing is a `Form` of every field.
///
/// The draft is not the point, the same split `RouteDraft` has. `PPMapPoint`
/// is immutable and is what the document holds; `PointDraft` is what the user
/// is typing and may be half-filled or abandoned. The draft is owned by
/// `MapScreen` because "Pick on map" dismisses the editor.

// MARK: - Draft

/// One point being edited. A class because it is bound with `@ObservedObject`
/// into a form and must survive the editor being dismissed for the crosshair.
@MainActor
final class PointDraft: ObservableObject, Identifiable {
    /// The document's id, or 0 for a new point, which decides whether Save is
    /// an add or an update. `Identifiable` uses a separate `id` so a new draft
    /// and an edit of point 0 do not collide in SwiftUI's sheet identity.
    let pointId: Int64
    nonisolated let id = UUID()

    @Published var name: String
    @Published var coordinate: PPGeoPoint?
    @Published var shape: String
    @Published var colorHex: String
    @Published var phone: String
    @Published var url: String
    @Published var remarks: String

    /// A key into the document's `symbols` table, or 0 for none. Not a path
    /// into an icon directory.
    @Published var symbolId: Int64

    /// Carried through untouched because the editor does not offer them, so a
    /// round trip is lossless for a document authored elsewhere.
    let category: String
    let elevationFt: Double
    let sizePx: Double

    /// A new point, with no position until the Location row gives it one
    /// (`isSaveable` holds Save back until then). `sizePx` is inherited from
    /// a point already on the map so a new marker matches its neighbours; the
    /// pack sets the number in `stage_data.py`, and 22 is the empty-document
    /// fallback.
    init(newAt coordinate: PPGeoPoint?, sizePx: Double = 22) {
        pointId = 0
        name = ""
        self.coordinate = coordinate
        shape = PPPointShapeCircle
        colorHex = PointPalette.swatches[0].hex
        phone = ""
        url = ""
        remarks = ""
        symbolId = 0
        category = ""
        elevationFt = 0
        self.sizePx = sizePx > 0 ? sizePx : 22
    }

    init(editing point: PPMapPoint) {
        pointId = point.pointId
        name = point.name
        coordinate = point.coordinate
        shape = point.shape
        colorHex = point.colorHex
        phone = point.phone
        url = point.url
        remarks = point.remarks
        symbolId = point.symbolId
        category = point.category
        elevationFt = point.elevationFt
        sizePx = point.sizePx
    }

    /// A point needs a place and a name. The name is required because an
    /// unnamed marker cannot be told from the next one, and the info sheet
    /// would open on a blank title.
    var isSaveable: Bool {
        coordinate != nil &&
            !name.trimmingCharacters(in: .whitespaces).isEmpty
    }

    /// The draft as the document would hold it, with whitespace trimmed: a
    /// trailing space in a URL is the difference between a link that opens
    /// and one that does not.
    func mapPoint() -> PPMapPoint? {
        guard let coordinate else { return nil }
        func clean(_ s: String) -> String {
            s.trimmingCharacters(in: .whitespacesAndNewlines)
        }
        return PPMapPoint(
            pointId: pointId,
            name: clean(name),
            coordinate: coordinate,
            shape: shape,
            sizePx: sizePx,
            colorHex: colorHex,
            symbolId: symbolId,
            category: category,
            elevationFt: elevationFt,
            remarks: clean(remarks),
            phone: clean(phone),
            url: clean(url))
    }
}

// MARK: - Palette

/// The colours the editor offers, as the `#rrggbb` strings the document
/// stores. A fixed palette rather than a colour wheel: what matters on a map
/// read at arm's length is that two points are tellable apart. These are the
/// colours the pack's own set uses.
///
/// No alpha: `#rrggbbaa` with `aa` = 00 is how a document asks for a bare
/// embedded icon with no badge, which is not something to offer on a phone.
enum PointPalette {
    struct Swatch: Identifiable {
        let hex: String
        let name: String
        var id: String { hex }
        var color: Color { PointPalette.color(hex) ?? .gray }
    }

    static let swatches: [Swatch] = [
        Swatch(hex: "#c82828", name: "Red"),
        Swatch(hex: "#e1a01e", name: "Amber"),
        Swatch(hex: "#1e8c46", name: "Green"),
        Swatch(hex: "#285ad2", name: "Blue"),
        Swatch(hex: "#8246a0", name: "Plum"),
        Swatch(hex: "#4a5a6e", name: "Slate"),
        Swatch(hex: "#b45a10", name: "Rust"),
        Swatch(hex: "#1a1a1a", name: "Black"),
    ]

    /// `#rrggbb` or `#rrggbbaa` to a `Color`, or nil. Both spellings, because
    /// `fv::PointColorFromString` accepts both and a document may hold either.
    static func color(_ hex: String) -> Color? {
        var text = hex
        if text.hasPrefix("#") { text.removeFirst() }
        guard text.count == 6 || text.count == 8,
              let value = UInt32(text, radix: 16) else { return nil }
        let hasAlpha = text.count == 8
        let r = Double((value >> (hasAlpha ? 24 : 16)) & 0xFF) / 255.0
        let g = Double((value >> (hasAlpha ? 16 : 8)) & 0xFF) / 255.0
        let b = Double((value >> (hasAlpha ? 8 : 0)) & 0xFF) / 255.0
        let a = hasAlpha ? Double(value & 0xFF) / 255.0 : 1.0
        return Color(red: r, green: g, blue: b, opacity: a)
    }
}

// MARK: - Marker

/// A marker drawn the way the map draws it: the shape in the point's colour
/// with the embedded icon stamped on top.
///
/// This duplicates the overlay's rendering, which is `BuiltinSymbolLibrary`
/// display lists through `GeoDraw`. Sharing them would mean rasterising
/// through `CpuCanvas` into a `UIImage` for every picker cell. The
/// proportions here follow `ShapeRing` in `point_overlay.cpp`: triangle and
/// diamond share the circle's circumscribed radius, and the star's inner
/// radius is 0.42 of its outer.
///
/// The icon is the document's own PNG bytes, decoded by `UIImage(data:)`.
struct PointShapeMark: View {
    let shape: String
    let color: Color
    var size: CGFloat = 22
    /// The embedded artwork stamped on the badge, or nil for a bare shape.
    var icon: PPPointSymbol? = nil

    /// `kIconFractionOfBadge` in `point_overlay.cpp`. A square inscribed in a
    /// circle has a side of 0.707d; this is pulled in further because the
    /// badge may be a diamond and must still read as one.
    private static let iconFraction: CGFloat = 0.62

    var body: some View {
        ZStack {
            Canvas { context, canvasSize in
                let r = min(canvasSize.width, canvasSize.height) / 2
                let c = CGPoint(x: canvasSize.width / 2,
                                y: canvasSize.height / 2)
                let path = Self.path(shape, center: c, radius: r)
                context.fill(path, with: .color(color))
                // The black edge the map draws: without it a badge disappears
                // against a dark chart.
                context.stroke(path, with: .color(.black.opacity(0.85)),
                               lineWidth: shape == PPPointShapeCross ? 0 : 1.5)
            }
            if let image = Self.image(icon) {
                // Black, as the overlay stamps it: Maki artwork is black on
                // transparency, so `.template` with a black tint matches.
                Image(uiImage: image)
                    .resizable()
                    .renderingMode(.template)
                    .foregroundStyle(.black)
                    .frame(width: size * Self.iconFraction,
                           height: size * Self.iconFraction)
            }
        }
        .frame(width: size, height: size)
    }

    /// The decoded tile, or nil for no icon and for a blob that will not
    /// read. The overlay treats an unreadable blob the same way: bare shape.
    private static func image(_ symbol: PPPointSymbol?) -> UIImage? {
        guard let symbol, !symbol.imageData.isEmpty else { return nil }
        return UIImage(data: symbol.imageData)
    }

    private static func path(_ shape: String, center c: CGPoint,
                             radius r: CGFloat) -> Path {
        var p = Path()
        switch shape {
        case PPPointShapeSquare:
            p.addRect(CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
        case PPPointShapeTriangle:
            p.move(to: CGPoint(x: c.x, y: c.y - r))
            p.addLine(to: CGPoint(x: c.x + r * 0.866, y: c.y + r * 0.5))
            p.addLine(to: CGPoint(x: c.x - r * 0.866, y: c.y + r * 0.5))
            p.closeSubpath()
        case PPPointShapeDiamond:
            p.move(to: CGPoint(x: c.x, y: c.y - r))
            p.addLine(to: CGPoint(x: c.x + r, y: c.y))
            p.addLine(to: CGPoint(x: c.x, y: c.y + r))
            p.addLine(to: CGPoint(x: c.x - r, y: c.y))
            p.closeSubpath()
        case PPPointShapeCross:
            // Two bars rather than two strokes, so the fill above draws it.
            let w = r * 0.34
            p.addRect(CGRect(x: c.x - w, y: c.y - r, width: 2 * w, height: 2 * r))
            p.addRect(CGRect(x: c.x - r, y: c.y - w, width: 2 * r, height: 2 * w))
        case PPPointShapeStar:
            for i in 0..<10 {
                let a = -Double.pi / 2 + Double(i) * Double.pi / 5
                let rr = i % 2 == 0 ? r : r * 0.42
                let pt = CGPoint(x: c.x + rr * CGFloat(cos(a)),
                                 y: c.y + rr * CGFloat(sin(a)))
                if i == 0 { p.move(to: pt) } else { p.addLine(to: pt) }
            }
            p.closeSubpath()
        default:
            p.addEllipse(in: CGRect(x: c.x - r, y: c.y - r,
                                    width: 2 * r, height: 2 * r))
        }
        return p
    }
}

// MARK: - Info sheet

/// What a tap on a marker shows: the name, plus whichever of the phone, URL
/// and remarks the point actually has. An empty row is absent rather than
/// disabled, unlike "Current location" in the route sheet: there is nothing
/// to wait for.
///
/// The phone and web rows are `Link`s so the system's own handler opens them
/// and the long-press menu comes for free.
struct PointInfoSheet: View {
    let point: PPMapPoint
    /// The document row `point.symbolId` names, or nil for a bare shape.
    /// Resolved by the caller, which holds the palette.
    let icon: PPPointSymbol?
    let onEdit: () -> Void
    let onDelete: () -> Void
    let onClose: () -> Void

    @State private var confirmingDelete = false
    /// The share sheet's payload, or nil. A fresh id each time, so sharing
    /// twice presents twice.
    @State private var sharing: SharePayload?

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    HStack(spacing: 12) {
                        PointShapeMark(shape: point.shape,
                                       color: PointPalette.color(point.colorHex)
                                           ?? .red,
                                       size: 28,
                                       icon: icon)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(point.name.isEmpty ? "Unnamed" : point.name)
                                .font(.headline)
                            if !point.category.isEmpty {
                                Text(point.category)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                    .padding(.vertical, 2)
                }

                if point.dialURL != nil || point.webURL != nil {
                    Section {
                        if let dial = point.dialURL {
                            Link(destination: dial) {
                                Label {
                                    // The display string, not the dialled one:
                                    // `dialURL` has already stripped it to
                                    // what a switchboard wants.
                                    Text(point.phone)
                                } icon: {
                                    Image(systemName: "phone.fill")
                                }
                            }
                        }
                        if let web = point.webURL {
                            Link(destination: web) {
                                Label {
                                    Text(Self.displayHost(web))
                                        .lineLimit(1)
                                        .truncationMode(.middle)
                                } icon: {
                                    Image(systemName: "safari.fill")
                                }
                            }
                        }
                    }
                }

                if !point.remarks.isEmpty {
                    Section("Remarks") {
                        Text(point.remarks)
                    }
                }

                Section {
                    Text(String(format: "%.5f, %.5f", point.coordinate.latitude,
                                point.coordinate.longitude))
                        .font(.caption)
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                }

                // Two rows because they are two errands: Maps is this phone
                // getting directions from an app that knows roads beyond this
                // pack; Share turns the point into a `.gpx` plus a link for
                // somebody else. Maps is a `Link` like the rows above.
                Section {
                    if let maps = PointShare.mapsAppURL(for: point) {
                        Link(destination: maps) {
                            Label("Open in Maps", systemImage: "map.fill")
                        }
                    }
                    Button {
                        sharing = PointShare.payload(for: point)
                    } label: {
                        Label("Share", systemImage: "square.and.arrow.up")
                    }
                }

                Section {
                    Button("Delete Pin", role: .destructive) {
                        confirmingDelete = true
                    }
                }
            }
            .navigationTitle(point.name.isEmpty ? "Pin" : point.name)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done", action: onClose)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Edit", action: onEdit)
                }
            }
            .sheet(item: $sharing) { payload in
                ShareSheet(items: payload.items, subject: payload.subject)
            }
            // The only confirmation in the app: the store writes on every
            // edit, so a delete is gone before the sheet closes.
            .confirmationDialog("Delete this pin?", isPresented: $confirmingDelete,
                                titleVisibility: .visible) {
                Button("Delete", role: .destructive, action: onDelete)
                Button("Cancel", role: .cancel) {}
            } message: {
                Text(point.name.isEmpty ? "This cannot be undone."
                                        : "\(point.name) will be removed. This cannot be undone.")
            }
        }
    }

    /// A URL as a person reads it: host without the scheme or a leading
    /// `www.`, plus the path. The full string is still what opens.
    private static func displayHost(_ url: URL) -> String {
        guard var host = url.host() else { return url.absoluteString }
        if host.hasPrefix("www.") { host.removeFirst(4) }
        let path = url.path()
        return path.isEmpty || path == "/" ? host : host + path
    }
}

// MARK: - Editor

/// Every field of a point this app lets a user change, plus its location and
/// whether it still exists.
struct PointEditSheet: View {
    @ObservedObject var draft: PointDraft

    /// The document's embedded artwork. Empty hides the picker rather than
    /// showing an empty one.
    let symbols: [PPPointSymbol]

    /// The last fix, or nil, for the Location row's "Current location".
    let currentLocation: PPGeoPoint?

    /// The crosshair, for the Location row's "Pick on map".
    let onPickLocation: () -> Void
    /// A place was found by name. The row writes the coordinate into the
    /// draft; this moves the map, which is the caller's job.
    let onFrameFound: (PPSearchResult) -> Void
    let onSave: () -> Void
    let onCancel: () -> Void

    /// Puts the cursor in the name field for a new point.
    @FocusState private var nameFocused: Bool

    var body: some View {
        NavigationStack {
            Form {
                Section("Name") {
                    TextField("Our Beach Spot", text: $draft.name)
                        .focused($nameFocused)
                        .submitLabel(.done)
                }

                Section {
                    // `.never` autocapitalisation on the URL: iOS would
                    // otherwise turn "kiawah.com" into "Kiawah.com", harmless
                    // for a host and wrong for a path.
                    TextField("Phone", text: $draft.phone)
                        .keyboardType(.phonePad)
                        .textContentType(.telephoneNumber)
                    TextField("Website", text: $draft.url)
                        .keyboardType(.URL)
                        .textContentType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                } header: {
                    Text("Contact")
                }

                Section("Remarks") {
                    // Two to five lines: a box that grew past half the screen
                    // would push the buttons out of reach.
                    TextField("(Free Text)", text: $draft.remarks,
                              axis: .vertical)
                        .lineLimit(2...5)
                }

                Section {
                    shapeRow
                    colorRow
                    // No explicit `Divider()`: a `Form` already separates its
                    // rows, and one here made an empty row with a hairline.
                    if !symbols.isEmpty { iconRow }
                } header: {
                    Text("Symbol")
                } footer: {
                    if !symbols.isEmpty {
                        Text("Choose shape, color & icon")
                    }
                }

                Section {
                    // The route sheet's own row, so a point and a waypoint
                    // are placed by the same two choices.
                    LocationRow(
                        title: "Place",
                        coordinate: draft.coordinate,
                        currentLocation: currentLocation,
                        onUseCurrent: { draft.coordinate = $0 },
                        onPickOnMap: onPickLocation,
                        onSearchResult: { result in
                            draft.coordinate = result.coordinate
                            onFrameFound(result)
                        })
                } header: {
                    Text("Location")
                } footer: {
                    if draft.coordinate == nil {
                        Text("Choose the location before saving.")
                    }
                }
            }
            .navigationTitle(draft.pointId == 0 ? "New Pin" : "Edit Pin")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel", action: onCancel)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save", action: onSave).disabled(!draft.isSaveable)
                }
            }
            .onAppear {
                // New points only: on an edit the keyboard would hide half
                // the form the user came to read.
                if draft.pointId == 0 && draft.name.isEmpty { nameFocused = true }
            }
        }
    }

    /// The six shapes drawn as themselves, since a list of the words "circle"
    /// and "diamond" is a worse way to choose a picture.
    private var shapeRow: some View {
        HStack(spacing: 14) {
            ForEach(PPPointShapeNames(), id: \.self) { name in
                Button {
                    draft.shape = name
                } label: {
                    PointShapeMark(
                        shape: name,
                        color: PointPalette.color(draft.colorHex) ?? .red,
                        size: 26,
                        // With the chosen icon, so the row previews the whole
                        // marker: a busy icon inside a star is unreadable, and
                        // seeing that here is cheaper than on the map.
                        icon: chosenIcon)
                    .padding(6)
                    .background {
                        // A ring rather than a change to the swatch: a marker
                        // that changed colour to say "chosen" would misreport
                        // what it looks like on the map.
                        if draft.shape == name {
                            RoundedRectangle(cornerRadius: 8)
                                .strokeBorder(Color.accentColor, lineWidth: 2)
                        }
                    }
                }
                .buttonStyle(.plain)
                .accessibilityLabel(name)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 4)
    }

    private var colorRow: some View {
        HStack(spacing: 12) {
            ForEach(PointPalette.swatches) { swatch in
                Button {
                    draft.colorHex = swatch.hex
                } label: {
                    Circle()
                        .fill(swatch.color)
                        .frame(width: 26, height: 26)
                        .overlay(Circle().strokeBorder(.black.opacity(0.3),
                                                       lineWidth: 1))
                        .padding(4)
                        .background {
                            if draft.colorHex.caseInsensitiveCompare(swatch.hex)
                                == .orderedSame {
                                Circle().strokeBorder(Color.accentColor,
                                                      lineWidth: 2)
                            }
                        }
                }
                .buttonStyle(.plain)
                .accessibilityLabel(swatch.name)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 4)
    }

    /// The palette row the draft names, or nil.
    private var chosenIcon: PPPointSymbol? {
        draft.symbolId == 0 ? nil
                            : symbols.first { $0.symbolId == draft.symbolId }
    }

    /// The document's artwork, as itself.
    ///
    /// A horizontal scroller rather than a grid: a grid would push the
    /// Location section off screen, and that is the one thing a new point
    /// cannot be saved without. The first cell is "no icon" and is a cell
    /// rather than a switch, because clearing is the same kind of choice as
    /// picking and needs its own hit target.
    private var iconRow: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 10) {
                iconCell(nil, label: "No icon") {
                    Image(systemName: "slash.circle")
                        .font(.system(size: 20))
                        .foregroundStyle(.secondary)
                        .frame(width: 28, height: 28)
                }
                ForEach(symbols, id: \.symbolId) { symbol in
                    iconCell(symbol.symbolId, label: symbol.name) {
                        // The badge, not the bare tile: a black glyph on a
                        // white sheet looks nothing like one on a coloured
                        // badge.
                        PointShapeMark(
                            shape: draft.shape,
                            color: PointPalette.color(draft.colorHex) ?? .red,
                            size: 28,
                            icon: symbol)
                    }
                }
            }
            // The row scrolls edge to edge, so it needs its own inset or the
            // end cells sit under the section's rounded corners.
            .padding(.horizontal, 2)
            .padding(.vertical, 4)
        }
    }

    private func iconCell<Content: View>(
        _ symbolId: Int64?, label: String,
        @ViewBuilder content: () -> Content) -> some View {
        let chosen = (symbolId ?? 0) == draft.symbolId
        return Button {
            draft.symbolId = symbolId ?? 0
        } label: {
            content()
                .padding(5)
                .background {
                    if chosen {
                        RoundedRectangle(cornerRadius: 8)
                            .strokeBorder(Color.accentColor, lineWidth: 2)
                    }
                }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(label)
    }
}
