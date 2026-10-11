// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore
import UniformTypeIdentifiers

/// The New Feature Layer dialog over `DeskHost`'s new-layer model: template,
/// geometry, name, format and file. Create writes the empty layer and opens
/// it; the fields are changed afterwards in Layer Fields.
@MainActor
final class NewLayerWindowController: NSWindowController, NSWindowDelegate, NSTextFieldDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let templatePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let geometryPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let nameField = NSTextField(string: "")
    private let formatPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let pathLabel = NSTextField(labelWithString: "")
    private let fieldsLabel = NSTextField(wrappingLabelWithString: "")
    private let messageLabel = NSTextField(wrappingLabelWithString: "")

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 460, height: 360),
                              styleMask: [.titled, .closable], backing: .buffered, defer: true)
        window.title = "New Feature Layer"
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    func reload() {
        templatePopup.selectItem(at: Int(host.NewLayerTemplate()))
        geometryPopup.selectItem(at: Int(host.NewLayerGeometry()))
        nameField.stringValue = String(host.NewLayerName())
        formatPopup.selectItem(at: Int(host.NewLayerFormat()))
        let path = String(host.NewLayerPath())
        pathLabel.stringValue = path.isEmpty ? "Not chosen" : path
        pathLabel.textColor = path.isEmpty ? .secondaryLabelColor : .labelColor
        let fields = String(host.NewLayerFieldSummary())
        fieldsLabel.stringValue = fields.isEmpty ? "No fields" : fields
    }

    private func buildLayout(in window: NSWindow) {
        for i in 0..<host.NewLayerTemplateCount() { templatePopup.addItem(withTitle: String(host.NewLayerTemplateTitle(i))) }
        for k in 0..<host.NewLayerGeometryCount() { geometryPopup.addItem(withTitle: String(host.NewLayerGeometryTitle(k))) }
        formatPopup.addItems(withTitles: ["GeoPackage", "GeoJSON"])
        templatePopup.target = self
        templatePopup.action = #selector(templateChanged)
        geometryPopup.target = self
        geometryPopup.action = #selector(geometryChanged)
        formatPopup.target = self
        formatPopup.action = #selector(formatChanged)
        nameField.delegate = self
        pathLabel.lineBreakMode = .byTruncatingMiddle
        let choose = NSButton(title: "Choose\u{2026}", target: self, action: #selector(choosePath))
        let fileRow = NSStackView(views: [pathLabel, choose])
        fileRow.orientation = .horizontal
        fieldsLabel.textColor = .secondaryLabelColor

        let grid = NSGridView(views: [
            [NSTextField(labelWithString: "Start from"), templatePopup],
            [NSTextField(labelWithString: "Geometry"), geometryPopup],
            [NSTextField(labelWithString: "Name"), nameField],
            [NSTextField(labelWithString: "Format"), formatPopup],
            [NSTextField(labelWithString: "File"), fileRow],
            [NSTextField(labelWithString: "Fields"), fieldsLabel],
        ])
        grid.rowSpacing = 8
        grid.columnSpacing = 8
        grid.rowAlignment = .firstBaseline
        grid.column(at: 0).xPlacement = .trailing
        grid.column(at: 1).width = 300

        messageLabel.textColor = .systemRed
        messageLabel.isHidden = true
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelLayer))
        cancel.keyEquivalent = "\u{1b}"
        let create = NSButton(title: "Create", target: self, action: #selector(createLayer))
        create.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [spacer, cancel, create])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [grid, messageLabel, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 12, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(stack)
        window.contentView = content
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            messageLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])
    }

    /// Shows `error` (or clears the message) and re-reads the model.
    private func report(_ error: std.string) {
        let text = String(error)
        messageLabel.stringValue = text
        messageLabel.isHidden = text.isEmpty
        reload()
    }

    @objc private func templateChanged() { report(host.SetNewLayerTemplate(Int32(templatePopup.indexOfSelectedItem))) }

    @objc private func geometryChanged() { report(host.SetNewLayerGeometry(Int32(geometryPopup.indexOfSelectedItem))) }

    @objc private func formatChanged() { report(host.SetNewLayerFormat(Int32(formatPopup.indexOfSelectedItem))) }

    func controlTextDidEndEditing(_ obj: Notification) {
        host.SetNewLayerName(std.string(nameField.stringValue))
        report(std.string(""))
    }

    @objc private func choosePath() {
        guard let window else { return }
        let panel = NSSavePanel()
        let geojson = host.NewLayerFormat() == 1
        let ext = geojson ? "geojson" : "gpkg"
        if let type = UTType(filenameExtension: ext) { panel.allowedContentTypes = [type] }
        let current = String(host.NewLayerPath())
        if current.isEmpty {
            panel.nameFieldStringValue = "\(String(host.NewLayerName())).\(ext)"
        } else {
            let url = URL(fileURLWithPath: current)
            panel.directoryURL = url.deletingLastPathComponent()
            panel.nameFieldStringValue = url.lastPathComponent
        }
        panel.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .OK, let url = panel.url else { return }
            self.host.SetNewLayerPath(std.string(url.path))
            self.report(std.string(""))
        }
    }

    @objc private func createLayer() {
        window?.makeFirstResponder(nil)  // ends an edit in progress
        host.SetNewLayerName(std.string(nameField.stringValue))
        let error = String(host.CreateNewLayer())
        if error.isEmpty {
            close()
        } else {
            report(std.string(error))
        }
    }

    @objc private func cancelLayer() { close() }

    func windowWillClose(_ notification: Notification) {
        host.CloseNewLayer()
        onClose?()
    }
}
