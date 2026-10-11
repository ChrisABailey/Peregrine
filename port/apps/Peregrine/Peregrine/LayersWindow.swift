// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The layers panel: the overlay stack top first, each row with a visibility
/// check box, its name and, for layers that support it, an opacity slider.
/// Selecting a row makes that overlay current; Move Up/Down and dragging
/// reorder the stack. Rows come from `DeskHost`'s layer snapshot.
@MainActor
final class LayersWindowController: NSWindowController, NSWindowDelegate,
    NSTableViewDataSource, NSTableViewDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let table = NSTableView()
    private let upButton = NSButton(title: "Move Up", target: nil, action: nil)
    private let downButton = NSButton(title: "Move Down", target: nil, action: nil)
    private static let rowType = NSPasteboard.PasteboardType("org.peregrine.layer-row")
    /// Set while the table is being reloaded, so the selection it restores is not a click.
    private var reloading = false

    init(host: fv.desk.DeskHost) {
        self.host = host
        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 360, height: 300),
                            styleMask: [.titled, .closable, .resizable, .utilityWindow],
                            backing: .buffered, defer: true)
        panel.title = "Layers"
        panel.minSize = NSSize(width: 280, height: 160)
        panel.isFloatingPanel = true
        panel.hidesOnDeactivate = true
        super.init(window: panel)
        panel.delegate = self
        buildLayout(in: panel)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the rows; the current overlay's row is selected.
    func reload() {
        reloading = true
        table.reloadData()
        let count = Int(host.LayerCount())
        if let current = (0..<count).first(where: { host.LayerCurrent(Int32($0)) }) {
            table.selectRowIndexes(IndexSet(integer: current), byExtendingSelection: false)
        } else {
            table.deselectAll(nil)
        }
        reloading = false
        updateButtons()
    }

    // MARK: Layout

    private func buildLayout(in window: NSWindow) {
        for (id, width) in [("visible", 24.0), ("name", 180.0), ("opacity", 110.0)] {
            let column = NSTableColumn(identifier: .init(id))
            column.width = width
            column.resizingMask = id == "name" ? .autoresizingMask : []
            table.addTableColumn(column)
        }
        table.headerView = nil
        table.rowHeight = 24
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        table.registerForDraggedTypes([Self.rowType])
        table.draggingDestinationFeedbackStyle = .gap
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder

        upButton.target = self
        upButton.action = #selector(moveLayerUp)
        downButton.target = self
        downButton.action = #selector(moveLayerDown)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [upButton, downButton, spacer])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [scroll, buttons])
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
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
        ])
    }

    private func updateButtons() {
        let row = table.selectedRow
        upButton.isEnabled = row > 0
        downButton.isEnabled = row >= 0 && row + 1 < Int(host.LayerCount())
    }

    // MARK: Actions

    private func move(from: Int, to: Int) {
        let error = String(host.MoveLayer(Int32(from), Int32(to)))
        if !error.isEmpty { NSSound.beep() }
        reload()
    }

    @objc private func moveLayerUp() {
        let row = table.selectedRow
        if row > 0 { move(from: row, to: row - 1) }
    }

    @objc private func moveLayerDown() {
        let row = table.selectedRow
        if row >= 0 { move(from: row, to: row + 1) }
    }

    @objc private func toggleVisible(_ sender: NSButton) {
        let row = table.row(for: sender)
        guard row >= 0 else { return }
        _ = host.SetLayerVisible(Int32(row), sender.state == .on)
    }

    @objc private func opacityChanged(_ sender: NSSlider) {
        let row = table.row(for: sender)
        guard row >= 0 else { return }
        _ = host.SetLayerOpacity(Int32(row), Int32(sender.integerValue))
        sender.toolTip = "Opacity \(sender.integerValue)%"
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.LayerCount()) }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let r = Int32(row)
        switch tableColumn?.identifier.rawValue {
        case "visible":
            let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleVisible(_:)))
            box.state = host.LayerVisible(r) ? .on : .off
            box.toolTip = "Show or hide"
            return box
        case "opacity":
            guard host.LayerHasOpacity(r) else { return NSView() }
            let slider = NSSlider(value: Double(host.LayerOpacity(r)), minValue: 0, maxValue: 100,
                                  target: self, action: #selector(opacityChanged(_:)))
            slider.controlSize = .small
            slider.isContinuous = true
            slider.toolTip = "Opacity \(host.LayerOpacity(r))%"
            return slider
        default:
            let name = String(host.LayerName(r)) + (host.LayerDirty(r) ? " \u{2022}" : "")
            let label = NSTextField(labelWithString: name)
            label.lineBreakMode = .byTruncatingTail
            label.toolTip = String(host.LayerTypeName(r))
            if host.LayerCurrent(r) { label.font = .boldSystemFont(ofSize: NSFont.systemFontSize) }
            let icon = String(host.LayerIcon(r))
            guard let image = CommandToolbarItem.image(for: icon, label: icon) else { return label }
            let imageView = NSImageView(image: image)
            imageView.setContentHuggingPriority(.required, for: .horizontal)
            let cell = NSStackView(views: [imageView, label])
            cell.orientation = .horizontal
            cell.spacing = 6
            return cell
        }
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        updateButtons()
        guard !reloading, table.selectedRow >= 0 else { return }
        _ = host.SelectLayer(Int32(table.selectedRow))
        reload()
    }

    func tableView(_ tableView: NSTableView, pasteboardWriterForRow row: Int) -> NSPasteboardWriting? {
        let item = NSPasteboardItem()
        item.setString(String(row), forType: Self.rowType)
        return item
    }

    func tableView(_ tableView: NSTableView, validateDrop info: NSDraggingInfo, proposedRow row: Int,
                   proposedDropOperation dropOperation: NSTableView.DropOperation) -> NSDragOperation {
        dropOperation == .above ? .move : []
    }

    func tableView(_ tableView: NSTableView, acceptDrop info: NSDraggingInfo, row: Int,
                   dropOperation: NSTableView.DropOperation) -> Bool {
        guard let text = info.draggingPasteboard.pasteboardItems?.first?.string(forType: Self.rowType),
              let from = Int(text) else { return false }
        // A drop "above row" lands at that index, or one less once the source is taken out.
        let to = from < row ? row - 1 : row
        guard to != from, to >= 0, to < Int(host.LayerCount()) else { return false }
        move(from: from, to: to)
        return true
    }

    func windowWillClose(_ notification: Notification) { onClose?() }
}
