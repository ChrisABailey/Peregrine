// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import PippinKit
import SwiftUI
import UIKit

/// The search box and its result list, used in two places: the route sheet's
/// first phase ("where to?"), and a stop's menu, which presents it as a sheet
/// to fill one row.
///
/// The field is the only control. There is no scope switch, because a rider
/// planning a route wants what is near them without first being asked a
/// question about scope.
///
/// The scope is not uniform, and the empty state explains it: the rider's own
/// points and the whole road network are searched wherever the map is
/// pointed, while the chart's other named features are limited to what is on
/// screen, because a tile pyramid with no name index cannot answer a text
/// query any other way. `search.chart_in_view_only` in `pippin.ini` is the
/// knob for testing that.
///
/// A row is a symbol and a name. The vocabulary is three wide: Point for the
/// rider's saved places, Road for the road network, POI for everything else
/// the chart names (`PPSearchRules.h` maps a provider's `detail` onto them).
/// The word itself is the accessibility label, since a shape is faster to
/// scan and invisible to VoiceOver.
struct SearchView: View {
    /// What the box is for, shown above the field: "Where to?" from the route
    /// sheet, or "Start"/"Via 1"/"End" from a stop's menu.
    let prompt: String
    /// Asks the stack. Handed in rather than reached for, so this view has
    /// never heard of `MapModel` and a preview can drive it with an array.
    let search: (String) async -> [PPSearchResult]
    /// A row was chosen. The caller decides what that means.
    let onChoose: (PPSearchResult) -> Void
    /// This term was searched and answered with something. Called past the
    /// cancellation check, so never a prefix the next keystroke discarded.
    let onSearched: (String) -> Void

    /// The unit preference, observed directly: this view is presented in a
    /// `.sheet` and inherits none of the map screen's environment. See
    /// `DisplayUnits`.
    @ObservedObject private var display = DisplayUnits.shared

    @State private var text: String
    @State private var results: [PPSearchResult] = []
    /// A search is in flight. Distinct from `results.isEmpty`, which is an
    /// answer; see `emptyState`.
    @State private var searching = false
    /// A query has completed for the current text. Until it has, "Nothing
    /// found" would be a claim about a question nobody has answered.
    @State private var answered = false

    @FocusState private var fieldFocused: Bool

    /// Seeds the field with `initialText` (the rider's last successful search,
    /// or the pack's own). Seeded into `@State` at init rather than assigned
    /// in `onAppear`, so the first `.task(id: text)` already has the term and
    /// the list is populated before the sheet finishes appearing.
    init(prompt: String, initialText: String = "",
         search: @escaping (String) async -> [PPSearchResult],
         onChoose: @escaping (PPSearchResult) -> Void,
         onSearched: @escaping (String) -> Void = { _ in }) {
        self.prompt = prompt
        self.search = search
        self.onChoose = onChoose
        self.onSearched = onSearched
        _text = State(initialValue: initialText)
    }

    /// The icon column's width. `@ScaledMetric` so it grows with the reader's
    /// type size, as the names beside it do.
    @ScaledMetric(relativeTo: .body) private var iconSlot: CGFloat = 22

    /// How long a pause in the typing counts as a question. Queries over this
    /// pack return in milliseconds, so the debounce protects the list from
    /// re-ordering under a thumb rather than protecting the search.
    private static let debounce = Duration.milliseconds(200)

    var body: some View {
        VStack(spacing: 0) {
            field
            Divider()
            content
        }
        // `.task(id:)` provides the cancellation: a new keystroke replaces
        // the task and cancels the sleep, so there is no timer to invalidate.
        // It cannot cancel a query already inside the C++, which nothing over
        // this pack runs long enough to need.
        .task(id: text) {
            let query = text.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !query.isEmpty else {
                results = []
                searching = false
                answered = false
                return
            }
            searching = true
            try? await Task.sleep(for: Self.debounce)
            if Task.isCancelled { return }
            let found = await search(query)
            // A cancelled query still reaches the C++ and still returns; this
            // check is what makes it not count.
            if Task.isCancelled { return }
            results = found
            searching = false
            answered = true
            if !found.isEmpty { onSearched(query) }
        }
        // The keyboard comes up only when the field is empty: a pre-filled
        // box has already answered the question, and the keyboard would hide
        // the rows the pre-fill exists to show.
        .onAppear { fieldFocused = text.isEmpty }
        // Select all when the field is tapped, or typing "marsh" into a box
        // reading "Boardwalk" asks for "Boardwalkmarsh". SwiftUI has no
        // spelling for this. The only text field in this sheet is the one
        // above; the async hop is because the selection is set after the
        // field becomes first responder, and setting it inside the
        // notification is undone by what follows.
        .onReceive(NotificationCenter.default.publisher(
            for: UITextField.textDidBeginEditingNotification)) { note in
            guard let field = note.object as? UITextField else { return }
            DispatchQueue.main.async { field.selectAll(nil) }
        }
    }

    private var field: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .foregroundStyle(.secondary)
            TextField(prompt, text: $text)
                .focused($fieldFocused)
                .textFieldStyle(.plain)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.words)
                // Nothing to submit: the list is already live. Return just
                // puts the keyboard away.
                .submitLabel(.done)
                .onSubmit { fieldFocused = false }
            if !text.isEmpty {
                Button {
                    text = ""
                    fieldFocused = true
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Clear")
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    @ViewBuilder
    private var content: some View {
        if results.isEmpty {
            emptyState
        } else {
            List(Array(results.enumerated()), id: \.offset) { _, result in
                Button {
                    onChoose(result)
                } label: {
                    row(result)
                }
                .buttonStyle(.plain)
            }
            .listStyle(.plain)
        }
    }

    /// One result: symbol, name, and the distance on the trailing edge. The
    /// distance is set apart rather than appended because it is how a rider
    /// chooses between two rows that read the same. It is the same metre the
    /// router prices a leg in, so a row and the route agree.
    ///
    /// The symbol sits in a fixed-width slot so every name starts at the same
    /// x; a ragged left edge makes an icon column read as clutter.
    private func row(_ result: PPSearchResult) -> some View {
        HStack(spacing: 10) {
            SearchKindIcon(kind: result.kind)
                .frame(width: iconSlot)
            Text(result.title)
                .font(.body)
                .lineLimit(2)
            Spacer(minLength: 12)
            if result.distanceMeters > 0 {
                Text(display.units.distance(result.distanceMeters))
                    .font(.caption)
                    .monospacedDigit()
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 6)
        .contentShape(Rectangle())
        // One element, read as "Ruddy Turnstone, Road, 825 m". Without this
        // the shape is silent and VoiceOver loses the kind.
        .accessibilityElement(children: .combine)
    }

    /// Three empty states: nothing typed, a query in flight, and a query that
    /// found nothing. Only the last is about the rider asking for something
    /// that is not there, and it explains the scope asymmetry.
    @ViewBuilder
    private var emptyState: some View {
        VStack(spacing: 8) {
            Spacer()
            if text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                Text("Type a place, a road, or one of your pins.")
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            } else if searching || !answered {
                ProgressView()
            } else {
                Text("Nothing found.")
                    .foregroundStyle(.secondary)
                Text("Your pins and roads are searched everywhere. Other places on the map are found only where you can see them.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
            Spacer()
        }
        .frame(maxWidth: .infinity)
        .padding(.horizontal, 32)
    }

}

// MARK: - Kind symbols

/// The symbol for a result's kind.
///
/// Point is `mappin.and.ellipse`, the same glyph the points button wears, so
/// the list and the button that shows those points read as one thing. Road is
/// drawn, because SF Symbols has no small squiggle that says "street" rather
/// than "route". POI is `drop.fill` turned upside down, which is the familiar
/// map-marker silhouette and keeps it in the same optical family as the pin.
struct SearchKindIcon: View {
    let kind: PPSearchKind

    var body: some View {
        icon
            // A shape carries nothing to VoiceOver. `PPSearchKindWord` is the
            // one definition of this vocabulary and the mac test asserts on it.
            .accessibilityLabel(PPSearchKindWord(kind))
    }

    @ViewBuilder
    private var icon: some View {
        switch kind {
        case .point:
            Image(systemName: "mappin.and.ellipse")
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(Color.accentColor)
        case .road:
            RoadSquiggle()
                .stroke(style: StrokeStyle(lineWidth: 2.4, lineCap: .round,
                                           lineJoin: .round))
                .foregroundStyle(.secondary)
                .frame(width: 19, height: 19)
        case .poi:
            Image(systemName: "drop.fill")
                .font(.system(size: 15, weight: .medium))
                // Rotated, not mirrored: a teardrop is symmetric about its
                // own axis.
                .rotationEffect(.degrees(180))
                .foregroundStyle(.secondary)
        @unknown default:
            Image(systemName: "questionmark")
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(.secondary)
        }
    }
}

/// A winding road: three points and the two bends between them, thrown
/// opposite ways.
///
/// One quadratic per segment rather than a cubic. A cubic's two controls can
/// double back, which at seventeen points reads as an integral sign rather
/// than a road; a single control cannot. The shape leans wide rather than
/// tall so it reads as a road going away rather than a letter standing up.
///
/// Drawn in unit space and scaled by the frame, so one shape serves every
/// Dynamic Type size. The 2.4-point stroke is what it takes to carry the same
/// visual weight as the filled teardrop beside it.
struct RoadSquiggle: Shape {
    func path(in rect: CGRect) -> Path {
        func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint {
            CGPoint(x: rect.minX + x * rect.width,
                    y: rect.minY + y * rect.height)
        }
        var path = Path()
        path.move(to: p(0.08, 0.76))
        path.addQuadCurve(to: p(0.50, 0.50), control: p(0.34, 0.76))
        path.addQuadCurve(to: p(0.92, 0.24), control: p(0.66, 0.24))
        return path
    }
}
