// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore
import UniformTypeIdentifiers

/// Answers the core's questions (`DeskHost::PendingRequest`) with modal
/// panels and alerts. Runs inside the host call that asked, so every dialog
/// is application-modal rather than a sheet.
@MainActor
enum Dialogs {
    static func answer(_ host: fv.desk.DeskHost) {
        let r = host.PendingRequest()
        let title = String(r.title)
        switch r.kind {
        case fv.desk.kRequestAskSave:
            let alert = NSAlert()
            alert.messageText = "Save changes to \u{201C}\(title)\u{201D}?"
            alert.informativeText = "Your changes will be lost if you don\u{2019}t save them."
            alert.addButton(withTitle: "Save")
            alert.addButton(withTitle: "Cancel")
            alert.addButton(withTitle: "Don\u{2019}t Save")
            switch alert.runModal() {
            case .alertFirstButtonReturn: host.AnswerIndex(0)
            case .alertThirdButtonReturn: host.AnswerIndex(1)
            default: host.AnswerIndex(2)
            }
        case fv.desk.kRequestChooseOpen:
            let panel = NSOpenPanel()
            panel.allowsMultipleSelection = r.multiple
            panel.canChooseDirectories = false
            configure(panel, host: host, request: r)
            if panel.runModal() == .OK {
                for url in panel.urls { host.AnswerPath(std.string(url.path)) }
            }
        case fv.desk.kRequestChooseSave:
            let panel = NSSavePanel()
            panel.nameFieldStringValue = String(r.suggested_name)
            configure(panel, host: host, request: r)
            if panel.runModal() == .OK, let url = panel.url {
                host.AnswerPath(std.string(url.path))
                host.AnswerIndex(0)
            }
        case fv.desk.kRequestChooseFromList:
            let alert = NSAlert()
            alert.messageText = title
            let list = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 420, height: 26))
            for i in 0..<r.item_count { list.addItem(withTitle: String(host.RequestItemLabel(i))) }
            alert.accessoryView = list
            alert.addButton(withTitle: "OK")
            alert.addButton(withTitle: "Cancel")
            if alert.runModal() == .alertFirstButtonReturn {
                host.AnswerIndex(Int32(list.indexOfSelectedItem))
            }
        case fv.desk.kRequestConfirmRevert:
            let alert = NSAlert()
            alert.messageText = "Revert \u{201C}\(title)\u{201D} to the saved version?"
            alert.informativeText = "Changes since the last save will be lost."
            alert.addButton(withTitle: "Revert")
            alert.addButton(withTitle: "Cancel")
            host.AnswerIndex(alert.runModal() == .alertFirstButtonReturn ? 1 : 0)
        case fv.desk.kRequestChooseDirectory:
            let panel = NSOpenPanel()
            panel.message = title
            panel.prompt = "Choose"
            panel.canChooseFiles = false
            panel.canChooseDirectories = true
            panel.allowsMultipleSelection = false
            if panel.runModal() == .OK, let url = panel.url { host.AnswerPath(std.string(url.path)) }
        default:
            break
        }
    }

    /// Applies the request's start directory and file filters to a panel.
    private static func configure(_ panel: NSSavePanel, host: fv.desk.DeskHost,
                                  request r: fv.desk.HostRequest) {
        let dir = String(r.directory)
        if !dir.isEmpty { panel.directoryURL = URL(fileURLWithPath: dir, isDirectory: true) }
        var types: [UTType] = []
        var any = r.item_count == 0
        for i in 0..<r.item_count {
            for pattern in String(host.RequestItemPattern(i)).split(separator: ";") {
                let p = pattern.trimmingCharacters(in: .whitespaces)
                if p == "*" || p == "*.*" { any = true; continue }
                let ext = p.hasPrefix("*.") ? String(p.dropFirst(2)) : p
                if let t = UTType(filenameExtension: ext) { types.append(t) }
            }
        }
        if !any, !types.isEmpty { panel.allowedContentTypes = types }
    }
}

/// A sheet showing a background job's progress with a Cancel button.
@MainActor
final class JobSheet: NSObject {
    private let window: NSWindow
    private let label = NSTextField(labelWithString: "")
    private let bar = NSProgressIndicator()
    private let onCancel: () -> Void

    init(title: String, onCancel: @escaping () -> Void) {
        self.onCancel = onCancel
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 440, height: 120),
                          styleMask: [.titled], backing: .buffered, defer: true)
        super.init()
        let heading = NSTextField(labelWithString: title)
        heading.font = .boldSystemFont(ofSize: 13)
        label.lineBreakMode = .byTruncatingMiddle
        label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        bar.isIndeterminate = false
        bar.minValue = 0
        bar.maxValue = 1
        let cancel = NSButton(title: "Cancel", target: nil, action: nil)
        cancel.keyEquivalent = "\u{1b}"
        let stack = NSStackView(views: [heading, bar, label, cancel])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 20, bottom: 16, right: 20)
        stack.setCustomSpacing(12, after: label)
        bar.widthAnchor.constraint(equalToConstant: 400).isActive = true
        label.widthAnchor.constraint(equalToConstant: 400).isActive = true
        window.contentView = stack
        cancel.target = self
        cancel.action = #selector(cancelPressed(_:))
    }

    func begin(on parent: NSWindow) { parent.beginSheet(window) }

    func update(fraction: Double, text: String) {
        bar.doubleValue = fraction
        label.stringValue = text
    }

    func end() { window.sheetParent?.endSheet(window) }

    @objc private func cancelPressed(_ sender: Any?) {
        label.stringValue = "Cancelling after the current data source\u{2026}"
        onCancel()
    }
}
