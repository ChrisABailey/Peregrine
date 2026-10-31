// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The Properties dialog of a layer or a source: one tab per tab `DeskHost`
/// lists. Source is read-only (a heading per section, values selectable);
/// General edits name, description, credits and scale range; Display the
/// opacity, map tip and hyperlink fields; Symbology and Labels hold the style
/// panel, Fields the fields panel; Selection the selection colour; Definition
/// Query the query builder. Apply writes every tab, OK applies and closes,
/// Revert drops the edits; a source's sheet has the Source tab alone and no
/// buttons. Closing the window closes the tabs' models.
@MainActor
final class PropertiesWindowController: NSWindowController, NSWindowDelegate, NSTextFieldDelegate,
    NSTextViewDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private enum Tab: Int32 { case source = 0, general, symbology, labels, fields, display, selection, definitionQuery }

    private let tabView = NSTabView()
    /// The Source sheet's rows, replaced on each reload: NSGridView traps when
    /// it removes a row whose cells are merged, as the section headings are.
    private var sourceGrid = NSGridView(numberOfColumns: 2, rows: 0)
    private let sourceDocument = FlippedView()
    private let sourceScroll = NSScrollView()
    private let generalPage = NSView()
    private let nameField = NSTextField(string: "")
    private let descriptionView = NSTextView()
    private let creditsField = NSTextField(string: "")
    private let minScaleField = NSTextField(string: "")
    private let maxScaleField = NSTextField(string: "")
    private var symbology: LayerStylePanel?
    private var labels: LayerStylePanel?
    private var fields: LayerFieldsPanel?
    private var definitionQuery: QueryBuilderPanel?
    private let displayPage = NSView()
    private let opacitySlider = NSSlider(value: 100, minValue: 0, maxValue: 100, target: nil, action: nil)
    private let opacityLabel = NSTextField(labelWithString: "")
    private let opacityRow = NSStackView()
    private let mapTipPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let hyperlinkPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let displayFieldsGrid = NSGridView()
    private let selectionPage = NSView()
    private let defaultSelectionRadio = NSButton(radioButtonWithTitle: "Use the default selection colour",
                                                 target: nil, action: nil)
    private let ownSelectionRadio = NSButton(radioButtonWithTitle: "Use this colour:", target: nil, action: nil)
    private let selectionWell = NSColorWell(style: .minimal)
    private let selectionWidthField = NSTextField(string: "")
    private let selectionCountLabel = NSTextField(labelWithString: "")
    /// The tabs the pages were built for.
    private var tabs: [Int32] = []
    private let messageLabel = NSTextField(wrappingLabelWithString: "")
    private let buttons = NSStackView()
    /// Set while the General page is filled from the model, so its changes are not edits.
    private var loading = false
    /// A General control changed since the page was last written to the draft.
    private var generalDirty = false

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 680, height: 540),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: true)
        window.minSize = NSSize(width: 480, height: 360)
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    // MARK: Layout

    private func buildLayout(in window: NSWindow) {
        sourceDocument.translatesAutoresizingMaskIntoConstraints = false
        sourceScroll.documentView = sourceDocument
        sourceScroll.hasVerticalScroller = true
        sourceScroll.drawsBackground = false
        sourceDocument.widthAnchor.constraint(equalTo: sourceScroll.contentView.widthAnchor).isActive = true
        buildGeneral()
        buildDisplay()
        buildSelection()

        tabView.translatesAutoresizingMaskIntoConstraints = false
        messageLabel.textColor = .systemRed
        messageLabel.isHidden = true
        let revert = NSButton(title: "Revert", target: self, action: #selector(revertProperties))
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelProperties))
        cancel.keyEquivalent = "\u{1b}"
        let apply = NSButton(title: "Apply", target: self, action: #selector(applyProperties))
        let ok = NSButton(title: "OK", target: self, action: #selector(okProperties))
        ok.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        buttons.setViews([revert, spacer, cancel, apply, ok], in: .leading)
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [tabView, messageLabel, buttons])
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
            tabView.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            messageLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])
        tabView.setContentHuggingPriority(.defaultLow, for: .vertical)
    }

    /// The General page: each control writes the draft when editing ends;
    /// Current fills a scale with the map's.
    private func buildGeneral() {
        for field in [nameField, creditsField, minScaleField, maxScaleField] { field.delegate = self }
        minScaleField.placeholderString = "No limit"
        maxScaleField.placeholderString = "No limit"
        descriptionView.isRichText = false
        descriptionView.font = NSFont.systemFont(ofSize: NSFont.systemFontSize)
        descriptionView.delegate = self
        descriptionView.autoresizingMask = [.width]
        let descriptionScroll = NSScrollView()
        descriptionScroll.documentView = descriptionView
        descriptionScroll.hasVerticalScroller = true
        descriptionScroll.borderType = .bezelBorder
        descriptionScroll.heightAnchor.constraint(equalToConstant: 90).isActive = true

        let minCurrent = NSButton(title: "Current", target: self, action: #selector(minScaleCurrent))
        let maxCurrent = NSButton(title: "Current", target: self, action: #selector(maxScaleCurrent))
        let heading = NSTextField(labelWithString: "Scale range")
        heading.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
        let help = NSTextField(wrappingLabelWithString:
            "The layer is not drawn zoomed out beyond the minimum scale or zoomed in beyond the maximum.")
        help.textColor = .secondaryLabelColor
        let empty = NSGridCell.emptyContentView

        let grid = NSGridView(views: [
            [NSTextField(labelWithString: "Name"), nameField, empty],
            [NSTextField(labelWithString: "Description"), descriptionScroll, empty],
            [NSTextField(labelWithString: "Credits"), creditsField, empty],
            [heading, empty, empty],
            [help, empty, empty],
            [NSTextField(labelWithString: "Minimum (out)"), minScaleField, minCurrent],
            [NSTextField(labelWithString: "Maximum (in)"), maxScaleField, maxCurrent],
        ])
        grid.rowSpacing = 6
        grid.columnSpacing = 8
        grid.rowAlignment = .firstBaseline
        grid.column(at: 0).xPlacement = .trailing
        for row in [3, 4] {
            grid.mergeCells(inHorizontalRange: NSRange(location: 0, length: 3), verticalRange: NSRange(location: row, length: 1))
            grid.cell(atColumnIndex: 0, rowIndex: row).xPlacement = .leading
        }
        grid.row(at: 3).topPadding = 10
        grid.translatesAutoresizingMaskIntoConstraints = false
        generalPage.addSubview(grid)
        NSLayoutConstraint.activate([
            grid.leadingAnchor.constraint(equalTo: generalPage.leadingAnchor, constant: 12),
            grid.trailingAnchor.constraint(equalTo: generalPage.trailingAnchor, constant: -12),
            grid.topAnchor.constraint(equalTo: generalPage.topAnchor, constant: 12),
            grid.bottomAnchor.constraint(lessThanOrEqualTo: generalPage.bottomAnchor, constant: -12),
            help.widthAnchor.constraint(lessThanOrEqualTo: grid.widthAnchor),
        ])
    }

    /// The Display page: opacity, and the fields a hover and a link show.
    private func buildDisplay() {
        opacitySlider.target = self
        opacitySlider.action = #selector(displayChanged)
        opacitySlider.isContinuous = true
        opacitySlider.widthAnchor.constraint(equalToConstant: 220).isActive = true
        opacityRow.setViews([NSTextField(labelWithString: "Opacity"), opacitySlider, opacityLabel], in: .leading)
        opacityRow.orientation = .horizontal
        for popup in [mapTipPopup, hyperlinkPopup] {
            popup.target = self
            popup.action = #selector(displayChanged)
        }
        let tipHelp = NSTextField(wrappingLabelWithString: "The field shown when the pointer rests on a feature.")
        tipHelp.textColor = .secondaryLabelColor
        let linkHelp = NSTextField(wrappingLabelWithString: "The field holding each feature\u{2019}s web address or file.")
        linkHelp.textColor = .secondaryLabelColor
        let empty = NSGridCell.emptyContentView
        displayFieldsGrid.addRow(with: [NSTextField(labelWithString: "Map tip"), mapTipPopup])
        displayFieldsGrid.addRow(with: [empty, tipHelp])
        displayFieldsGrid.addRow(with: [NSTextField(labelWithString: "Hyperlink"), hyperlinkPopup])
        displayFieldsGrid.addRow(with: [empty, linkHelp])
        displayFieldsGrid.rowSpacing = 6
        displayFieldsGrid.columnSpacing = 8
        displayFieldsGrid.column(at: 0).xPlacement = .trailing
        let stack = NSStackView(views: [opacityRow, displayFieldsGrid])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 16
        Self.pin(stack, in: displayPage)
    }

    /// The Selection page: the default halo or a colour and width of the layer's own.
    private func buildSelection() {
        for radio in [defaultSelectionRadio, ownSelectionRadio] {
            radio.target = self
            radio.action = #selector(selectionChanged)
        }
        selectionWell.target = self
        selectionWell.action = #selector(selectionChanged)
        selectionWidthField.delegate = self
        selectionWidthField.widthAnchor.constraint(equalToConstant: 60).isActive = true
        let ownRow = NSStackView(views: [ownSelectionRadio, selectionWell, NSTextField(labelWithString: "Width"),
                                         selectionWidthField, NSTextField(labelWithString: "px")])
        ownRow.orientation = .horizontal
        let help = NSTextField(wrappingLabelWithString:
            "Selected features are drawn over a halo of this colour, wider than the feature by the width.")
        help.textColor = .secondaryLabelColor
        selectionCountLabel.textColor = .secondaryLabelColor
        let select = NSButton(title: "Select by Attributes\u{2026}", target: self, action: #selector(selectByAttributes))
        let countRow = NSStackView(views: [selectionCountLabel, select])
        countRow.orientation = .horizontal
        let stack = NSStackView(views: [defaultSelectionRadio, ownRow, help, countRow])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.setCustomSpacing(20, after: help)
        help.widthAnchor.constraint(lessThanOrEqualToConstant: 520).isActive = true
        Self.pin(stack, in: selectionPage)
    }

    /// Pins `content` to the top of `page`, inset from its edges.
    private static func pin(_ content: NSView, in page: NSView) {
        content.translatesAutoresizingMaskIntoConstraints = false
        page.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: page.leadingAnchor, constant: 12),
            content.trailingAnchor.constraint(lessThanOrEqualTo: page.trailingAnchor, constant: -12),
            content.topAnchor.constraint(equalTo: page.topAnchor, constant: 12),
            content.bottomAnchor.constraint(lessThanOrEqualTo: page.bottomAnchor, constant: -12),
        ])
    }

    // MARK: Reload

    private func hostTabs() -> [Int32] {
        (0..<host.LayerPropertiesTabCount()).map { host.LayerPropertiesTabAt($0) }
    }

    /// Rebuilds every page from the host and shows the tab it asks for.
    func reload() {
        for item in tabView.tabViewItems { tabView.removeTabViewItem(item) }
        symbology = nil
        labels = nil
        fields = nil
        definitionQuery = nil
        tabs = hostTabs()
        let report: (String) -> Void = { [weak self] in self?.show(message: $0) }
        for (i, tab) in tabs.enumerated() {
            let page: NSView
            switch Tab(rawValue: tab) {
            case .source: page = sourceScroll
            case .general: page = generalPage
            case .symbology:
                let panel = LayerStylePanel(host: host, part: .symbology)
                panel.onReport = report
                symbology = panel
                page = Self.padded(panel.view)
            case .labels:
                let panel = LayerStylePanel(host: host, part: .labels)
                panel.onReport = report
                labels = panel
                page = Self.padded(panel.view)
            case .fields:
                let panel = LayerFieldsPanel(host: host)
                panel.onReport = report
                fields = panel
                page = Self.padded(panel.view)
            case .display: page = displayPage
            case .selection: page = selectionPage
            case .definitionQuery:
                let panel = QueryBuilderPanel(host: host, which: QueryBuilderPanel.definition)
                panel.onReport = report
                definitionQuery = panel
                page = Self.padded(panel.view)
            case nil: continue
            }
            let item = NSTabViewItem(identifier: NSNumber(value: tab))
            item.label = String(host.LayerPropertiesTabTitle(Int32(i)))
            item.view = page
            tabView.addTabViewItem(item)
        }
        tabView.tabViewType = tabs.count > 1 ? .topTabsBezelBorder : .noTabsNoBorder
        buttons.isHidden = tabs.count <= 1
        reloadSource()
        reloadGeneral()
        reloadDisplay()
        reloadSelection()
        show(message: "")
        showTab(host.LayerPropertiesTab())
    }

    /// Rebuilds the pages when the host's tabs changed (a style tab taken by
    /// Style This Feature…), keeping the tab shown. False when the layer the
    /// dialog was open on has gone.
    func refresh() -> Bool {
        if tabs.contains(Tab.general.rawValue) && !host.LayerGeneralOpen() { return false }
        if hostTabs() == tabs { return true }
        let shown = currentTab
        reload()
        if let shown { showTab(shown) }
        return true
    }

    /// Shows the page of `tab`, a `fv::desk::PropertiesTab`.
    func showTab(_ tab: Int32) {
        if let i = tabs.firstIndex(of: tab) { tabView.selectTabViewItem(at: i) }
    }

    private var currentTab: Int32? {
        guard let item = tabView.selectedTabViewItem else { return nil }
        return (item.identifier as? NSNumber)?.int32Value
    }

    /// A panel inset from the tab's edges.
    private static func padded(_ panel: NSView) -> NSView {
        let page = NSView()
        panel.translatesAutoresizingMaskIntoConstraints = false
        page.addSubview(panel)
        NSLayoutConstraint.activate([
            panel.leadingAnchor.constraint(equalTo: page.leadingAnchor, constant: 12),
            panel.trailingAnchor.constraint(equalTo: page.trailingAnchor, constant: -12),
            panel.topAnchor.constraint(equalTo: page.topAnchor, constant: 12),
            panel.bottomAnchor.constraint(equalTo: page.bottomAnchor, constant: -12),
        ])
        return page
    }

    private func reloadSource() {
        window?.title = String(host.LayerPropertiesTitle())
        sourceGrid.removeFromSuperview()
        sourceGrid = NSGridView(numberOfColumns: 2, rows: 0)
        sourceGrid.rowSpacing = 4
        sourceGrid.columnSpacing = 12
        sourceGrid.rowAlignment = .firstBaseline
        sourceGrid.column(at: 0).xPlacement = .trailing
        sourceGrid.translatesAutoresizingMaskIntoConstraints = false
        sourceDocument.addSubview(sourceGrid)
        NSLayoutConstraint.activate([
            sourceGrid.leadingAnchor.constraint(equalTo: sourceDocument.leadingAnchor, constant: 12),
            sourceGrid.trailingAnchor.constraint(lessThanOrEqualTo: sourceDocument.trailingAnchor, constant: -12),
            sourceGrid.topAnchor.constraint(equalTo: sourceDocument.topAnchor, constant: 12),
            sourceGrid.bottomAnchor.constraint(equalTo: sourceDocument.bottomAnchor, constant: -12),
        ])
        var section = ""
        for i in 0..<host.LayerPropertyCount() {
            let s = String(host.LayerPropertySection(i))
            if i == 0 || s != section {
                section = s
                let heading = NSTextField(labelWithString: s)
                heading.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
                let row = sourceGrid.addRow(with: [heading, NSGridCell.emptyContentView])
                sourceGrid.mergeCells(inHorizontalRange: NSRange(location: 0, length: 2),
                                      verticalRange: NSRange(location: sourceGrid.numberOfRows - 1, length: 1))
                sourceGrid.cell(for: heading)?.xPlacement = .leading
                if sourceGrid.numberOfRows > 1 { row.topPadding = 10 }
            }
            let key = NSTextField(labelWithString: String(host.LayerPropertyKey(i)))
            key.textColor = .secondaryLabelColor
            key.alignment = .right
            let value = NSTextField(wrappingLabelWithString: String(host.LayerPropertyValue(i)))
            value.isSelectable = true
            value.preferredMaxLayoutWidth = 440
            sourceGrid.addRow(with: [key, value])
        }
    }

    private func reloadGeneral() {
        loading = true
        nameField.stringValue = String(host.LayerGeneralName())
        descriptionView.string = String(host.LayerGeneralDescription())
        creditsField.stringValue = String(host.LayerGeneralCredits())
        minScaleField.stringValue = String(host.LayerGeneralMinScale())
        maxScaleField.stringValue = String(host.LayerGeneralMaxScale())
        loading = false
        generalDirty = false
    }

    private func reloadDisplay() {
        guard host.LayerDisplayOpen() else { return }
        let opacity = host.LayerDisplayOpacity()
        opacityRow.isHidden = !host.LayerDisplayHasOpacity()
        opacitySlider.integerValue = Int(opacity)
        opacityLabel.stringValue = "\(opacity)%"
        let count = host.LayerDisplayFieldCount()
        displayFieldsGrid.isHidden = count == 0
        func fill(_ popup: NSPopUpButton, none: String, selected: String) {
            popup.removeAllItems()
            popup.addItem(withTitle: none)
            popup.lastItem?.representedObject = ""
            for i in 0..<count {
                popup.addItem(withTitle: String(host.LayerDisplayFieldTitle(i)))
                popup.lastItem?.representedObject = String(host.LayerDisplayFieldName(i))
            }
            let at = popup.itemArray.firstIndex { ($0.representedObject as? String) == selected } ?? 0
            popup.selectItem(at: at)
        }
        fill(mapTipPopup, none: "The feature\u{2019}s title", selected: String(host.LayerDisplayMapTipField()))
        fill(hyperlinkPopup, none: "None", selected: String(host.LayerDisplayHyperlinkField()))
    }

    private func reloadSelection() {
        guard host.LayerSelectionOpen() else { return }
        let useDefault = host.LayerSelectionUsesDefault()
        defaultSelectionRadio.state = useDefault ? .on : .off
        ownSelectionRadio.state = useDefault ? .off : .on
        selectionWell.color = NSColor(rgba: host.LayerSelectionColor())
        selectionWell.isEnabled = !useDefault
        selectionWidthField.stringValue = String(format: "%g", host.LayerSelectionWidth())
        selectionWidthField.isEnabled = !useDefault
        let n = host.LayerSelectionCount()
        selectionCountLabel.stringValue = n == 1 ? "1 feature is selected." : "\(n) features are selected."
    }

    // MARK: Display and Selection edits

    @objc private func displayChanged() {
        opacityLabel.stringValue = "\(opacitySlider.integerValue)%"
        let tip = mapTipPopup.selectedItem?.representedObject as? String ?? ""
        let link = hyperlinkPopup.selectedItem?.representedObject as? String ?? ""
        show(message: String(host.SetLayerDisplay(Int32(opacitySlider.integerValue), std.string(tip), std.string(link))))
    }

    @objc private func selectionChanged(_ sender: Any?) {
        if let radio = sender as? NSButton, radio === defaultSelectionRadio || radio === ownSelectionRadio {
            defaultSelectionRadio.state = radio === defaultSelectionRadio ? .on : .off
            ownSelectionRadio.state = radio === ownSelectionRadio ? .on : .off
        }
        commitSelection()
    }

    /// Writes the Selection page to the draft; false, with the message shown, when refused.
    @discardableResult
    private func commitSelection() -> Bool {
        guard host.LayerSelectionOpen() else { return true }
        let useDefault = defaultSelectionRadio.state == .on
        selectionWell.isEnabled = !useDefault
        selectionWidthField.isEnabled = !useDefault
        let width = Double(selectionWidthField.stringValue.trimmingCharacters(in: .whitespaces)) ?? -1
        let error = String(host.SetLayerSelection(useDefault, selectionWell.color.rgba, width))
        show(message: error)
        return error.isEmpty
    }

    @objc private func selectByAttributes() {
        show(message: String(host.Execute(std.string("layers.select_by_attributes"))))
    }

    // MARK: General edits

    func controlTextDidChange(_ obj: Notification) {
        if !loading { generalDirty = true }
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        if obj.object as? NSTextField === selectionWidthField {
            commitSelection()
            return
        }
        if generalDirty { commitGeneral() }
    }

    func textDidChange(_ notification: Notification) {
        if !loading { generalDirty = true }
    }

    func textDidEndEditing(_ notification: Notification) {
        if generalDirty { commitGeneral() }
    }

    @objc private func minScaleCurrent() {
        minScaleField.stringValue = String(host.StatusScale())
        commitGeneral()
    }

    @objc private func maxScaleCurrent() {
        maxScaleField.stringValue = String(host.StatusScale())
        commitGeneral()
    }

    /// Writes the General page to the draft; false, with the message shown, when refused.
    @discardableResult
    private func commitGeneral() -> Bool {
        generalDirty = false
        guard host.LayerGeneralOpen() else { return true }
        let error = String(host.SetLayerGeneral(std.string(nameField.stringValue), std.string(descriptionView.string),
                                                std.string(creditsField.stringValue),
                                                std.string(minScaleField.stringValue),
                                                std.string(maxScaleField.stringValue)))
        show(message: error)
        return error.isEmpty
    }

    private func show(message: String) {
        messageLabel.stringValue = message
        messageLabel.isHidden = message.isEmpty
    }

    // MARK: Buttons

    /// Applies every tab, asking first when field values would be lost.
    @discardableResult
    private func apply() -> Bool {
        window?.makeFirstResponder(nil)  // ends an edit in progress
        if generalDirty && !commitGeneral() { return false }
        if host.LayerSelectionOpen() && ownSelectionRadio.state == .on && !commitSelection() { return false }
        if !messageLabel.isHidden { return false }
        let loss = String(host.LayerFieldsLoss())
        if !loss.isEmpty {
            let alert = NSAlert()
            alert.messageText = "Change the layer\u{2019}s fields?"
            alert.informativeText = loss
            alert.alertStyle = .warning
            alert.addButton(withTitle: "Change Fields")
            alert.addButton(withTitle: "Cancel")
            if alert.runModal() != .alertFirstButtonReturn { return false }
        }
        let error = String(host.ApplyLayerProperties())
        if hostTabs() == tabs {
            reloadSource()
            reloadGeneral()
            reloadDisplay()
            reloadSelection()
            symbology?.reload()
            labels?.reload()
            fields?.reload()
            definitionQuery?.reload()
        } else {
            // A change of fields reopened the style model; its panels are rebuilt.
            let shown = currentTab
            reload()
            if let shown { showTab(shown) }
        }
        show(message: error)
        return error.isEmpty
    }

    @objc private func applyProperties() { apply() }

    @objc private func okProperties() {
        if apply() { close() }
    }

    @objc private func cancelProperties() { close() }

    @objc private func revertProperties() {
        let error = String(host.RevertLayerProperties())
        reloadGeneral()
        reloadDisplay()
        reloadSelection()
        symbology?.reload()
        labels?.reload()
        fields?.reload()
        definitionQuery?.reload()
        show(message: error)
    }

    func windowWillClose(_ notification: Notification) {
        host.CloseLayerProperties()
        onClose?()
    }
}

extension NSColor {
    /// A colour from 0xRRGGBBAA, as `DeskHost` passes colours.
    convenience init(rgba: UInt32) {
        self.init(srgbRed: CGFloat((rgba >> 24) & 0xff) / 255, green: CGFloat((rgba >> 16) & 0xff) / 255,
                  blue: CGFloat((rgba >> 8) & 0xff) / 255, alpha: CGFloat(rgba & 0xff) / 255)
    }

    /// The colour as 0xRRGGBBAA in sRGB.
    var rgba: UInt32 {
        let c = usingColorSpace(.sRGB) ?? self
        func byte(_ v: CGFloat) -> UInt32 { UInt32(max(0, min(255, (v * 255).rounded()))) }
        return byte(c.redComponent) << 24 | byte(c.greenComponent) << 16 | byte(c.blueComponent) << 8 | byte(c.alphaComponent)
    }
}

/// A document view laid out from the top, as a scroll view's content expects.
private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}
