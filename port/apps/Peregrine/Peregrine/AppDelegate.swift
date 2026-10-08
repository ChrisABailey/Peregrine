// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The application: one map window and the menu bar DeskKit's model generates.
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
        main = MapWindowController()
        NSApp.mainMenu = MainMenu.make(host: main.host, target: main)
        main.onMenusChanged = { [weak self] in
            guard let self else { return }
            NSApp.mainMenu = MainMenu.make(host: self.main.host, target: self.main)
        }
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

    /// Quitting from anywhere (the Dock, logout) runs DeskKit's quit, which
    /// asks about each unsaved overlay; a cancel keeps the app running.
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let main, Arguments(CommandLine.arguments).shot == nil else { return .terminateNow }
        if !main.host.QuitRequested() { _ = main.host.Execute(std.string("app.quit")) }
        return main.host.QuitRequested() ? .terminateNow : .terminateCancel
    }

    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        main?.openCatalog(URL(fileURLWithPath: filename)) ?? false
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
