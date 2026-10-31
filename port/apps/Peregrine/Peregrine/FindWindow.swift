// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// Map ▸ Go To / Find…: one field that takes a coordinate or a name.
///
/// A coordinate (decimal, DMS, MGRS) is the only answer when the text parses
/// as one; otherwise the overlays, the road network and the base map on
/// screen are searched (`DeskHost::Search`) and listed under a heading per
/// group. Typing searches after a short pause; Return searches at once and
/// goes there when there is a single answer; a double-click goes to a row.
@MainActor
final class FindWindowController: NSWindowController, NSWindowDelegate, NSTableViewDataSource,
    NSTableViewDelegate, NSSearchFieldDelegate
{
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private enum Line {
        case heading(String)
        case result(Int32)
    }

    private static let viewOnlyKey = "FindMapViewOnly"
    private let field = NSSearchField()
    private let viewOnly = NSButton(checkboxWithTitle: "Search the map in this view only",
                                    target: nil, action: nil)
    private let table = NSTableView()
    private let statusLabel = NSTextField(labelWithString: "")
    private let goButton = NSButton(title: "Go To", target: nil, action: nil)
    private var lines: [Line] = []
    private var pending: DispatchWorkItem?

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 680, height: 440),
                             styleMask: [.titled, .closable, .resizable, .utilityWindow],
                             backing: .buffered, defer: true)
        window.title = "Go To / Find"
        window.minSize = NSSize(width: 420, height: 260)
        window.hidesOnDeactivate = false
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    private func buildLayout(in window: NSWindow) {
        field.placeholderString = "33.74 -84.39, 33 44 55 N 84 23 17 W, MGRS, or a name"
        field.delegate = self
        field.sendsSearchStringImmediately = false
        field.target = self
        field.action = #selector(searchNow)

        let defaults = UserDefaults.standard
        viewOnly.state = defaults.object(forKey: Self.viewOnlyKey) as? Bool ?? true ? .on : .off
        viewOnly.target = self
        viewOnly.action = #selector(viewOnlyChanged)

        for (id, title, width) in [("name", "Name", 230.0), ("detail", "Kind", 200.0),
                                   ("source", "From", 100.0), ("distance", "Distance", 80.0)] {
            let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(id))
            column.title = title
            column.width = width
            table.addTableColumn(column)
        }
        table.dataSource = self
        table.delegate = self
        table.usesAlternatingRowBackgroundColors = true
        table.columnAutoresizingStyle = .uniformColumnAutoresizingStyle
        table.allowsMultipleSelection = false
        table.target = self
        table.doubleAction = #selector(goToSelection)
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        statusLabel.textColor = .secondaryLabelColor
        statusLabel.lineBreakMode = .byTruncatingTail
        statusLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        statusLabel.setContentHuggingPriority(.defaultLow, for: .horizontal)

        goButton.target = self
        goButton.action = #selector(goToSelection)
        goButton.isEnabled = false
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        for button in [close, goButton] { button.setContentHuggingPriority(.defaultHigh, for: .horizontal) }
        let buttons = NSStackView(views: [statusLabel, close, goButton])
        buttons.orientation = .horizontal
        buttons.distribution = .fill

        let stack = NSStackView(views: [field, viewOnly, scroll, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 16, bottom: 12, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(stack)
        window.contentView = content
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            field.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])
        scroll.setContentHuggingPriority(.defaultLow, for: .vertical)
    }

    /// Puts the cursor in the field with its text selected, ready to type over.
    func focusField() {
        window?.makeFirstResponder(field)
        field.selectText(nil)
    }

    // MARK: Searching

    func controlTextDidChange(_ obj: Notification) {
        pending?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.runSearch() }
        pending = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2, execute: work)
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        if selector == #selector(NSResponder.insertNewline(_:)) {
            searchNow()
            if host.SearchRowCount() == 1 { go(row: 0) }
            return true
        }
        if selector == #selector(NSResponder.moveDown(_:)), !lines.isEmpty {
            window?.makeFirstResponder(table)
            selectFirstResult()
            return true
        }
        return false
    }

    @objc private func searchNow() {
        pending?.cancel()
        pending = nil
        runSearch()
    }

    @objc private func viewOnlyChanged() {
        UserDefaults.standard.set(viewOnly.state == .on, forKey: Self.viewOnlyKey)
        runSearch()
    }

    private func runSearch() {
        let n = host.Search(std.string(field.stringValue), viewOnly.state == .on)
        lines = []
        var group: Int32 = -1
        for i in 0..<n {
            let g = host.SearchRowGroup(i)
            if g != group {
                group = g
                lines.append(.heading(String(host.SearchRowGroupTitle(i))))
            }
            lines.append(.result(i))
        }
        table.reloadData()
        statusLabel.stringValue = String(host.SearchStatus())
        updateGoButton()
    }

    // MARK: Going there

    @objc private func goToSelection() {
        let row = table.selectedRow
        guard row >= 0, row < lines.count, case let .result(i) = lines[row] else { return }
        go(row: i)
    }

    private func go(row: Int32) { host.GoToSearchRow(row) }

    private func selectFirstResult() {
        if let first = lines.firstIndex(where: { if case .result = $0 { return true } else { return false } }) {
            table.selectRowIndexes(IndexSet(integer: first), byExtendingSelection: false)
            table.scrollRowToVisible(first)
        }
    }

    private func updateGoButton() {
        let row = table.selectedRow
        if row >= 0, row < lines.count, case .result = lines[row] {
            goButton.isEnabled = true
        } else {
            goButton.isEnabled = false
        }
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { lines.count }

    func tableView(_ tableView: NSTableView, isGroupRow row: Int) -> Bool {
        if case .heading = lines[row] { return true }
        return false
    }

    func tableView(_ tableView: NSTableView, shouldSelectRow row: Int) -> Bool {
        if case .result = lines[row] { return true }
        return false
    }

    func tableViewSelectionDidChange(_ notification: Notification) { updateGoButton() }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        switch lines[row] {
        case let .heading(title):
            // A group row is drawn once across the table with a nil column.
            guard tableColumn == nil else { return nil }
            let label = NSTextField(labelWithString: title)
            label.font = .boldSystemFont(ofSize: NSFont.smallSystemFontSize)
            return label
        case let .result(i):
            guard let id = tableColumn?.identifier.rawValue else { return nil }
            let text: String
            switch id {
            case "name": text = String(host.SearchRowTitle(i))
            case "detail": text = String(host.SearchRowDetail(i))
            case "source": text = String(host.SearchRowSource(i))
            default: text = Self.distanceText(host.SearchRowDistance(i))
            }
            let label = NSTextField(labelWithString: text)
            label.lineBreakMode = .byTruncatingTail
            if id == "distance" { label.alignment = .right }
            return label
        }
    }

    /// "350 m" under a kilometre, "12.4 km" beyond.
    private static func distanceText(_ meters: Double) -> String {
        if meters < 1000 { return "\(Int(meters.rounded())) m" }
        return String(format: "%.1f km", meters / 1000)
    }

    @objc private func closeWindow() { close() }

    func windowWillClose(_ notification: Notification) {
        pending?.cancel()
        onClose?()
    }
}
