// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import UniformTypeIdentifiers

/// The application: one map window and a fixed menu bar.
///
/// Command line: `--catalog <path>` opens a catalog at launch (otherwise the
/// last one opened is reopened at the last view); `--center lat,lon[,scale]`
/// moves the camera there; `--shot <png>` writes the window once the first
/// frame is drawn, then quits.
@main @MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var main: MapWindowController!

    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        app.run()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.mainMenu = makeMenu()
        main = MapWindowController()
        main.window?.center()
        main.showWindow(nil)
        NSApp.activate()

        let args = Arguments(CommandLine.arguments)
        let last = UserDefaults.standard.string(forKey: "LastCatalog")
        if let path = args.catalog ?? last, FileManager.default.fileExists(atPath: path),
           main.openCatalog(URL(fileURLWithPath: path)) {
            if let c = args.center {
                main.goTo(lat: c.lat, lon: c.lon, scale: c.scale)
            } else if path == last {
                main.restoreView()
            }
        }
        if let shot = args.shot {
            DispatchQueue.main.async { [main] in
                do {
                    try main!.writeSnapshot(to: URL(fileURLWithPath: shot))
                } catch {
                    FileHandle.standardError.write(Data("--shot: \(error)\n".utf8))
                }
                NSApp.terminate(nil)
            }
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        if Arguments(CommandLine.arguments).shot == nil { main?.saveView() }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        main?.openCatalog(URL(fileURLWithPath: filename)) ?? false
    }

    // MARK: Actions

    @objc func openCatalog(_ sender: Any?) {
        let panel = NSOpenPanel()
        panel.title = "Open Map Catalog"
        panel.message = "Choose a Peregrine map catalog database."
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = ["sqlite", "db"].compactMap { UTType(filenameExtension: $0) }
        guard let window = main.window else { return }
        panel.beginSheetModal(for: window) { [main] response in
            if response == .OK, let url = panel.url { main?.openCatalog(url) }
        }
    }

    @objc func zoomIn(_ sender: Any?) { main.execute("map.zoom_in") }
    @objc func zoomOut(_ sender: Any?) { main.execute("map.zoom_out") }
    @objc func recenter(_ sender: Any?) { main.execute("map.recenter") }

    // MARK: Menu bar

    /// The application, File, Map and Window menus, built by hand rather than
    /// from DeskKit's menu model.
    private func makeMenu() -> NSMenu {
        let bar = NSMenu()

        let app = NSMenu(title: "Peregrine")
        app.addItem(withTitle: "About Peregrine",
                    action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
                    keyEquivalent: "")
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
        app.addItem(withTitle: "Quit Peregrine", action: #selector(NSApplication.terminate(_:)),
                    keyEquivalent: "q")
        add(app, to: bar)

        let file = NSMenu(title: "File")
        let open = file.addItem(withTitle: "Open Map Catalog…",
                                action: #selector(openCatalog(_:)), keyEquivalent: "o")
        open.keyEquivalentModifierMask = [.command, .option]
        file.addItem(.separator())
        file.addItem(withTitle: "Close Window", action: #selector(NSWindow.performClose(_:)),
                     keyEquivalent: "w")
        add(file, to: bar)

        let map = NSMenu(title: "Map")
        map.addItem(withTitle: "Zoom In", action: #selector(zoomIn(_:)),
                    keyEquivalent: String(UnicodeScalar(NSPageUpFunctionKey)!))
            .keyEquivalentModifierMask = []
        map.addItem(withTitle: "Zoom Out", action: #selector(zoomOut(_:)),
                    keyEquivalent: String(UnicodeScalar(NSPageDownFunctionKey)!))
            .keyEquivalentModifierMask = []
        map.addItem(withTitle: "Recenter on Data", action: #selector(recenter(_:)),
                    keyEquivalent: "")
        add(map, to: bar)

        let window = NSMenu(title: "Window")
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)),
                       keyEquivalent: "m")
        window.addItem(withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)),
                       keyEquivalent: "")
        add(window, to: bar)
        NSApp.windowsMenu = window
        return bar
    }

    private func add(_ menu: NSMenu, to bar: NSMenu) {
        let item = NSMenuItem(title: menu.title, action: nil, keyEquivalent: "")
        item.submenu = menu
        bar.addItem(item)
    }
}

/// The launch options this app reads; anything else is ignored.
struct Arguments {
    var catalog: String?
    var shot: String?
    var center: (lat: Double, lon: Double, scale: Double?)?

    init(_ argv: [String]) {
        var it = argv.dropFirst().makeIterator()
        while let a = it.next() {
            switch a {
            case "--catalog": catalog = it.next()
            case "--shot": shot = it.next()
            case "--center":
                let v = (it.next() ?? "").split(separator: ",").compactMap { Double($0) }
                if v.count >= 2 { center = (v[0], v[1], v.count > 2 ? v[2] : nil) }
            default: break
            }
        }
    }
}
