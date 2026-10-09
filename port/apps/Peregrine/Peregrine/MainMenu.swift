// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// Builds the menu bar: the application and Edit menus AppKit expects, the
/// File, Map, Overlay and editor menus generated from DeskKit's menu model,
/// then Window and Help.
///
/// Every generated item sends `runCommand(_:)` to `target` with the command
/// id as its represented object; `target` validates it against the host.
@MainActor
enum MainMenu {
    /// Commands the macOS application menu hosts instead of the menu the
    /// shared layout puts them in.
    static let appMenuCommands: Set<String> = ["app.quit"]

    static func make(host: fv.desk.DeskHost, target: AnyObject) -> NSMenu {
        let bar = NSMenu()
        add(applicationMenu(target: target), to: bar)

        var stack: [NSMenu] = []
        var editInserted = false
        for i in 0..<host.MenuEntryCount() {
            let e = host.MenuEntryAt(i)
            let depth = Int(e.depth)
            let label = String(e.label)
            if e.kind == fv.desk.kMenuTop {
                stack.forEach(tidySeparators)
                if !editInserted, String(e.id) != "file" {
                    add(editMenu(), to: bar)
                    editInserted = true
                }
                let menu = NSMenu(title: label)
                add(menu, to: bar)
                stack = [menu]
                continue
            }
            // Leaving a submenu: its entries end where the depth drops.
            while stack.count > depth { stack.removeLast() }
            guard let parent = stack.last else { continue }
            switch e.kind {
            case fv.desk.kMenuSubmenu:
                let sub = NSMenu(title: label)
                let item = NSMenuItem(title: label, action: nil, keyEquivalent: "")
                item.submenu = sub
                parent.addItem(item)
                stack.append(sub)
            case fv.desk.kMenuSeparator:
                parent.addItem(.separator())
            default:
                let id = String(e.id)
                if appMenuCommands.contains(id) { continue }
                parent.addItem(commandItem(id: id, label: label, entry: e, target: target))
            }
        }
        stack.forEach(tidySeparators)
        if !editInserted { add(editMenu(), to: bar) }
        bar.items.compactMap(\.submenu).forEach(tidySeparators)

        let window = NSMenu(title: "Window")
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)),
                       keyEquivalent: "m")
        window.addItem(withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)),
                       keyEquivalent: "")
        add(window, to: bar)
        NSApp.windowsMenu = window

        let help = NSMenu(title: "Help")
        let showLog = help.addItem(withTitle: "Show Log", action: Selector(("showLog:")),
                                   keyEquivalent: "")
        showLog.target = target
        add(help, to: bar)
        NSApp.helpMenu = help
        return bar
    }

    /// The menu item for one command, with its shortcut mapped to AppKit's.
    static func commandItem(id: String, label: String, entry e: fv.desk.HostMenuEntry,
                            target: AnyObject) -> NSMenuItem {
        let item = NSMenuItem(title: label, action: Selector(("runCommand:")), keyEquivalent: "")
        item.target = target
        item.representedObject = id
        if let key = keyEquivalent(String(e.key)) {
            item.keyEquivalent = key
            item.keyEquivalentModifierMask = modifierMask(e.modifiers)
        }
        return item
    }

    /// AppKit's key equivalent for a DeskKit key spelling; nil for none.
    /// Letters are lower-cased: an upper-case equivalent implies Shift.
    static func keyEquivalent(_ key: String) -> String? {
        if key.isEmpty { return nil }
        if key.count == 1 { return key.lowercased() }
        func fn(_ code: Int) -> String { String(UnicodeScalar(UInt32(code))!) }
        switch key {
        case "PageUp": return fn(NSPageUpFunctionKey)
        case "PageDown": return fn(NSPageDownFunctionKey)
        case "Home": return fn(NSHomeFunctionKey)
        case "End": return fn(NSEndFunctionKey)
        case "Left": return fn(NSLeftArrowFunctionKey)
        case "Right": return fn(NSRightArrowFunctionKey)
        case "Up": return fn(NSUpArrowFunctionKey)
        case "Down": return fn(NSDownArrowFunctionKey)
        case "Delete": return "\u{8}"
        case "Escape": return "\u{1b}"
        case "Return": return "\r"
        case "Tab": return "\t"
        case "Space": return " "
        default:
            if key.hasPrefix("F"), let n = Int(key.dropFirst()), (1...35).contains(n) {
                return fn(NSF1FunctionKey + n - 1)
            }
            return nil
        }
    }

    static func modifierMask(_ bits: UInt32) -> NSEvent.ModifierFlags {
        var mask: NSEvent.ModifierFlags = []
        if bits & fv.desk.kHostPrimary.rawValue != 0 { mask.insert(.command) }
        if bits & fv.desk.kHostShift.rawValue != 0 { mask.insert(.shift) }
        if bits & fv.desk.kHostAlt.rawValue != 0 { mask.insert(.option) }
        if bits & fv.desk.kHostControl.rawValue != 0 { mask.insert(.control) }
        return mask
    }

    // MARK: Fixed menus

    private static func applicationMenu(target: AnyObject) -> NSMenu {
        let app = NSMenu(title: "Peregrine")
        app.addItem(withTitle: "About Peregrine",
                    action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
                    keyEquivalent: "")
        app.addItem(.separator())
        let services = NSMenuItem(title: "Services", action: nil, keyEquivalent: "")
        services.submenu = NSMenu(title: "Services")
        NSApp.servicesMenu = services.submenu
        app.addItem(services)
        app.addItem(.separator())
        app.addItem(withTitle: "Hide Peregrine", action: #selector(NSApplication.hide(_:)),
                    keyEquivalent: "h")
        let others = app.addItem(withTitle: "Hide Others",
                                 action: #selector(NSApplication.hideOtherApplications(_:)),
                                 keyEquivalent: "h")
        others.keyEquivalentModifierMask = [.command, .option]
        app.addItem(withTitle: "Show All",
                    action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
        app.addItem(.separator())
        let quit = NSMenuItem(title: "Quit Peregrine", action: Selector(("runCommand:")),
                              keyEquivalent: "q")
        quit.target = target
        quit.representedObject = "app.quit"
        app.addItem(quit)
        return app
    }

    /// The standard Edit menu; its actions go to the first responder (text
    /// fields in panels and sheets).
    private static func editMenu() -> NSMenu {
        let edit = NSMenu(title: "Edit")
        edit.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
        let redo = edit.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "z")
        redo.keyEquivalentModifierMask = [.command, .shift]
        edit.addItem(.separator())
        edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)),
                     keyEquivalent: "a")
        return edit
    }

    private static func add(_ menu: NSMenu, to bar: NSMenu) {
        let item = NSMenuItem(title: menu.title, action: nil, keyEquivalent: "")
        item.submenu = menu
        bar.addItem(item)
    }

    /// Drops leading, trailing and repeated separators left by relocated items.
    private static func tidySeparators(_ menu: NSMenu) {
        var previousWasSeparator = true
        for item in menu.items {
            if item.isSeparatorItem && previousWasSeparator { menu.removeItem(item) }
            else { previousWasSeparator = item.isSeparatorItem }
        }
        if let last = menu.items.last, last.isSeparatorItem { menu.removeItem(last) }
    }
}
