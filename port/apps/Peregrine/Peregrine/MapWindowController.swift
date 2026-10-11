// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// One map window: the map widget over a status bar (feedback from the map and
/// overlays on the left, scale and series in the centre, position and terrain
/// height under the cursor on the right), with the toolbar, menus and dialogs
/// DeskKit's model calls for.
final class MapWindowController: NSWindowController, NSMenuItemValidation {
    let host: fv.desk.DeskHost
    let canvas: MapCanvasView
    /// The window title when no overlay file is open: the app and the catalog.
    private var catalogTitle = "Peregrine"
    private let scaleLabel = MapWindowController.label()
    private let productLabel = MapWindowController.label()
    private let feedbackLabel = MapWindowController.label()
    private let positionLabel = MapWindowController.label(monospaced: true)
    private let elevationLabel = MapWindowController.label(monospaced: true)
    private var toolbar: CommandToolbar!
    private var jobSheet: JobSheet?
    /// Open options windows, by HostOptionsKind.
    private var optionsWindows: [Int32: OptionsWindowController] = [:]
    private var dataSourcesWindow: DataSourcesWindowController?
    private var profileWindow: ProfileWindowController?
    private var layersWindow: LayersWindowController?
    private var featureFormWindow: FeatureFormWindowController?
    private var attributeTableWindow: AttributeTableWindowController?
    private var layerStyleWindow: LayerStyleWindowController?
    private var layerFieldsWindow: LayerFieldsWindowController?
    private var newLayerWindow: NewLayerWindowController?
    /// Called after the menu model changed; the app rebuilds the menu bar.
    var onMenusChanged: (() -> Void)?

    init() {
        host = fv.desk.DeskHost.Create()
        let settingsError = String(host.LoadSettings(std.string("")))
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
            as? String ?? "(unversioned)"
        let logError = String(host.StartLog(std.string(""), std.string(version)))
        if !logError.isEmpty {
            FileHandle.standardError.write(Data("Peregrine: no log file: \(logError)\n".utf8))
        }
        canvas = MapCanvasView(host: host)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1100, height: 750),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Peregrine"
        window.minSize = NSSize(width: 400, height: 300)
        window.tabbingMode = .disallowed
        super.init(window: window)

        // Three equal thirds keep the scale centred whatever the sides hold.
        let left = NSStackView(views: [feedbackLabel])
        let centre = NSStackView()
        centre.addView(scaleLabel, in: .center)
        centre.addView(productLabel, in: .center)
        let right = NSStackView()
        right.addView(positionLabel, in: .trailing)
        right.addView(elevationLabel, in: .trailing)
        for s in [left, centre, right] { s.spacing = 16 }
        let bar = StatusBarView(views: [left, centre, right])
        bar.orientation = .horizontal
        bar.distribution = .fillEqually
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

    /// Copies the host's status bar text and hover tool tip into the window.
    func refreshStatus() {
        scaleLabel.stringValue = String(host.StatusScale())
        productLabel.stringValue = String(host.StatusProduct())
        feedbackLabel.stringValue = String(host.StatusFeedback())
        positionLabel.stringValue = String(host.StatusPosition())
        elevationLabel.stringValue = String(host.StatusElevation())
        let tip = String(host.HoverToolTip())
        let next: String? = tip.isEmpty ? nil : tip
        if canvas.toolTip != next { canvas.toolTip = next }
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
        catalogTitle = "Peregrine \u{2014} \(url.deletingPathExtension().lastPathComponent)"
        updateTitle()
        window?.representedURL = url
        NSDocumentController.shared.noteNewRecentDocumentURL(url)
        UserDefaults.standard.set(path, forKey: "LastCatalog")
    }

    /// Titles the window after the current or topmost overlay file, or the
    /// catalog when no overlay file is open.
    private func updateTitle() {
        let document = String(host.DocumentName())
        let title = document.isEmpty ? catalogTitle : document
        if window?.title != title { window?.title = title }
    }

    /// Reacts to what the host reports each display refresh: menus, the
    /// catalog, a background job and notices.
    private func handle(_ t: fv.desk.HostTick) {
        updateTitle()
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
        if t.sources_requested && host.TakeSourcesRequest() { showDataSources() }
        if t.profile_requested && host.TakeRouteProfileRequest() { showRouteProfile() }
        if t.profile_changed { profileWindow?.reload() }
        if t.layers_requested && host.TakeLayersRequest() { showLayers() }
        if t.layers_changed { layersWindow?.reload() }
        if t.form_requested && host.TakeFeatureFormRequest() { showFeatureForm() }
        if t.table_requested && host.TakeAttributeTableRequest() { showAttributeTable() }
        if t.style_requested && host.TakeLayerStyleRequest() { showLayerStyle() }
        if t.fields_requested && host.TakeLayerFieldsRequest() { showLayerFields() }
        if t.new_layer_requested && host.TakeNewLayerRequest() { showNewLayer() }
        if t.features_changed { featuresDidChange() }
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

    /// Brings up the Map Data Sources window, reusing it when it is showing.
    private func showDataSources() {
        if let existing = dataSourcesWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = DataSourcesWindowController(host: host)
        controller.onClose = { [weak self] in self?.dataSourcesWindow = nil }
        controller.presentError = { message, detail, parent in
            let alert = NSAlert()
            alert.alertStyle = .warning
            alert.messageText = message
            alert.informativeText = detail
            if let parent { alert.beginSheetModal(for: parent) } else { alert.runModal() }
        }
        dataSourcesWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Brings up the route elevation profile window, reusing it when it is showing.
    private func showRouteProfile() {
        if let existing = profileWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = ProfileWindowController(host: host)
        controller.onClose = { [weak self] in self?.profileWindow = nil }
        profileWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Brings up the layers panel, reusing it when it is showing.
    private func showLayers() {
        if let existing = layersWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = LayersWindowController(host: host)
        controller.onClose = { [weak self] in self?.layersWindow = nil }
        layersWindow = controller
        if let panel = controller.window, let main = window {
            // Beside the map window's top-right corner, inside the screen.
            let frame = main.frame
            panel.setFrameTopLeftPoint(NSPoint(x: frame.maxX - panel.frame.width - 20, y: frame.maxY - 60))
        }
        controller.showWindow(nil)
    }

    /// Brings up the feature form on the model the core just opened.
    private func showFeatureForm() {
        if let existing = featureFormWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = FeatureFormWindowController(host: host)
        controller.onClose = { [weak self] in self?.featureFormWindow = nil }
        featureFormWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Brings up the attribute table on the model the core just opened.
    private func showAttributeTable() {
        if let existing = attributeTableWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = AttributeTableWindowController(host: host)
        controller.onClose = { [weak self] in self?.attributeTableWindow = nil }
        attributeTableWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Brings up the style dialog on the model the core just opened.
    private func showLayerStyle() {
        if let existing = layerStyleWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = LayerStyleWindowController(host: host)
        controller.onClose = { [weak self] in self?.layerStyleWindow = nil }
        layerStyleWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Brings up the Layer Fields dialog on the model the core just opened.
    private func showLayerFields() {
        if let existing = layerFieldsWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = LayerFieldsWindowController(host: host)
        controller.onClose = { [weak self] in self?.layerFieldsWindow = nil }
        layerFieldsWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Brings up the New Feature Layer dialog on a fresh model.
    private func showNewLayer() {
        if let existing = newLayerWindow {
            existing.reload()
            existing.showWindow(nil)
            return
        }
        let controller = NewLayerWindowController(host: host)
        controller.onClose = { [weak self] in self?.newLayerWindow = nil }
        newLayerWindow = controller
        controller.window?.center()
        controller.showWindow(nil)
    }

    /// Closes a form, table, style or fields dialog whose overlay closed, and
    /// refreshes the table. The form and style dialog are not rebuilt here:
    /// they hold the user's edits.
    private func featuresDidChange() {
        if !host.FeatureFormOpen() { featureFormWindow?.close() }
        if !host.LayerStyleOpen() { layerStyleWindow?.close() }
        if !host.LayerFieldsOpen() { layerFieldsWindow?.close() }
        if host.AttributeTableOpen() { attributeTableWindow?.reload() } else { attributeTableWindow?.close() }
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

    /// Help ▸ Show Log: opens today's log file in the default viewer (Console).
    @objc func showLog(_ sender: Any?) {
        let path = String(host.LogFilePath())
        guard !path.isEmpty else {
            present(error: "No log file", detail: "The application log could not be started.")
            return
        }
        NSWorkspace.shared.open(URL(fileURLWithPath: path))
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
