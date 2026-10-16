// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// Select by Attributes over `DeskHost`'s selection query: the query builder,
/// how the matches combine with the current selection, and Apply, which
/// selects and keeps the window open. Closing the window closes the model.
@MainActor
final class SelectByAttributesWindowController: NSWindowController, NSWindowDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let methodPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let builder: QueryBuilderPanel
    private let messageLabel = NSTextField(wrappingLabelWithString: "")

    init(host: fv.desk.DeskHost) {
        self.host = host
        builder = QueryBuilderPanel(host: host, which: QueryBuilderPanel.select)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 620, height: 460),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: true)
        window.minSize = NSSize(width: 480, height: 360)
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    private func buildLayout(in window: NSWindow) {
        for k in 0..<host.SelectionMethodCount() { methodPopup.addItem(withTitle: String(host.SelectionMethodTitle(k))) }
        let methodRow = NSStackView(views: [NSTextField(labelWithString: "Method"), methodPopup])
        methodRow.orientation = .horizontal
        builder.onReport = { [weak self] in self?.show(message: $0) }
        builder.view.translatesAutoresizingMaskIntoConstraints = false
        messageLabel.isHidden = true

        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        let apply = NSButton(title: "Apply", target: self, action: #selector(applySelection))
        apply.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [spacer, close, apply])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [methodRow, builder.view, messageLabel, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
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
            builder.view.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            messageLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])
        builder.view.setContentHuggingPriority(.defaultLow, for: .vertical)
    }

    /// Re-reads the query, as when the window is asked for again on another layer.
    func reload() {
        window?.title = String(host.SelectByAttributesTitle())
        builder.reload()
        show(message: "")
    }

    /// False when the layer the window was open on has closed.
    func refresh() -> Bool { host.QueryOpen(QueryBuilderPanel.select) }

    private func show(message: String, isError: Bool = true) {
        messageLabel.stringValue = message
        messageLabel.textColor = isError ? .systemRed : .secondaryLabelColor
        messageLabel.isHidden = message.isEmpty
    }

    @objc private func applySelection() {
        builder.commitEditing()
        let error = String(host.ApplySelectByAttributes(Int32(methodPopup.indexOfSelectedItem)))
        if !error.isEmpty {
            show(message: error)
            return
        }
        let n = host.SelectByAttributesSelectedCount()
        show(message: n == 1 ? "1 feature selected" : "\(n) features selected", isError: false)
    }

    @objc private func closeWindow() { close() }

    func windowWillClose(_ notification: Notification) {
        host.CloseSelectByAttributes()
        onClose?()
    }
}
