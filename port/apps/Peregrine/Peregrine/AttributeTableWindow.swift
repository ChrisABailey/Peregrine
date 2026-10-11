// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The attribute table: every feature of the current layer, one column per
/// field. Clicking a header sorts, the search field filters, selecting a row
/// selects the feature on the map and double-clicking edits it.
@MainActor
final class AttributeTableWindowController: NSWindowController, NSWindowDelegate,
    NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let table = NSTableView()
    private let filter = NSSearchField()
    private let countLabel = NSTextField(labelWithString: "")
    /// Set while the table is being reloaded, so the selection it restores is not a click.
    private var reloading = false

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 320),
                              styleMask: [.titled, .closable, .resizable, .miniaturizable],
                              backing: .buffered, defer: true)
        window.minSize = NSSize(width: 320, height: 160)
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        rebuildColumns()
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the rows and selects the selected feature's row.
    func reload() {
        window?.title = "Attributes — " + String(host.AttributeTableTitle())
        if table.numberOfColumns != Int(host.TableColumnCount()) { rebuildColumns() }
        reloading = true
        table.reloadData()
        let row = Int(host.SelectedTableRow())
        if row >= 0 {
            table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
            table.scrollRowToVisible(row)
        } else {
            table.deselectAll(nil)
        }
        reloading = false
        countLabel.stringValue = "\(host.TableRowCount()) features"
    }

    // MARK: Layout

    private func buildLayout(in window: NSWindow) {
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = false
        table.columnAutoresizingStyle = .noColumnAutoresizing
        table.dataSource = self
        table.delegate = self
        table.target = self
        table.doubleAction = #selector(editRow)
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.hasHorizontalScroller = true
        scroll.borderType = .bezelBorder

        filter.placeholderString = "Filter"
        filter.delegate = self
        let refresh = NSButton(title: "Reload", target: self, action: #selector(reloadLayer))
        let edit = NSButton(title: "Edit…", target: self, action: #selector(editRow))
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let top = NSStackView(views: [filter, spacer, countLabel])
        top.orientation = .horizontal
        let bottom = NSStackView(views: [refresh, NSView(), edit])
        bottom.orientation = .horizontal

        let stack = NSStackView(views: [top, scroll, bottom])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(stack)
        window.contentView = content
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            top.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            bottom.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            filter.widthAnchor.constraint(equalToConstant: 200),
        ])
    }

    /// One column per field; each header sorts by its column.
    private func rebuildColumns() {
        for column in table.tableColumns { table.removeTableColumn(column) }
        for c in 0..<host.TableColumnCount() {
            let column = NSTableColumn(identifier: .init(String(c)))
            column.title = String(host.TableColumnTitle(c))
            column.width = host.TableColumnNumeric(c) ? 90 : 150
            column.sortDescriptorPrototype = NSSortDescriptor(key: String(c), ascending: true)
            if host.TableColumnNumeric(c) { column.headerCell.alignment = .right }
            table.addTableColumn(column)
        }
    }

    // MARK: Actions

    @objc private func reloadLayer() {
        _ = host.ReloadAttributeTable()
        reload()
    }

    @objc private func editRow() {
        let row = table.clickedRow >= 0 ? table.clickedRow : table.selectedRow
        guard row >= 0 else {
            NSSound.beep()
            return
        }
        let error = String(host.EditTableRow(Int32(row)))
        if !error.isEmpty { NSSound.beep() }
    }

    func controlTextDidChange(_ notification: Notification) {
        host.SetTableFilter(std.string(filter.stringValue))
        reload()
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.TableRowCount()) }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard let id = tableColumn?.identifier.rawValue, let c = Int32(id) else { return nil }
        let label = NSTextField(labelWithString: String(host.TableCell(Int32(row), c)))
        label.lineBreakMode = .byTruncatingTail
        if host.TableColumnNumeric(c) { label.alignment = .right }
        return label
    }

    func tableView(_ tableView: NSTableView, sortDescriptorsDidChange oldDescriptors: [NSSortDescriptor]) {
        guard let first = tableView.sortDescriptors.first, let key = first.key, let c = Int32(key) else {
            host.SortTable(-1, true)
            reload()
            return
        }
        host.SortTable(c, first.ascending)
        reload()
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        guard !reloading, table.selectedRow >= 0 else { return }
        _ = host.SelectTableRow(Int32(table.selectedRow))
    }

    func windowWillClose(_ notification: Notification) {
        host.CloseAttributeTable()
        onClose?()
    }
}
