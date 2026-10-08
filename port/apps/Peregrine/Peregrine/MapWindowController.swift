// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// One map window: the map widget over a status bar showing the scale, the
/// series drawn, the ladder's message and the position under the cursor, with
/// the toolbar, menus and dialogs DeskKit's model calls for.
final class MapWindowController: NSWindowController, NSMenuItemValidation {
    let host: fv.desk.DeskHost
    let canvas: MapCanvasView
    private let scaleLabel = MapWindowController.label()
    private let productLabel = MapWindowController.label()
    private let messageLabel = MapWindowController.label()
    private let positionLabel = MapWindowController.label(monospaced: true)
    private var toolbar: CommandToolbar!
    private var jobSheet: JobSheet?
    /// Open options windows, by HostOptionsKind.
    private var optionsWindows: [Int32: OptionsWindowController] = [:]
    /// Called after the menu model changed; the app rebuilds the menu bar.
    var onMenusChanged: (() -> Void)?

    init() {
        host = fv.desk.DeskHost.Create()
        let settingsError = String(host.LoadSettings(std.string("")))
        canvas = MapCanvasView(host: host)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1100, height: 750),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Peregrine"
        window.minSize = NSSize(width: 400, height: 300)
        window.tabbingMode = .disallowed
        super.init(window: window)

        let bar = StatusBarView(views: [scaleLabel, productLabel, messageLabel, NSView(), positionLabel])
        bar.orientation = .horizontal
        bar.spacing = 16
        bar.edgeInsets = NSEdgeInsets(top: 4, left: 10, bottom: 4, right: 10)
        let content = NSView()
        for v in [canvas, bar] as [NSView] {
            v.translatesAutoresizingMaskIntoConstraints = false
            content.addSubview(v)
        }
        NSLayoutConstraint.activate([
            canvas.topAnchor.constraint(equalTo: content.topAnchor),
            canvas.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            canvas.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            canvas.bottomAnchor.constraint(equalTo: bar.topAnchor),
            bar.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            bar.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            bar.bottomAnchor.constraint(equalTo: content.bottomAnchor),
        ])
        window.contentView = content
        window.initialFirstResponder = canvas
        canvas.onStatusChange = { [weak self] in self?.refreshStatus() }
        canvas.onError = { [weak self] in self?.present(error: "Peregrine", detail: $0) }
        canvas.onTick = { [weak self] in self?.handle($0) }
        window.toolbarStyle = .unified
        toolbar = CommandToolbar(host: host, target: self)
        toolbar.update(window)
        let context = Unmanaged.passUnretained(self).toOpaque()
        host.SetRequestHandler({ context in
            guard let context else { return }
            let me = Unmanaged<MapWindowController>.fromOpaque(context).takeUnretainedValue()
            MainActor.assumeIsolated { Dialogs.answer(me.host) }
        }, context)
        refreshStatus()
        if !settingsError.isEmpty {
            DispatchQueue.main.async { [weak self] in
                self?.present(error: "Settings could not be read", detail: settingsError)
            }
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    private static func label(monospaced: Bool = false) -> NSTextField {
        let l = NSTextField(labelWithString: "")
        l.font = monospaced ? .monospacedDigitSystemFont(ofSize: 12, weight: .regular)
                            : .systemFont(ofSize: 12)
        l.lineBreakMode = .byTruncatingTail
        l.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        return l
    }

    func refreshStatus() {
        scaleLabel.stringValue = String(host.StatusScale())
        productLabel.stringValue = String(host.StatusProduct())
        messageLabel.stringValue = String(host.StatusMessage())
        positionLabel.stringValue = String(host.StatusPosition())
    }

    // MARK: Catalog and commands

    /// Opens a catalog; on failure shows the error and keeps the current one.
    @discardableResult
    func openCatalog(_ url: URL) -> Bool {
        let error = String(host.OpenCatalog(std.string(url.path)))
        guard error.isEmpty else {
            present(error: "Could not open the map catalog", detail: error)
            return false
        }
        catalogDidChange()
        refreshStatus()
        return true
    }

    /// Titles the window after the open catalog and remembers it for the
    /// next launch and the Open Recent menu.
    private func catalogDidChange() {
        let path = String(host.CatalogPath())
        guard !path.isEmpty, path != ":memory:" else { return }
        let url = URL(fileURLWithPath: path)
        window?.title = "Peregrine \u{2014} \(url.deletingPathExtension().lastPathComponent)"
        window?.representedURL = url
        NSDocumentController.shared.noteNewRecentDocumentURL(url)
        UserDefaults.standard.set(path, forKey: "LastCatalog")
    }

    /// Reacts to what the host reports each display refresh: menus, the
    /// catalog, a background job and notices.
    private func handle(_ t: fv.desk.HostTick) {
        if t.menus_changed {
            if let window { toolbar.update(window) }
            onMenusChanged?()
        }
        if t.catalog_changed { catalogDidChange() }
        if t.job_changed { updateJob() }
        if t.options_requested {
            let kind = host.TakeOptionsRequest()
            if kind >= 0 { showOptions(kind) }
        }
        let notice = String(host.TakeNotice())
        if !notice.isEmpty { present(notice: notice) }
    }

    /// Brings up the options window of `kind` on the model the core just
    /// opened, reusing the window when it is already showing.
    private func showOptions(_ kind: Int32) {
        if let existing = optionsWindows[kind] {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = OptionsWindowController(host: host, kind: kind)
        controller.onClose = { [weak self] in self?.optionsWindows[kind] = nil }
        optionsWindows[kind] = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Shows, advances or dismisses the progress sheet of a catalog build.
    private func updateJob() {
        guard host.JobActive() else {
            jobSheet?.end()
            jobSheet = nil
            return
        }
        if jobSheet == nil, let window {
            let sheet = JobSheet(title: "Building the map catalog") { [weak self] in
                self?.host.CancelJob()
            }
            sheet.begin(on: window)
            jobSheet = sheet
        }
        jobSheet?.update(fraction: host.JobFraction(), text: String(host.JobText()))
    }

    /// Moves the camera; `scale` defaults to the current one.
    func goTo(lat: Double, lon: Double, scale: Double? = nil) {
        host.GoTo(lat, lon, scale ?? host.ScaleDenom())
    }

    /// Remembers the camera for the next launch with the same catalog.
    func saveView() {
        let d = UserDefaults.standard
        d.set([host.CenterLat(), host.CenterLon(), host.ScaleDenom()], forKey: "LastView")
    }

    /// Returns to the camera saved by `saveView()`; false when there is none.
    @discardableResult
    func restoreView() -> Bool {
        guard let v = UserDefaults.standard.array(forKey: "LastView") as? [Double], v.count == 3
        else { return false }
        goTo(lat: v[0], lon: v[1], scale: v[2])
        return true
    }

    func execute(_ command: String) {
        let error = String(host.Execute(std.string(command)))
        if !error.isEmpty { present(error: "Command failed", detail: error) }
        if host.QuitRequested() { NSApp.terminate(nil) }
    }

    /// The action of every generated menu item and toolbar button: the
    /// command id is the menu item's represented object or the button's
    /// identifier.
    @objc func runCommand(_ sender: Any?) {
        if let item = sender as? NSMenuItem, let id = item.representedObject as? String {
            execute(id)
        } else if let view = sender as? NSView, let id = view.identifier?.rawValue {
            execute(id)
        }
    }

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        guard let id = item.representedObject as? String else { return true }
        item.state = host.IsChecked(std.string(id)) ? .on : .off
        return host.IsEnabled(std.string(id))
    }

    func present(notice: String) {
        let alert = NSAlert()
        alert.alertStyle = .informational
        alert.messageText = notice
        if let window { alert.beginSheetModal(for: window) } else { alert.runModal() }
    }

    func present(error message: String, detail: String) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = message
        alert.informativeText = detail
        if let window { alert.beginSheetModal(for: window) } else { alert.runModal() }
    }

    /// Writes the window's content (map and status bar) as a PNG, rendered
    /// from its layer tree as the window server composites it.
    func writeSnapshot(to url: URL) throws {
        canvas.renderNow()
        refreshStatus()
        guard let view = window?.contentView else { throw CocoaError(.fileWriteUnknown) }
        view.displayIfNeeded()
        let scale = window?.backingScaleFactor ?? 1
        let size = view.bounds.size
        guard let layer = view.layer,
              let rep = NSBitmapImageRep(bitmapDataPlanes: nil,
                                         pixelsWide: Int(size.width * scale),
                                         pixelsHigh: Int(size.height * scale),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                         isPlanar: false, colorSpaceName: .deviceRGB,
                                         bytesPerRow: 0, bitsPerPixel: 0),
              let ctx = NSGraphicsContext(bitmapImageRep: rep) else {
            throw CocoaError(.fileWriteUnknown)
        }
        ctx.cgContext.scaleBy(x: scale, y: scale)
        layer.render(in: ctx.cgContext)
        guard let png = rep.representation(using: .png, properties: [:]) else {
            throw CocoaError(.fileWriteUnknown)
        }
        try png.write(to: url)
        let status = [host.StatusScale(), host.StatusProduct(), host.StatusMessage()].map { String($0) }
        FileHandle.standardError.write(Data("\(url.lastPathComponent): \(status.joined(separator: " | "))\n".utf8))
    }
}

/// The status bar row; paints the window background so a snapshot of the
/// content view shows it as the window does.
final class StatusBarView: NSStackView {
    // Fills its bounds, not dirtyRect: views do not clip to bounds by default
    // (macOS 14 SDK), and dirtyRect can reach over the map.
    override func draw(_ dirtyRect: NSRect) {
        NSColor.windowBackgroundColor.setFill()
        bounds.fill()
        NSColor.separatorColor.setFill()
        NSRect(x: bounds.minX, y: bounds.maxY - 1, width: bounds.width, height: 1).fill()
    }
}
