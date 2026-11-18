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

    /// Package art already read, by path; nil records a file that would not load.
    private static var fileImages: [String: NSImage?] = [:]

    /// The art for a DeskKit icon: a package's .svg/.png/.pdf when `icon` is
    /// an absolute path, otherwise the SF Symbol for the name. nil when there
    /// is none, so the button shows its label instead.
    static func image(for icon: String, label: String) -> NSImage? {
        if icon.hasPrefix("/") { return fileImage(at: icon, label: label) }
        return symbolImage(for: icon, label: label)
    }

    /// Loads package art once, sized to a toolbar symbol.
    private static func fileImage(at path: String, label: String) -> NSImage? {
        if let cached = fileImages[path] { return cached }
        let ext = (path as NSString).pathExtension.lowercased()
        var image: NSImage?
        if ["svg", "png", "pdf"].contains(ext), let loaded = NSImage(contentsOfFile: path) {
            let aspect = loaded.size.height > 0 ? loaded.size.width / loaded.size.height : 1
            loaded.size = NSSize(width: 18 * aspect, height: 18)
            loaded.accessibilityDescription = label
            // Vector art is drawn as a template so it follows the toolbar's
            // light and dark appearance, as the SF Symbols do; PNG keeps its colours.
            loaded.isTemplate = ext != "png"
            image = loaded
        }
        fileImages[path] = image
        return image
    }

    private static func symbolImage(for icon: String, label: String) -> NSImage? {
        let symbols = [
            "zoom_in": "plus.magnifyingglass",
            "zoom_out": "minus.magnifyingglass",
            "recenter": "scope",
            "grid": "grid",
            "points": "mappin.and.ellipse",
            "features": "square.stack.3d.up",
            "contour": "mountain.2",
            "tamask": "exclamationmark.triangle",
            "scalebar": "ruler",
            "movingmap": "location.north.line",
            "course_up": "location.north.circle",
            "continuous": "scope",
            "snap_to_road": "road.lanes",
            "trail_dots": "point.3.connected.trianglepath.dotted",
            "listen": "antenna.radiowaves.left.and.right",
            "feed": "slider.horizontal.3",
            "ownship": "gauge.with.dots.needle.33percent",
            "route": "point.topleft.down.to.point.bottomright.curvepath",
            "pencil": "pencil",
            "add_point": "plus.circle",
            "edit_point": "info.circle",
            "delete_point": "minus.circle",
            "leg_type": "arrow.triangle.swap",
            "follow_roads": "car",
            "follow_roads_bicycle": "bicycle",
            "avoid_hills": "mountain.2",
            "straight_legs": "line.diagonal",
            "labels": "textformat",
            "select": "cursorarrow",
            "draw_line": "scribble",
            "draw_area": "pentagon",
            "add_hole": "circle.dashed",
            "finish": "checkmark.circle",
            "delete_vertex": "minus.diamond",
            "delete_hole": "circle.slash",
            "delete_feature": "trash",
            "undo": "arrow.uturn.backward",
            "redo": "arrow.uturn.forward",
            "elevation_profile": "chart.xyaxis.line",
            "attribute_table": "tablecells",
            "layer_style": "paintpalette",
            "style_feature": "paintbrush",
            "layer_fields": "list.bullet.rectangle",
            "export_selection": "square.and.arrow.up",
            "layers": "sidebar.right",
        ]
        guard let name = symbols[icon] else { return nil }
        return NSImage(systemSymbolName: name, accessibilityDescription: label)
    }
}
