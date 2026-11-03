// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore
import UniformTypeIdentifiers

/// Tools ▸ Overlay Manager: the installed overlay packages, their details,
/// and Install…, Install from Folder…, Enable/Disable, Remove and Show in
/// Finder. The model is DeskKit's PackageManager, read through `DeskHost`.
@MainActor
final class OverlayManagerWindowController: NSWindowController, NSWindowDelegate,
    NSTableViewDataSource, NSTableViewDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let table = NSTableView()
    private let detail = NSTextView()
    private let banner = NSTextField(labelWithString:
        "Restart Peregrine to finish the changes marked “at relaunch”.")
    private let enableButton = NSButton(title: "Disable", target: nil, action: nil)
    private let removeButton = NSButton(title: "Remove", target: nil, action: nil)
    private let revealButton = NSButton(title: "Show in Finder", target: nil, action: nil)
    private var rows: [fv.desk.HostPackage] = []

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 520),
                              styleMask: [.titled, .closable, .resizable],
                              backing: .buffered, defer: true)
        window.title = "Overlay Manager"
        window.minSize = NSSize(width: 560, height: 380)
        window.tabbingMode = .disallowed
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the packages, keeping the selected one selected.
    func reload() {
        let selected = selectedPackage.map { String($0.id) }
        rows = (0..<host.PackageCount()).map { host.PackageAt($0) }
        table.reloadData()
        if let selected, let row = rows.firstIndex(where: { String($0.id) == selected }) {
            table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        } else {
            table.deselectAll(nil)
        }
        banner.isHidden = !host.PackagesRestartPending()
        updateDetail()
    }

    /// Asks to install the .pgpkg or package folder at `path`, showing what it
    /// is and what code it runs, then installs it on confirmation.
    func confirmInstall(path: String, link: Bool = false) {
        let p = host.InspectPackage(std.string(path))
        if !String(p.error).isEmpty {
            alert("“\((path as NSString).lastPathComponent)” is not an overlay package", String(p.error))
            return
        }
        let confirm = NSAlert()
        confirm.messageText = "Install “\(String(p.name))” \(String(p.version))?"
        var lines: [String] = []
        let publisher = String(p.publisher)
        lines.append("Publisher: " + (publisher.isEmpty ? "unknown" : publisher))
        let kinds = String(p.code_kinds)
        if !kinds.isEmpty {
            lines.append("Runs code: \(kinds). It runs with your permissions; "
                + "install it only if you trust the publisher.")
        }
        let overlays = String(p.overlays), tools = String(p.tools)
        if !overlays.isEmpty { lines.append("Overlays: " + overlays.replacingOccurrences(of: "\n", with: ", ")) }
        if !tools.isEmpty { lines.append("Tools: " + tools.replacingOccurrences(of: "\n", with: ", ")) }
        if link { lines.append("The folder is used in place; removing the package leaves it untouched.") }
        confirm.informativeText = lines.joined(separator: "\n\n")
        confirm.addButton(withTitle: "Install")
        confirm.addButton(withTitle: "Cancel")
        let finish: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard let self, response == .alertFirstButtonReturn else { return }
            let o = link ? self.host.LinkPackage(std.string(path)) : self.host.InstallPackage(std.string(path))
            self.reload()
            if !String(o.error).isEmpty {
                self.alert("Could not install the package", String(o.error))
            } else if !o.live {
                self.alert("“\(String(p.name))” \(String(p.version)) is ready",
                           "It replaces version \(String(o.replaced_version)) when Peregrine restarts.")
            }
        }
        if let window, window.isVisible {
            confirm.beginSheetModal(for: window, completionHandler: finish)
        } else {
            finish(confirm.runModal())
        }
    }

    // MARK: Layout

    private func buildLayout(in window: NSWindow) {
        for (id, title, width) in [("name", "Name", 230.0), ("version", "Version", 80.0),
                                   ("location", "Location", 80.0), ("state", "State", 200.0)] {
            let column = NSTableColumn(identifier: .init(id))
            column.title = title
            column.width = width
            table.addTableColumn(column)
        }
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        let tableScroll = NSScrollView()
        tableScroll.documentView = table
        tableScroll.hasVerticalScroller = true
        tableScroll.borderType = .bezelBorder

        detail.isEditable = false
        detail.drawsBackground = false
        detail.textContainerInset = NSSize(width: 4, height: 6)
        let detailScroll = NSScrollView()
        detailScroll.documentView = detail
        detailScroll.hasVerticalScroller = true
        detailScroll.borderType = .bezelBorder
        detail.autoresizingMask = [.width]

        banner.textColor = .systemOrange

        let install = NSButton(title: "Install…", target: self, action: #selector(installFile))
        let installFolder = NSButton(title: "Install from Folder…", target: self, action: #selector(installFolder))
        enableButton.target = self
        enableButton.action = #selector(toggleEnabled)
        removeButton.target = self
        removeButton.action = #selector(remove)
        revealButton.target = self
        revealButton.action = #selector(reveal)
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [install, installFolder, enableButton, removeButton,
                                          revealButton, spacer, close])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [tableScroll, detailScroll, banner, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
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
            tableScroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40),
            detailScroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40),
            tableScroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 140),
            detailScroll.heightAnchor.constraint(equalTo: tableScroll.heightAnchor, multiplier: 0.8),
        ])
    }

    private var selectedPackage: fv.desk.HostPackage? {
        let row = table.selectedRow
        return row >= 0 && row < rows.count ? rows[row] : nil
    }

    /// The detail pane and the buttons for the selected package.
    private func updateDetail() {
        guard let p = selectedPackage else {
            detail.string = rows.isEmpty
                ? "No overlay packages are installed. Install… adds a .pgpkg file."
                : "Select a package to see its details."
            enableButton.isEnabled = false
            removeButton.isEnabled = false
            revealButton.isEnabled = false
            return
        }
        var text = "\(String(p.name)) \(String(p.version))\n\(String(p.id))\n\n"
        let description = String(p.description)
        if !description.isEmpty { text += description + "\n\n" }
        let fields: [(String, String)] = [
            ("Publisher", String(p.publisher)), ("License", String(p.license)),
            ("Runs code", String(p.code_kinds)), ("Location", String(p.dir)),
        ]
        for (label, value) in fields where !value.isEmpty { text += "\(label): \(value)\n" }
        let overlays = String(p.overlays), tools = String(p.tools)
        if !overlays.isEmpty { text += "\nOverlays:\n  " + overlays.replacingOccurrences(of: "\n", with: "\n  ") + "\n" }
        if !tools.isEmpty { text += "\nTools:\n  " + tools.replacingOccurrences(of: "\n", with: "\n  ") + "\n" }
        let reason = String(p.reason)
        if !reason.isEmpty { text += "\n\(String(p.state)): \(reason)\n" + signingHint(reason) }
        detail.string = text
        let system = String(p.location) == "System"
        enableButton.title = p.enabled ? "Disable" : "Enable"
        enableButton.isEnabled = String(p.state) != "Shadowed"
        removeButton.isEnabled = p.removable && !system
        revealButton.isEnabled = true
    }

    /// Gatekeeper's refusal of an unsigned or unnotarized library, explained.
    private func signingHint(_ reason: String) -> String {
        let r = reason.lowercased()
        guard r.contains("code signature") || r.contains("not valid for use in process")
            || r.contains("quarantine") || r.contains("notariz") else { return "" }
        return "\nmacOS refused the library. A downloaded library must be signed and "
            + "notarized by its author.\n"
    }

    private func alert(_ message: String, _ detail: String) {
        let a = NSAlert()
        a.alertStyle = .warning
        a.messageText = message
        a.informativeText = detail
        if let window, window.isVisible { a.beginSheetModal(for: window) } else { a.runModal() }
    }

    // MARK: Actions

    @objc private func installFile() {
        guard let window else { return }
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [UTType(filenameExtension: "pgpkg") ?? .zip]
        panel.prompt = "Install"
        panel.message = "Choose an overlay package (.pgpkg)"
        panel.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .OK, let url = panel.url else { return }
            DispatchQueue.main.async { self.confirmInstall(path: url.path) }
        }
    }

    @objc private func installFolder() {
        guard let window else { return }
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.prompt = "Install"
        panel.message = "Choose a package folder (it holds peregrine-package.json); it is used in place"
        panel.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .OK, let url = panel.url else { return }
            DispatchQueue.main.async { self.confirmInstall(path: url.path, link: true) }
        }
    }

    @objc private func toggleEnabled() {
        guard let p = selectedPackage else { return }
        let error = String(host.SetPackageEnabled(p.id, !p.enabled))
        if !error.isEmpty { alert("Could not change the package", error) }
        reload()
    }

    @objc private func remove() {
        guard let p = selectedPackage, let window else { return }
        let confirm = NSAlert()
        confirm.messageText = "Remove “\(String(p.name))”?"
        confirm.informativeText = String(p.location) == "Linked"
            ? "Peregrine stops using the folder; its files are left in place."
            : "Its files are deleted. A package in use is removed when Peregrine restarts."
        confirm.addButton(withTitle: "Remove")
        confirm.addButton(withTitle: "Cancel")
        confirm.beginSheetModal(for: window) { [weak self] response in
            guard let self, response == .alertFirstButtonReturn else { return }
            let error = String(self.host.RemovePackage(p.id))
            if !error.isEmpty { self.alert("Could not remove the package", error) }
            self.reload()
        }
    }

    @objc private func reveal() {
        guard let p = selectedPackage else { return }
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: String(p.dir))])
    }

    @objc private func closeWindow() { close() }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int { rows.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let p = rows[row]
        let text: String
        switch tableColumn?.identifier.rawValue {
        case "version": text = String(p.version)
        case "location": text = String(p.location)
        case "state":
            let pending = String(p.pending)
            text = pending.isEmpty ? String(p.state) : "\(String(p.state)) · \(pending)"
        default: text = String(p.name)
        }
        let label = NSTextField(labelWithString: text)
        label.lineBreakMode = .byTruncatingTail
        if tableColumn?.identifier.rawValue == "state" {
            switch String(p.state) {
            case "Failed", "Unavailable": label.textColor = .systemRed
            case "Disabled", "Shadowed": label.textColor = .secondaryLabelColor
            default: break
            }
        }
        return label
    }

    func tableViewSelectionDidChange(_ notification: Notification) { updateDetail() }

    func windowWillClose(_ notification: Notification) { onClose?() }
}
