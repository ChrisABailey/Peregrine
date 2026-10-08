// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The window toolbar, generated from DeskKit's toolbar model: one icon button
/// per command, with the command's label as its tooltip.
@MainActor
final class CommandToolbar: NSObject, NSToolbarDelegate {
    private let host: fv.desk.DeskHost
    private weak var target: AnyObject?
    private var entries: [(id: String, label: String, icon: String, checkable: Bool)] = []
    private var generation = 0

    init(host: fv.desk.DeskHost, target: AnyObject) {
        self.host = host
        self.target = target
    }

    /// Re-reads the toolbar model and replaces `window`'s toolbar when its
    /// commands changed.
    func update(_ window: NSWindow) {
        var next: [(id: String, label: String, icon: String, checkable: Bool)] = []
        for i in 0..<host.ToolbarEntryCount() {
            let e = host.ToolbarEntryAt(i)
            if e.kind == fv.desk.kMenuSeparator { next.append(("", "", "", false)) }
            else { next.append((String(e.id), String(e.label), String(e.icon), e.checkable)) }
        }
        if window.toolbar != nil, next.map(\.id) == entries.map(\.id),
           next.map(\.label) == entries.map(\.label) { return }
        entries = next
        generation += 1
        let toolbar = NSToolbar(identifier: "Peregrine.\(generation)")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        toolbar.autosavesConfiguration = false
        window.toolbar = toolbar
    }

    private func identifiers() -> [NSToolbarItem.Identifier] {
        entries.enumerated().map { i, e in
            e.id.isEmpty ? NSToolbarItem.Identifier("separator.\(i)")
                         : NSToolbarItem.Identifier(e.id)
        }
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        identifiers()
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        identifiers()
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier id: NSToolbarItem.Identifier,
                 willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        guard let e = entries.first(where: { $0.id == id.rawValue }), !e.id.isEmpty else {
            // A gap between groups.
            let gap = NSToolbarItem(itemIdentifier: id)
            gap.view = NSView(frame: NSRect(x: 0, y: 0, width: 12, height: 1))
            return gap
        }
        return CommandToolbarItem(host: host, command: e.id, label: e.label, icon: e.icon,
                                  checkable: e.checkable, target: target)
    }
}

/// A toolbar button that executes one command; a checkable command's button
/// shows its checked state.
@MainActor
final class CommandToolbarItem: NSToolbarItem {
    private let host: fv.desk.DeskHost
    private let command: String
    private let button: NSButton

    init(host: fv.desk.DeskHost, command: String, label: String, icon: String,
         checkable: Bool, target: AnyObject?) {
        self.host = host
        self.command = command
        let image = Self.image(for: icon, label: label)
        button = image.map { NSButton(image: $0, target: target, action: Selector(("runCommand:"))) }
            ?? NSButton(title: label, target: target, action: Selector(("runCommand:")))
        super.init(itemIdentifier: NSToolbarItem.Identifier(command))
        button.bezelStyle = .texturedRounded
        button.setButtonType(checkable ? .pushOnPushOff : .momentaryPushIn)
        button.identifier = NSUserInterfaceItemIdentifier(command)
        self.label = label
        toolTip = label
        view = button
    }

    override func validate() {
        let enabled = host.IsEnabled(std.string(command))
        isEnabled = enabled
        button.isEnabled = enabled
        button.state = host.IsChecked(std.string(command)) ? .on : .off
    }

    /// The SF Symbol for a DeskKit icon name; nil when there is none, so the
    /// button shows its label instead.
    static func image(for icon: String, label: String) -> NSImage? {
        let symbols = [
            "zoom_in": "plus.magnifyingglass",
            "zoom_out": "minus.magnifyingglass",
            "recenter": "scope",
            "grid": "grid",
            "points": "mappin.and.ellipse",
            "contour": "mountain.2",
            "tamask": "exclamationmark.triangle",
            "scalebar": "ruler",
            "movingmap": "location.north.line",
            "route": "point.topleft.down.to.point.bottomright.curvepath",
            "pencil": "pencil",
        ]
        guard let name = symbols[icon] else { return nil }
        return NSImage(systemSymbolName: name, accessibilityDescription: label)
    }
}
