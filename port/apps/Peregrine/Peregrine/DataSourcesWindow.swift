// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The Map Data Sources window: the directories Generate Coverage scans, with
/// Add…, Remove and Generate Coverage. The list lives in the catalog; the
/// window re-reads it from `DeskHost` after every change.
@MainActor
final class DataSourcesWindowController: NSWindowController, NSWindowDelegate,
    NSTableViewDataSource, NSTableViewDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?
    /// Shows an error from the core; the window passes its own as the sheet parent.
    var presentError: ((String, String, NSWindow?) -> Void)?

    private let table = NSTableView()
    private let removeButton = NSButton(title: "Remove", target: nil, action: nil)
    private let generateButton = NSButton(title: "Generate Coverage", target: nil, action: nil)

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 600, height: 340),
                              styleMask: [.titled, .closable, .resizable],
                              backing: .buffered, defer: true)
        window.title = "Map Data Sources"
        window.minSize = NSSize(width: 440, height: 240)
        window.tabbingMode = .disallowed
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the list and the buttons' state.
    func reload() {
        table.reloadData()
        updateButtons()
    }

    // MARK: Layout

    private func buildLayout(in window: NSWindow) {
        let intro = NSTextField(wrappingLabelWithString:
            "Folders of map data. Each is searched for every format it holds "
            + "(rpf, tiros3, dted, geotiff, enc, OSM and VPF databases). "
            + "Generate Coverage removes the catalog's coverage and rescans every folder.")
        intro.textColor = .secondaryLabelColor

        let column = NSTableColumn(identifier: .init("path"))
        column.resizingMask = .autoresizingMask
        table.addTableColumn(column)
        table.headerView = nil
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder

        let add = NSButton(title: "Add…", target: self, action: #selector(addFolder))
        removeButton.target = self
        removeButton.action = #selector(removeFolder)
        generateButton.target = self
        generateButton.action = #selector(generate)
        generateButton.keyEquivalent = "\r"
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [add, removeButton, spacer, close, generateButton])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [intro, scroll, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 20, bottom: 16, right: 20)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(stack)
        window.contentView = content
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            intro.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40),
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40),
        ])
    }

    private func updateButtons() {
        let idle = !host.JobActive()
        removeButton.isEnabled = idle && table.selectedRow >= 0
        generateButton.isEnabled = idle
    }

    // MARK: Actions

    @objc private func addFolder() {
        guard let window else { return }
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
        panel.prompt = "Add"
        panel.message = "Choose folders of map data"
        panel.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .OK else { return }
            for url in panel.urls {
                let error = String(self.host.AddScanRoot(std.string(url.path)))
                if !error.isEmpty {
                    self.presentError?("Could not add the folder", error, self.window)
                    break
                }
            }
            self.reload()
        }
    }

    @objc private func removeFolder() {
        let row = table.selectedRow
        guard row >= 0 else { return }
        let error = String(host.RemoveScanRoot(host.ScanRootAt(Int32(row))))
        if !error.isEmpty { presentError?("Could not remove the folder", error, window) }
        reload()
    }

    @objc private func generate() {
        let error = String(host.GenerateCoverage())
        if !error.isEmpty {
            presentError?("Could not generate coverage", error, window)
            return
        }
        // The map window's progress sheet takes over.
        close()
    }

    @objc private func closeWindow() { close() }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.ScanRootCount()) }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let path = String(host.ScanRootAt(Int32(row)))
        let label = NSTextField(labelWithString: path)
        label.lineBreakMode = .byTruncatingMiddle
        label.toolTip = path
        if !host.ScanRootReachable(Int32(row)) {
            label.stringValue = path + "  (not found)"
            label.textColor = .systemRed
        }
        return label
    }

    func tableViewSelectionDidChange(_ notification: Notification) { updateButtons() }

    func windowWillClose(_ notification: Notification) { onClose?() }
}
