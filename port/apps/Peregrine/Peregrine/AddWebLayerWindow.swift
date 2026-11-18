// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// Overlay ▸ Add Web Layer…: a service URL, the layers it offers, and Add.
///
/// Connect lists the layers on the core's worker (`DeskHost::WebConnect`);
/// the map window's tick calls `reload()` when the listing finishes. Ticked
/// layers open as read-only feature overlays. The URL box offers the
/// services added before.
@MainActor
final class AddWebLayerWindowController: NSWindowController, NSWindowDelegate, NSTableViewDataSource,
    NSTableViewDelegate, NSComboBoxDelegate
{
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let urlBox = NSComboBox()
    private let connectButton = NSButton(title: "Connect", target: nil, action: nil)
    private let table = NSTableView()
    private let descriptionLabel = NSTextField(wrappingLabelWithString: "")
    private let statusLabel = NSTextField(labelWithString: "")
    private let spinner = NSProgressIndicator()
    private let addButton = NSButton(title: "Add", target: nil, action: nil)

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 640, height: 420),
                             styleMask: [.titled, .closable, .resizable, .utilityWindow],
                             backing: .buffered, defer: true)
        window.title = "Add Web Layer"
        window.minSize = NSSize(width: 460, height: 300)
        window.hidesOnDeactivate = false
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reloadRecent()
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    private func buildLayout(in window: NSWindow) {
        urlBox.placeholderString = "OGC API – Features or Esri FeatureServer / MapServer URL"
        urlBox.completes = true
        urlBox.usesDataSource = false
        urlBox.delegate = self
        urlBox.target = self
        urlBox.action = #selector(connect)
        urlBox.setContentHuggingPriority(.defaultLow, for: .horizontal)
        connectButton.target = self
        connectButton.action = #selector(connect)
        connectButton.setContentHuggingPriority(.defaultHigh, for: .horizontal)
        spinner.style = .spinning
        spinner.controlSize = .small
        spinner.isDisplayedWhenStopped = false
        let urlRow = NSStackView(views: [urlBox, spinner, connectButton])
        urlRow.orientation = .horizontal

        for (id, title, width) in [("add", "", 24.0), ("title", "Layer", 300.0),
                                   ("geometry", "Geometry", 100.0), ("id", "Id", 120.0)] {
            let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(id))
            column.title = title
            column.width = width
            if id == "add" {
                column.minWidth = width
                column.maxWidth = width
            }
            table.addTableColumn(column)
        }
        table.dataSource = self
        table.delegate = self
        table.usesAlternatingRowBackgroundColors = true
        table.columnAutoresizingStyle = .uniformColumnAutoresizingStyle
        table.target = self
        table.doubleAction = #selector(addDoubleClicked)
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        descriptionLabel.textColor = .secondaryLabelColor
        descriptionLabel.maximumNumberOfLines = 3
        descriptionLabel.lineBreakMode = .byTruncatingTail

        statusLabel.textColor = .secondaryLabelColor
        statusLabel.lineBreakMode = .byTruncatingTail
        statusLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        statusLabel.setContentHuggingPriority(.defaultLow, for: .horizontal)
        addButton.target = self
        addButton.action = #selector(addLayers)
        addButton.keyEquivalent = "\r"
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        for button in [close, addButton] { button.setContentHuggingPriority(.defaultHigh, for: .horizontal) }
        let buttons = NSStackView(views: [statusLabel, close, addButton])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [urlRow, scroll, descriptionLabel, buttons])
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
            urlRow.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            descriptionLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])
        scroll.setContentHuggingPriority(.defaultLow, for: .vertical)
    }

    /// Puts the cursor in the URL box with its text selected.
    func focusField() {
        window?.makeFirstResponder(urlBox)
        urlBox.selectText(nil)
    }

    /// Fills the URL box's list with the remembered services, most recent first.
    private func reloadRecent() {
        urlBox.removeAllItems()
        for i in 0..<host.WebRecentCount() { urlBox.addItem(withObjectValue: String(host.WebRecentUrl(i))) }
        if urlBox.stringValue.isEmpty, host.WebRecentCount() > 0 {
            urlBox.stringValue = String(host.WebRecentUrl(0))
        }
    }

    /// Re-reads the connection: its state, the layer rows and the status line.
    func reload() {
        let connecting = host.WebState() == 1
        if connecting { spinner.startAnimation(nil) } else { spinner.stopAnimation(nil) }
        connectButton.isEnabled = !connecting
        statusLabel.stringValue = String(host.WebStatus())
        table.reloadData()
        updateSelection()
    }

    private func updateSelection() {
        var any = false
        for i in 0..<host.WebLayerCount() where host.WebLayerChosen(i) { any = true }
        addButton.isEnabled = any
        let row = table.selectedRow
        descriptionLabel.stringValue = row >= 0 ? String(host.WebLayerDescription(Int32(row))) : ""
    }

    // MARK: Actions

    @objc private func connect() {
        let error = String(host.WebConnect(std.string(urlBox.stringValue)))
        reload()
        if !error.isEmpty { statusLabel.stringValue = error }
    }

    @objc private func addLayers() {
        let error = String(host.AddWebLayers())
        if error.isEmpty {
            close()
        } else {
            statusLabel.stringValue = error
            reloadRecent()
        }
    }

    /// A double-click ticks that row alone and adds it.
    @objc private func addDoubleClicked() {
        let row = table.clickedRow
        guard row >= 0 else { return }
        for i in 0..<host.WebLayerCount() { host.SetWebLayerChosen(i, i == Int32(row)) }
        addLayers()
    }

    @objc private func chosenToggled(_ sender: NSButton) {
        host.SetWebLayerChosen(Int32(sender.tag), sender.state == .on)
        updateSelection()
    }

    @objc private func closeWindow() { close() }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.WebLayerCount()) }

    func tableViewSelectionDidChange(_ notification: Notification) { updateSelection() }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard let id = tableColumn?.identifier.rawValue else { return nil }
        let i = Int32(row)
        if id == "add" {
            let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(chosenToggled(_:)))
            box.tag = row
            box.state = host.WebLayerChosen(i) ? .on : .off
            return box
        }
        let text: String
        switch id {
        case "title": text = String(host.WebLayerTitle(i))
        case "geometry": text = String(host.WebLayerGeometry(i))
        default: text = String(host.WebLayerId(i))
        }
        let label = NSTextField(labelWithString: text)
        label.lineBreakMode = .byTruncatingTail
        return label
    }

    func windowWillClose(_ notification: Notification) {
        host.WebCancel()
        onClose?()
    }
}
