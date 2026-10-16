// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The style editor over `DeskHost`'s style model: renderer and field, the
/// legend of classes with swatches and a symbol editor for the selected class
/// (symbology), and the label settings. Edits go to the model's draft as they
/// are made. Shown as the Symbology and Labels tabs of a layer's Properties,
/// and whole in the Style This Feature… dialog.
@MainActor
final class LayerStylePanel: NSObject, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate {
    enum Part { case all, symbology, labels }

    let host: fv.desk.DeskHost
    let part: Part
    /// The panel's root view.
    let view = NSView()
    /// Receives each edit's error message, "" for success.
    var onReport: ((String) -> Void)?

    private let rendererPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let fieldPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let classesPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let modePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let varyPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let classifyButton = NSButton(title: "Classify", target: nil, action: nil)
    private let categoriseButton = NSButton(title: "Categorise into Rules", target: nil, action: nil)
    private let rendererRow = NSStackView()
    private let gradedRow = NSStackView()
    private let table = NSTableView()
    private let addButton = NSButton(title: "+", target: nil, action: nil)
    private let removeButton = NSButton(title: "\u{2212}", target: nil, action: nil)
    private let clearOverrideButton = NSButton(title: "Use Layer Style", target: nil, action: nil)
    private let symbolGrid = NSGridView()
    private let labelPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let labelSize = NSTextField(string: "")

    private static let swatchSize = NSSize(width: 36, height: 18)
    private static let valueColumn = NSUserInterfaceItemIdentifier("value")
    private static let labelColumn = NSUserInterfaceItemIdentifier("label")
    private static let swatchColumn = NSUserInterfaceItemIdentifier("swatch")
    /// Tag of the label size field, apart from the symbol rows' property indexes.
    private static let labelSizeTag = 1_000

    init(host: fv.desk.DeskHost, part: Part) {
        self.host = host
        self.part = part
        super.init()
        NSColorPanel.shared.showsAlpha = true
        buildLayout()
        reload()
    }

    /// Re-reads the whole model, keeping the selected row where it can.
    func reload() {
        let feature = host.LayerStyleIsFeature()
        rendererRow.isHidden = feature
        rendererPopup.selectItem(at: Int(host.StyleRenderer()))
        fieldPopup.removeAllItems()
        for i in 0..<host.StyleFieldCount() { fieldPopup.addItem(withTitle: String(host.StyleFieldTitle(i))) }
        let field = String(host.StyleField())
        for i in 0..<host.StyleFieldCount() where String(host.StyleFieldName(i)) == field {
            fieldPopup.selectItem(at: Int(i))
        }
        let renderer = host.StyleRenderer()
        // Under rules (3) the legend lists the rules; there is no field, class or symbol to edit here.
        fieldPopup.isHidden = renderer == 0 || renderer == 3
        classifyButton.isHidden = fieldPopup.isHidden
        categoriseButton.isHidden = feature || renderer != 1
        symbolGrid.isHidden = !feature && renderer == 3
        gradedRow.isHidden = feature || host.StyleRenderer() != 2
        classesPopup.selectItem(withTitle: String(host.StyleClassCount()))
        modePopup.selectItem(at: Int(host.StyleClassMode()))
        varyPopup.selectItem(at: Int(host.StyleClassVary()))
        addButton.isHidden = feature || renderer == 0 || renderer == 3
        removeButton.isHidden = addButton.isHidden
        clearOverrideButton.isHidden = !feature
        clearOverrideButton.isEnabled = host.StyleHasOverride()

        labelPopup.removeAllItems()
        labelPopup.addItem(withTitle: "None")
        let labelField = String(host.StyleLabelField())
        for i in 0..<host.StyleLabelFieldCount() {
            labelPopup.addItem(withTitle: String(host.StyleLabelFieldTitle(i)))
            if String(host.StyleLabelFieldName(i)) == labelField { labelPopup.selectItem(at: Int(i) + 1) }
        }
        labelSize.doubleValue = host.StyleLabelSize()

        let selected = table.selectedRow
        table.reloadData()
        let row = min(max(selected, 0), Int(host.StyleEntryCount()) - 1)
        if row >= 0 { table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false) }
        reloadSymbol()
    }

    // MARK: Layout

    private func buildLayout() {
        rendererPopup.addItems(withTitles: ["Single symbol", "Categorised", "Graduated", "Rules"])
        rendererPopup.target = self
        rendererPopup.action = #selector(rendererChanged)
        fieldPopup.target = self
        fieldPopup.action = #selector(fieldChanged)
        classifyButton.target = self
        classifyButton.action = #selector(classify)
        categoriseButton.target = self
        categoriseButton.action = #selector(categoriseIntoRules)
        rendererRow.setViews([NSTextField(labelWithString: "Draw"), rendererPopup,
                              NSTextField(labelWithString: "by"), fieldPopup, classifyButton, categoriseButton],
                             in: .leading)
        rendererRow.orientation = .horizontal

        classesPopup.addItems(withTitles: (2...10).map(String.init))
        modePopup.addItems(withTitles: ["Equal intervals", "Quantiles"])
        varyPopup.addItems(withTitles: ["Colour ramp", "Size"])
        gradedRow.setViews([NSTextField(labelWithString: "Classes"), classesPopup, modePopup,
                            NSTextField(labelWithString: "varying"), varyPopup], in: .leading)
        gradedRow.orientation = .horizontal

        for (id, title, width) in [(Self.swatchColumn, "", 48.0), (Self.valueColumn, "Value", 140.0),
                                   (Self.labelColumn, "Legend", 200.0)] {
            let column = NSTableColumn(identifier: id)
            column.title = title
            column.width = width
            table.addTableColumn(column)
        }
        table.rowHeight = Self.swatchSize.height + 6
        table.dataSource = self
        table.delegate = self
        table.usesAlternatingRowBackgroundColors = true
        let tableScroll = NSScrollView()
        tableScroll.documentView = table
        tableScroll.hasVerticalScroller = true
        tableScroll.borderType = .bezelBorder

        addButton.target = self
        addButton.action = #selector(addEntry)
        removeButton.target = self
        removeButton.action = #selector(removeEntry)
        clearOverrideButton.target = self
        clearOverrideButton.action = #selector(clearOverride)
        let entryButtons = NSStackView(views: [addButton, removeButton, clearOverrideButton])
        entryButtons.orientation = .horizontal

        symbolGrid.rowSpacing = 6
        symbolGrid.columnSpacing = 8
        symbolGrid.rowAlignment = .firstBaseline
        let symbolBox = NSBox()
        symbolBox.title = "Symbol"
        symbolBox.contentView = symbolGrid

        labelPopup.target = self
        labelPopup.action = #selector(labelChanged)
        labelSize.tag = Self.labelSizeTag
        labelSize.delegate = self
        labelSize.widthAnchor.constraint(equalToConstant: 50).isActive = true
        let labelRow = NSStackView(views: [NSTextField(labelWithString: "Labels"), labelPopup,
                                           NSTextField(labelWithString: "size"), labelSize,
                                           NSTextField(labelWithString: "px")])
        labelRow.orientation = .horizontal

        let middle = NSStackView(views: [tableScroll, symbolBox])
        middle.orientation = .horizontal
        middle.alignment = .top
        middle.distribution = .fill

        let symbology: [NSView] = [rendererRow, gradedRow, middle, entryButtons]
        let views: [NSView]
        switch part {
        case .all: views = symbology + [labelRow]
        case .symbology: views = symbology
        case .labels: views = [labelRow]
        }
        let stack = NSStackView(views: views)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        var constraints = [
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.topAnchor),
        ]
        if part == .labels {
            constraints.append(stack.bottomAnchor.constraint(lessThanOrEqualTo: view.bottomAnchor))
        } else {
            constraints += [
                stack.bottomAnchor.constraint(equalTo: view.bottomAnchor),
                middle.widthAnchor.constraint(equalTo: stack.widthAnchor),
                tableScroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 180),
                symbolBox.widthAnchor.constraint(equalToConstant: 220),
            ]
            middle.setHuggingPriority(.defaultLow, for: .vertical)
        }
        NSLayoutConstraint.activate(constraints)
    }

    // MARK: Legend table

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.StyleEntryCount()) }

    func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
        guard let id = column?.identifier else { return nil }
        let i = Int32(row)
        if id == Self.swatchColumn { return NSImageView(image: swatch(for: i) ?? NSImage()) }
        // A rules row has a legend title to edit but no value.
        let editable = !host.StyleEntryIsOther(i) && host.StyleRenderer() != 0 && !host.LayerStyleIsFeature() &&
            !(id == Self.valueColumn && host.StyleRenderer() == 3)
        let text = id == Self.valueColumn ? String(host.StyleEntryValue(i)) : String(host.StyleEntryLabel(i))
        let field = NSTextField(string: text)
        field.isBordered = false
        field.drawsBackground = false
        field.isEditable = editable
        field.tag = row
        field.identifier = id
        field.delegate = self
        return field
    }

    func tableViewSelectionDidChange(_ notification: Notification) { reloadSymbol() }

    /// Entry `i`'s swatch at the window's backing scale.
    private func swatch(for i: Int32) -> NSImage? {
        let scale = view.window?.backingScaleFactor ?? 2
        let w = Int(Self.swatchSize.width * scale), h = Int(Self.swatchSize.height * scale)
        var bytes = Data(count: w * h * 4)
        let copied = bytes.withUnsafeMutableBytes { buf in
            host.CopyStyleSwatch(i, Int32(w), Int32(h), Double(scale),
                                 buf.baseAddress!.assumingMemoryBound(to: UInt8.self), buf.count)
        }
        guard copied, let provider = CGDataProvider(data: bytes as CFData),
              let space = CGColorSpace(name: CGColorSpace.sRGB),
              let image = CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32,
                                  bytesPerRow: w * 4, space: space,
                                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: true,
                                  intent: .defaultIntent) else { return nil }
        return NSImage(cgImage: image, size: Self.swatchSize)
    }

    // MARK: Symbol editor

    private var entry: Int32 { Int32(max(table.selectedRow, 0)) }

    /// Rebuilds the symbol rows for the selected entry.
    private func reloadSymbol() {
        // removeRow(at:) leaves the cells' views in the grid; they must be removed separately.
        symbolGrid.subviews.forEach { $0.removeFromSuperview() }
        while symbolGrid.numberOfRows > 0 { symbolGrid.removeRow(at: 0) }
        var group = ""
        for k in 0..<host.StylePropertyCount() {
            let g = String(host.StylePropertyGroup(k))
            if g != group {
                group = g
                let heading = NSTextField(labelWithString: g)
                heading.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
                symbolGrid.addRow(with: [heading, NSGridCell.emptyContentView])
            }
            let row = symbolGrid.addRow(with: [NSTextField(labelWithString: String(host.StylePropertyTitle(k))), editor(for: k)])
            // A colour well has no baseline; first-baseline alignment drops it below its label.
            if host.StylePropertyKind(k) == 0 {
                row.rowAlignment = .none
                row.yPlacement = .center
            }
        }
    }

    /// The editor for property `k` of the selected entry.
    private func editor(for k: Int32) -> NSView {
        switch host.StylePropertyKind(k) {
        case 0:
            let well = NSColorWell(style: .minimal)
            well.color = Self.color(host.StyleColor(entry, k))
            well.tag = Int(k)
            well.target = self
            well.action = #selector(colorChanged(_:))
            return well
        case 2:
            let popup = NSPopUpButton(frame: .zero, pullsDown: false)
            for c in 0..<host.StylePropertyChoiceCount(k) { popup.addItem(withTitle: String(host.StylePropertyChoice(k, c))) }
            popup.selectItem(at: Int(host.StyleChoice(entry, k)))
            popup.tag = Int(k)
            popup.target = self
            popup.action = #selector(choiceChanged(_:))
            return popup
        default:
            let field = NSTextField(string: String(format: "%g", host.StyleNumber(entry, k)))
            field.tag = Int(k)
            field.delegate = self
            field.widthAnchor.constraint(equalToConstant: 60).isActive = true
            return field
        }
    }

    private static func color(_ rgba: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((rgba >> 24) & 0xff) / 255, green: CGFloat((rgba >> 16) & 0xff) / 255,
                blue: CGFloat((rgba >> 8) & 0xff) / 255, alpha: CGFloat(rgba & 0xff) / 255)
    }

    private static func rgba(_ color: NSColor) -> UInt32 {
        let c = color.usingColorSpace(.sRGB) ?? color
        func byte(_ v: CGFloat) -> UInt32 { UInt32(max(0, min(255, (v * 255).rounded()))) }
        return byte(c.redComponent) << 24 | byte(c.greenComponent) << 16 | byte(c.blueComponent) << 8 | byte(c.alphaComponent)
    }

    // MARK: Edits

    /// Passes `error` on and redraws the panel.
    private func report(_ error: std.string) {
        onReport?(String(error))
        reload()
    }

    /// Leaving rules for a generator is Regenerate…, which replaces the rules; it asks first.
    @objc private func rendererChanged() {
        let chosen = Int32(rendererPopup.indexOfSelectedItem)
        if host.StyleRenderer() == 3 && chosen != 3 {
            let alert = NSAlert()
            alert.messageText = "Regenerate the style as \(rendererPopup.titleOfSelectedItem ?? "")?"
            alert.informativeText = "The layer's rules are replaced by a generated style when you apply."
            alert.addButton(withTitle: "Regenerate")
            alert.addButton(withTitle: "Cancel")
            if alert.runModal() != .alertFirstButtonReturn { return reload() }
        }
        report(host.SetStyleRenderer(chosen))
    }

    @objc private func categoriseIntoRules() { report(host.CategoriseStyleIntoRules()) }

    @objc private func fieldChanged() {
        report(host.SetStyleField(host.StyleFieldName(Int32(fieldPopup.indexOfSelectedItem))))
    }

    @objc private func classify() {
        let classes = Int32(classesPopup.titleOfSelectedItem ?? "5") ?? 5
        report(host.ClassifyStyle(classes, Int32(modePopup.indexOfSelectedItem), Int32(varyPopup.indexOfSelectedItem)))
    }

    @objc private func addEntry() { report(host.AddStyleEntry()) }

    @objc private func removeEntry() {
        guard table.selectedRow >= 0 else { return NSSound.beep() }
        report(host.RemoveStyleEntry(Int32(table.selectedRow)))
    }

    @objc private func clearOverride() { report(host.ClearStyleOverride()) }

    @objc private func colorChanged(_ sender: NSColorWell) {
        report(host.SetStyleColor(entry, Int32(sender.tag), Self.rgba(sender.color)))
    }

    @objc private func choiceChanged(_ sender: NSPopUpButton) {
        report(host.SetStyleChoice(entry, Int32(sender.tag), Int32(sender.indexOfSelectedItem)))
    }

    @objc private func labelChanged() { applyLabel() }

    private func applyLabel() {
        let i = labelPopup.indexOfSelectedItem
        let name = i <= 0 ? std.string("") : host.StyleLabelFieldName(Int32(i - 1))
        report(host.SetStyleLabel(name, labelSize.doubleValue))
    }

    /// Commits a number, a class value or a legend label when its field ends editing.
    func controlTextDidEndEditing(_ obj: Notification) {
        guard let field = obj.object as? NSTextField else { return }
        if field.tag == Self.labelSizeTag {
            applyLabel()
        } else if field.identifier == Self.valueColumn {
            report(host.SetStyleEntryValue(Int32(field.tag), std.string(field.stringValue)))
        } else if field.identifier == Self.labelColumn {
            report(host.SetStyleEntryLabel(Int32(field.tag), std.string(field.stringValue)))
        } else {
            guard let value = Double(field.stringValue) else { return report(std.string("enter a number")) }
            report(host.SetStyleNumber(entry, Int32(field.tag), value))
        }
    }
}

/// The Style This Feature… dialog: the whole style panel over one feature's
/// symbol, with Apply, OK, Revert and Cancel. Closing it closes the model.
@MainActor
final class LayerStyleWindowController: NSWindowController, NSWindowDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let panel: LayerStylePanel
    /// Cleared by `dismiss()`, when the model now belongs to Properties.
    private var closesModel = true
    private let messageLabel = NSTextField(wrappingLabelWithString: "")

    init(host: fv.desk.DeskHost) {
        self.host = host
        panel = LayerStylePanel(host: host, part: .all)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 520),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: true)
        window.minSize = NSSize(width: 480, height: 420)
        super.init(window: window)
        window.delegate = self
        panel.onReport = { [weak self] in self?.show(message: $0) }
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the title and the panel.
    func reload() {
        window?.title = String(host.LayerStyleTitle())
        panel.reload()
    }

    private func buildLayout(in window: NSWindow) {
        messageLabel.textColor = .systemRed
        messageLabel.isHidden = true
        let revert = NSButton(title: "Revert", target: self, action: #selector(revertStyle))
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelStyle))
        cancel.keyEquivalent = "\u{1b}"
        let apply = NSButton(title: "Apply", target: self, action: #selector(applyStyle))
        let ok = NSButton(title: "OK", target: self, action: #selector(okStyle))
        ok.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [revert, spacer, cancel, apply, ok])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [panel.view, messageLabel, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 12, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(stack)
        window.contentView = content
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            panel.view.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            messageLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
        ])
        panel.view.setContentHuggingPriority(.defaultLow, for: .vertical)
    }

    private func show(message: String) {
        messageLabel.stringValue = message
        messageLabel.isHidden = message.isEmpty
    }

    @discardableResult
    private func apply() -> Bool {
        window?.makeFirstResponder(nil)  // ends an edit in progress
        let error = String(host.ApplyLayerStyle())
        show(message: error)
        panel.reload()
        return error.isEmpty
    }

    @objc private func applyStyle() { apply() }

    @objc private func okStyle() {
        if apply() { close() }
    }

    @objc private func cancelStyle() { close() }

    @objc private func revertStyle() {
        show(message: String(host.RevertLayerStyle()))
        panel.reload()
    }

    /// Closes the window without closing the style model.
    func dismiss() {
        closesModel = false
        close()
    }

    func windowWillClose(_ notification: Notification) {
        if closesModel { host.CloseLayerStyle() }
        onClose?()
    }
}
