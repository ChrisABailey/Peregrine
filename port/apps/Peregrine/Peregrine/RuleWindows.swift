// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

// MARK: Rule prompts

/// The small questions the rule menu asks: a new rule's name and type, a new
/// name, a new type. Each runs as a sheet on `window` and calls the host.
@MainActor
enum RulePrompts {
    /// The current overlay's layer row; -1 for none.
    static func currentLayerRow(_ host: fv.desk.DeskHost) -> Int32 {
        (0..<host.LayerCount()).first { host.LayerCurrent($0) } ?? -1
    }

    /// The rule types the host offers, in menu order.
    static func types(_ host: fv.desk.DeskHost) -> [String] {
        (0..<host.RuleTypeCount()).map { String(host.RuleTypeName($0)) }
    }

    /// Asks for a name and type, then adds the rule to the current layer.
    static func addRule(host: fv.desk.DeskHost, window: NSWindow?, done: @escaping () -> Void) {
        let row = currentLayerRow(host)
        guard row >= 0 else { return }
        let name = NSTextField(string: "")
        name.placeholderString = "Rule name"
        let type = NSPopUpButton(frame: .zero, pullsDown: false)
        type.addItems(withTitles: types(host))
        type.selectItem(withTitle: "line")
        let accessory = NSStackView(views: [name, type])
        accessory.orientation = .vertical
        accessory.alignment = .leading
        accessory.frame = NSRect(x: 0, y: 0, width: 260, height: 54)
        name.widthAnchor.constraint(equalToConstant: 260).isActive = true
        ask(window: window, message: "Add Rule", accessory: accessory, focus: name) {
            report(host.AddRule(row, std.string(name.stringValue), std.string(type.titleOfSelectedItem ?? "line")),
                   window: window)
            done()
        }
    }

    /// Asks for the current rule's new name.
    static func rename(host: fv.desk.DeskHost, window: NSWindow?, done: @escaping () -> Void) {
        let row = currentLayerRow(host), rule = host.CurrentRule()
        guard row >= 0, rule >= 0 else { return }
        let name = NSTextField(string: String(host.RuleName(row, rule)))
        name.frame = NSRect(x: 0, y: 0, width: 260, height: 24)
        ask(window: window, message: "Rename Rule", accessory: name, focus: name) {
            report(host.RenameRule(row, rule, std.string(name.stringValue)), window: window)
            done()
        }
    }

    /// Asks for the current rule's new type.
    static func changeType(host: fv.desk.DeskHost, window: NSWindow?, done: @escaping () -> Void) {
        let row = currentLayerRow(host), rule = host.CurrentRule()
        guard row >= 0, rule >= 0 else { return }
        let type = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 160, height: 26), pullsDown: false)
        type.addItems(withTitles: types(host))
        type.selectItem(withTitle: String(host.RuleType(row, rule)))
        ask(window: window, message: "Change Rule Type",
            info: "Paint and layout are reset for the new type; the colour, filter and scale range are kept.",
            accessory: type, focus: type) {
            report(host.SetRuleType(row, rule, std.string(type.titleOfSelectedItem ?? "")), window: window)
            done()
        }
    }

    private static func ask(window: NSWindow?, message: String, info: String = "", accessory: NSView,
                            focus: NSView, then ok: @escaping () -> Void) {
        let alert = NSAlert()
        alert.messageText = message
        alert.informativeText = info
        alert.accessoryView = accessory
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")
        alert.window.initialFirstResponder = focus
        let handle: (NSApplication.ModalResponse) -> Void = { if $0 == .alertFirstButtonReturn { ok() } }
        if let window { alert.beginSheetModal(for: window, completionHandler: handle) } else { handle(alert.runModal()) }
    }

    private static func report(_ error: std.string, window: NSWindow?) {
        let text = String(error)
        guard !text.isEmpty else { return }
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = text
        if let window { alert.beginSheetModal(for: window) } else { alert.runModal() }
    }
}

// MARK: Rule filter

/// Edit Filter… on a style rule: the query builder over `DeskHost::kQueryRule`,
/// with Apply, which writes the rule's filter and keeps the window open.
@MainActor
final class RuleFilterWindowController: NSWindowController, NSWindowDelegate {
    let host: fv.desk.DeskHost
    var onClose: (() -> Void)?

    private let builder: QueryBuilderPanel
    private let messageLabel = NSTextField(wrappingLabelWithString: "")

    init(host: fv.desk.DeskHost) {
        self.host = host
        builder = QueryBuilderPanel(host: host, which: QueryBuilderPanel.rule)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 620, height: 420),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: true)
        window.minSize = NSSize(width: 480, height: 320)
        super.init(window: window)
        window.delegate = self
        builder.onReport = { [weak self] in self?.show(message: $0) }
        builder.view.translatesAutoresizingMaskIntoConstraints = false
        messageLabel.isHidden = true
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        let apply = NSButton(title: "Apply", target: self, action: #selector(applyFilter))
        apply.keyEquivalent = "\r"
        let buttons = NSStackView(views: [Self.spacer(), close, apply])
        buttons.orientation = .horizontal
        window.contentView = Self.column([builder.view, messageLabel, buttons], fill: [builder.view, messageLabel, buttons])
        builder.view.setContentHuggingPriority(.defaultLow, for: .vertical)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    func reload() {
        window?.title = String(host.RuleFilterTitle())
        builder.reload()
        show(message: "")
    }

    /// False when the rule's layer has closed.
    func refresh() -> Bool { host.QueryOpen(QueryBuilderPanel.rule) }

    private func show(message: String, isError: Bool = true) {
        messageLabel.stringValue = message
        messageLabel.textColor = isError ? .systemRed : .secondaryLabelColor
        messageLabel.isHidden = message.isEmpty
    }

    @objc private func applyFilter() {
        builder.commitEditing()
        let error = String(host.ApplyRuleFilter())
        show(message: error.isEmpty ? "Filter applied" : error, isError: !error.isEmpty)
    }

    @objc private func closeWindow() { close() }

    func windowWillClose(_ notification: Notification) {
        host.CloseRuleFilter()
        onClose?()
    }

    static func spacer() -> NSView {
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        return spacer
    }

    /// A content view holding `views` top to bottom, those in `fill` as wide as the window.
    static func column(_ views: [NSView], fill: [NSView]) -> NSView {
        let stack = NSStackView(views: views)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 16, bottom: 12, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView()
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
        ] + fill.map { $0.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32) })
        return content
    }
}

// MARK: Rule style

/// Edit Style… on a style rule: one tab per page of the rule's type plus a
/// General tab (legend title, zoom range). Each property is a row with a mode
/// pop-up (default, constant, by field value, by class, by zoom, expression)
/// and the editor that mode needs. Edits go to `DeskHost`'s draft as they are
/// made; Apply writes the draft to the rule as one undoable edit.
@MainActor
final class RuleStyleWindowController: NSWindowController, NSWindowDelegate, NSTabViewDelegate {
    let host: fv.desk.DeskHost
    var onClose: (() -> Void)?

    private let tabs = NSTabView()
    private let messageLabel = NSTextField(wrappingLabelWithString: "")
    /// Page titles in tab order, kept so a rebuild returns to the same tab.
    private var pageTitles: [String] = []

    /// `fv::desk::RuleValueMode`, which Swift does not import.
    private static let modeTitles = ["Default", "Constant", "Varies by field value", "Varies by class",
                                     "Varies by zoom", "Expression"]
    private enum Kind: Int32 { case color = 0, number, choice, text, field, dash, offset }
    private enum Mode: Int32 { case standard = 0, constant, byField, byClass, byZoom, expression }

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 640, height: 520),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: true)
        window.minSize = NSSize(width: 520, height: 380)
        super.init(window: window)
        window.delegate = self
        NSColorPanel.shared.showsAlpha = true
        messageLabel.isHidden = true
        let revert = NSButton(title: "Revert", target: self, action: #selector(revertDraft))
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.keyEquivalent = "\u{1b}"
        let apply = NSButton(title: "Apply", target: self, action: #selector(applyDraft))
        // Return commits the field being edited; Command-Return applies.
        apply.keyEquivalent = "\r"
        apply.keyEquivalentModifierMask = .command
        let buttons = NSStackView(views: [revert, RuleFilterWindowController.spacer(), close, apply])
        buttons.orientation = .horizontal
        tabs.delegate = self
        window.contentView = RuleFilterWindowController.column([tabs, messageLabel, buttons],
                                                              fill: [tabs, messageLabel, buttons])
        tabs.setContentHuggingPriority(.defaultLow, for: .vertical)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Rebuilds every page from the draft, staying on the current tab.
    func reload() {
        window?.title = String(host.RuleStyleTitle())
        let current = tabs.selectedTabViewItem?.label
        while let item = tabs.tabViewItems.first { tabs.removeTabViewItem(item) }
        pageTitles = []
        var byPage: [String: [Int32]] = [:]
        for k in 0..<host.RuleStylePropertyCount() {
            let page = String(host.RuleStylePropertyPage(k))
            if byPage[page] == nil { pageTitles.append(page) }
            byPage[page, default: []].append(k)
        }
        for page in pageTitles { addTab(page, rows: byPage[page]!.map(propertyRow)) }
        addTab("General", rows: generalRows())
        if let current, let item = tabs.tabViewItems.first(where: { $0.label == current }) {
            tabs.selectTabViewItem(item)
        }
    }

    /// False when the rule style pages have closed (their rule or layer is gone).
    func refresh() -> Bool { host.RuleStyleOpen() }

    private func addTab(_ title: String, rows: [NSView]) {
        let stack = NSStackView(views: rows)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        let scroll = NSScrollView()
        scroll.documentView = FlippedStackHost(stack: stack)
        scroll.documentView?.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView?.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor).isActive = true
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        let item = NSTabViewItem(identifier: title)
        item.label = title
        item.view = scroll
        tabs.addTabViewItem(item)
    }

    private func show(message: String, isError: Bool = true) {
        messageLabel.stringValue = message
        messageLabel.textColor = isError ? .systemRed : .secondaryLabelColor
        messageLabel.isHidden = message.isEmpty
    }

    /// Shows `error` and re-reads the pages; true when it is empty.
    @discardableResult
    private func finish(_ error: std.string, rebuild: Bool = true) -> Bool {
        let text = String(error)
        show(message: text)
        if !text.isEmpty { NSSound.beep() }
        // A refused edit is rebuilt too, so the control shows the value the draft kept.
        if rebuild || !text.isEmpty { DispatchQueue.main.async { [weak self] in self?.reload() } }
        return text.isEmpty
    }

    // MARK: General page

    private func generalRows() -> [NSView] {
        let legend = NSTextField(string: String(host.RuleStyleLegend()))
        legend.placeholderString = "the rule's name"
        legend.widthAnchor.constraint(equalToConstant: 240).isActive = true
        let minZoom = numberField(host.RuleStyleMinZoom())
        let maxZoom = numberField(host.RuleStyleMaxZoom())
        bind(legend) { [weak self] text in
            self?.host.SetRuleStyleLegend(std.string(text))
            return std.string("")
        }
        let range: (String) -> std.string = { [weak self] _ in
            guard let self else { return std.string("") }
            return self.host.SetRuleStyleZoomRange(minZoom.doubleValue, maxZoom.doubleValue)
        }
        bind(minZoom, commit: range)
        bind(maxZoom, commit: range)
        let note = NSTextField(wrappingLabelWithString:
            "The rule draws from the minimum zoom up to, not including, the maximum (0–24).")
        note.textColor = .secondaryLabelColor
        return [labelled("Legend title", legend), labelled("Minimum zoom", minZoom),
                labelled("Maximum zoom", maxZoom), note]
    }

    // MARK: Property rows

    private func propertyRow(_ k: Int32) -> NSView {
        let mode = NSPopUpButton(frame: .zero, pullsDown: false)
        let current = host.RuleStyleMode(k)
        for i in 0..<host.RuleStyleModeCount(k) {
            let m = host.RuleStyleModeAt(k, i)
            mode.addItem(withTitle: Self.modeTitles[Int(m)])
            mode.lastItem?.tag = Int(m)
            if m == current { mode.select(mode.lastItem) }
        }
        mode.tag = Int(k)
        mode.target = self
        mode.action = #selector(modeChanged(_:))
        let title = NSTextField(labelWithString: String(host.RuleStylePropertyTitle(k)))
        title.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
        title.toolTip = String(host.RuleStylePropertyKey(k))
        title.widthAnchor.constraint(equalToConstant: 120).isActive = true
        let header = NSStackView(views: [title, mode])
        header.orientation = .horizontal
        var views: [NSView] = [header]
        switch Mode(rawValue: current) ?? .standard {
        case .standard:
            break
        case .constant:
            views.append(indented(valueEditor(k, String(host.RuleStyleConstant(k))) { [weak self] in
                self?.host.SetRuleStyleConstant(k, std.string($0)) ?? std.string("")
            }))
        case .byField, .byClass:
            views.append(contentsOf: tableRows(k, byField: current == Mode.byField.rawValue))
        case .byZoom:
            views.append(contentsOf: tableRows(k, byField: false))
        case .expression:
            views.append(expressionRow(k))
        }
        let row = NSStackView(views: views)
        row.orientation = .vertical
        row.alignment = .leading
        row.spacing = 6
        return row
    }

    /// The field pop-up, the table of key and value rows, and the value for the rest.
    private func tableRows(_ k: Int32, byField: Bool) -> [NSView] {
        let mode = Mode(rawValue: host.RuleStyleMode(k)) ?? .byZoom
        var views: [NSView] = []
        if mode != .byZoom {
            let numeric = mode == .byClass
            let field = NSPopUpButton(frame: .zero, pullsDown: false)
            field.addItem(withTitle: "Choose a field")
            field.lastItem?.isEnabled = false
            for i in 0..<host.RuleStyleFieldCount(numeric) {
                field.addItem(withTitle: String(host.RuleStyleFieldName(numeric, i)))
            }
            field.autoenablesItems = false
            let chosen = String(host.RuleStyleField(k))
            if chosen.isEmpty { field.selectItem(at: 0) } else { field.selectItem(withTitle: chosen) }
            field.tag = Int(k)
            field.target = self
            field.action = #selector(fieldChanged(_:))
            var line: [NSView] = [NSTextField(labelWithString: "Field"), field]
            if byField {
                let fill = NSButton(title: "Fill Values", target: self, action: #selector(fillValues(_:)))
                fill.tag = Int(k)
                fill.toolTip = "One row per distinct value of the field"
                line.append(fill)
            }
            views.append(indented(row(line)))
        }
        let keyTitle = mode == .byField ? "Value" : mode == .byClass ? "From" : "Zoom"
        for i in 0..<host.RuleStyleStopCount(k) {
            let key = NSTextField(string: String(host.RuleStyleStopKey(k, i)))
            key.widthAnchor.constraint(equalToConstant: mode == .byField ? 140 : 60).isActive = true
            let value = String(host.RuleStyleStopValue(k, i))
            let editor = valueEditor(k, value) { [weak self] text in
                guard let self else { return std.string("") }
                return self.host.SetRuleStyleStop(k, i, self.host.RuleStyleStopKey(k, i), std.string(text))
            }
            bind(key) { [weak self] text in
                guard let self else { return std.string("") }
                return self.host.SetRuleStyleStop(k, i, std.string(text), self.host.RuleStyleStopValue(k, i))
            }
            let remove = NSButton(title: "\u{2212}", target: self, action: #selector(removeStop(_:)))
            remove.tag = Int(k) << 16 | Int(i)
            views.append(indented(row([NSTextField(labelWithString: keyTitle), key, editor, remove])))
        }
        let add = NSButton(title: mode == .byZoom ? "Add Zoom Level" : mode == .byClass ? "Add Class" : "Add Value",
                           target: self, action: #selector(addStop(_:)))
        add.tag = Int(k)
        views.append(indented(add))
        if mode != .byZoom {
            let otherTitle = mode == .byField ? "Other values" : "Below the first class"
            views.append(indented(row([NSTextField(labelWithString: otherTitle),
                                       valueEditor(k, String(host.RuleStyleConstant(k))) { [weak self] in
                                           self?.host.SetRuleStyleConstant(k, std.string($0)) ?? std.string("")
                                       }])))
        }
        return views
    }

    private func expressionRow(_ k: Int32) -> NSView {
        let text = NSTextField(string: String(host.RuleStyleExpression(k)))
        text.font = .monospacedSystemFont(ofSize: NSFont.smallSystemFontSize, weight: .regular)
        text.widthAnchor.constraint(greaterThanOrEqualToConstant: 380).isActive = true
        text.placeholderString = "[\"interpolate\", [\"linear\"], [\"zoom\"], 10, 1, 16, 4]"
        bind(text) { [weak self] in self?.host.SetRuleStyleExpression(k, std.string($0)) ?? std.string("") }
        let verify = NSButton(title: "Verify", target: nil, action: nil)
        let result = NSTextField(labelWithString: "")
        ControlAction.attach(to: verify) { [weak self] _ in
            guard let self else { return }
            let error = String(self.host.VerifyRuleStyleExpression(k, std.string(text.stringValue)))
            result.stringValue = error.isEmpty ? "The expression evaluates." : error
            result.textColor = error.isEmpty ? .secondaryLabelColor : .systemRed
        }
        return indented(NSStackView(views: [row([text, verify]), result]).vertical())
    }

    /// The editor for one constant of property `k`, committing through `commit`.
    private func valueEditor(_ k: Int32, _ value: String, commit: @escaping (String) -> std.string) -> NSView {
        if host.RuleStylePropertyIcons(k) { return iconEditor(value, commit: commit) }
        switch Kind(rawValue: host.RuleStylePropertyKind(k)) ?? .text {
        case .color:
            let well = NSColorWell(style: .minimal)
            well.color = Self.color(fv.desk.DeskHost.RuleColorRGBA(std.string(value), 0x0000_00ff))
            well.widthAnchor.constraint(equalToConstant: 44).isActive = true
            well.toolTip = value
            ControlAction.attach(to: well) { [weak self] control in
                guard let well = control as? NSColorWell else { return }
                let text = String(fv.desk.DeskHost.RuleColorText(Self.rgba(well.color)))
                well.toolTip = text
                self?.finish(commit(text), rebuild: false)
            }
            return well
        case .choice, .field:
            let popup = NSPopUpButton(frame: .zero, pullsDown: false)
            popup.addItem(withTitle: "Choose")
            let isField = Kind(rawValue: host.RuleStylePropertyKind(k)) == .field
            let count = isField ? host.RuleStyleFieldCount(false) : host.RuleStylePropertyChoiceCount(k)
            for i in 0..<count {
                popup.addItem(withTitle: String(isField ? host.RuleStyleFieldName(false, i)
                                                        : host.RuleStylePropertyChoice(k, i)))
            }
            if !value.isEmpty { popup.selectItem(withTitle: value) }
            ControlAction.attach(to: popup) { [weak self] control in
                guard let popup = control as? NSPopUpButton, popup.indexOfSelectedItem > 0 else { return }
                self?.finish(commit(popup.titleOfSelectedItem ?? ""), rebuild: false)
            }
            return popup
        case .number, .text, .dash, .offset:
            let field = NSTextField(string: value)
            field.widthAnchor.constraint(equalToConstant: 100).isActive = true
            let kind = Kind(rawValue: host.RuleStylePropertyKind(k))
            if kind == .number {
                field.toolTip = "\(host.RuleStylePropertyMin(k)) to \(host.RuleStylePropertyMax(k))"
            } else if kind == .offset {
                field.placeholderString = "0 0"
                field.toolTip = "Across and down from the anchor, in ems (multiples of the text size)"
            } else if kind == .dash {
                field.placeholderString = "4 2"
                field.toolTip = "Dash and gap lengths in line widths, separated by spaces"
            } else {
                field.placeholderString = "sprite name"
            }
            bind(field, commit: commit)
            return field
        }
    }

    /// A button showing the chosen icon and its name; it opens the icon grid.
    private func iconEditor(_ value: String, commit: @escaping (String) -> std.string) -> NSView {
        let button = NSButton(title: value.isEmpty ? "Choose Icon" : value, target: nil, action: nil)
        button.image = IconPickerController.image(host, value, points: 16)
        button.imagePosition = .imageLeading
        button.toolTip = "Choose an icon"
        ControlAction.attach(to: button) { [weak self] control in
            guard let self else { return }
            let popover = NSPopover()
            popover.behavior = .transient
            popover.contentViewController = IconPickerController(host: self.host, selected: value) {
                [weak self, weak popover] name in
                popover?.close()
                self?.finish(commit(name))
            }
            popover.show(relativeTo: control.bounds, of: control, preferredEdge: .maxY)
        }
        return button
    }

    private func numberField(_ value: Double) -> NSTextField {
        let field = NSTextField(string: String(format: "%g", value))
        field.widthAnchor.constraint(equalToConstant: 60).isActive = true
        return field
    }

    /// Commits `field`'s text when editing ends; a refusal rebuilds the page.
    private func bind(_ field: NSTextField, rebuild: Bool = false, commit: @escaping (String) -> std.string) {
        field.cell?.sendsActionOnEndEditing = true
        ControlAction.attach(to: field) { [weak self] control in
            self?.finish(commit(control.stringValue), rebuild: rebuild)
        }
    }

    private func labelled(_ title: String, _ view: NSView) -> NSView {
        let label = NSTextField(labelWithString: title)
        label.widthAnchor.constraint(equalToConstant: 120).isActive = true
        return row([label, view])
    }

    private func row(_ views: [NSView]) -> NSStackView {
        let stack = NSStackView(views: views)
        stack.orientation = .horizontal
        stack.spacing = 6
        return stack
    }

    private func indented(_ view: NSView) -> NSView {
        let pad = NSView()
        pad.widthAnchor.constraint(equalToConstant: 24).isActive = true
        return row([pad, view])
    }

    // MARK: Actions

    @objc private func modeChanged(_ sender: NSPopUpButton) {
        finish(host.SetRuleStyleMode(Int32(sender.tag), Int32(sender.selectedTag())))
    }

    @objc private func fieldChanged(_ sender: NSPopUpButton) {
        finish(host.SetRuleStyleField(Int32(sender.tag), std.string(sender.titleOfSelectedItem ?? "")))
    }

    @objc private func fillValues(_ sender: NSButton) { finish(host.FillRuleStyleValues(Int32(sender.tag))) }
    @objc private func addStop(_ sender: NSButton) { finish(host.AddRuleStyleStop(Int32(sender.tag))) }

    @objc private func removeStop(_ sender: NSButton) {
        finish(host.RemoveRuleStyleStop(Int32(sender.tag >> 16), Int32(sender.tag & 0xffff)))
    }

    @objc private func applyDraft() {
        window?.makeFirstResponder(nil)  // commits the field being edited
        let error = String(host.ApplyRuleStyle())
        show(message: error.isEmpty ? "Style applied" : error, isError: !error.isEmpty)
        if error.isEmpty { reload() }
    }

    @objc private func revertDraft() {
        finish(host.RevertRuleStyle())
    }

    @objc private func closeWindow() { close() }

    func windowWillClose(_ notification: Notification) {
        host.CloseRuleStyle()
        onClose?()
    }

    private static func color(_ rgba: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((rgba >> 24) & 0xff) / 255, green: CGFloat((rgba >> 16) & 0xff) / 255,
                blue: CGFloat((rgba >> 8) & 0xff) / 255, alpha: CGFloat(rgba & 0xff) / 255)
    }

    private static func rgba(_ color: NSColor) -> UInt32 {
        let c = color.usingColorSpace(.sRGB) ?? .black
        let byte = { (v: CGFloat) in UInt32(max(0, min(255, (v * 255).rounded()))) }
        return byte(c.redComponent) << 24 | byte(c.greenComponent) << 16 | byte(c.blueComponent) << 8 |
            byte(c.alphaComponent)
    }
}

// MARK: Icon picker

/// A searchable grid of the builtin icons; choosing one calls `onChoose` with its name.
@MainActor
final class IconPickerController: NSViewController, NSSearchFieldDelegate {
    private static let columns = 10
    private static let cell: CGFloat = 32

    private let host: fv.desk.DeskHost
    private let selected: String
    private let onChoose: (String) -> Void
    private let names: [String]
    private let search = NSSearchField()
    private let grid = NSStackView()

    init(host: fv.desk.DeskHost, selected: String, onChoose: @escaping (String) -> Void) {
        self.host = host
        self.selected = selected
        self.onChoose = onChoose
        names = (0..<host.RuleIconCount()).map { String(host.RuleIconName($0)) }
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    override func loadView() {
        search.placeholderString = "Search icons"
        search.delegate = self
        grid.orientation = .vertical
        grid.alignment = .leading
        grid.spacing = 2
        let scroll = NSScrollView()
        scroll.documentView = FlippedStackHost(stack: grid)
        scroll.documentView?.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView?.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor).isActive = true
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        let width = CGFloat(Self.columns) * (Self.cell + 2) + 20
        scroll.widthAnchor.constraint(equalToConstant: width).isActive = true
        scroll.heightAnchor.constraint(equalToConstant: 300).isActive = true
        let column = NSStackView(views: [search, scroll])
        column.orientation = .vertical
        column.alignment = .leading
        column.edgeInsets = NSEdgeInsets(top: 10, left: 10, bottom: 10, right: 10)
        search.widthAnchor.constraint(equalTo: scroll.widthAnchor).isActive = true
        view = column
        fill("")
    }

    func controlTextDidChange(_ obj: Notification) { fill(search.stringValue) }

    /// Lays out the icons whose names contain `filter`, `columns` to a row.
    private func fill(_ filter: String) {
        for v in grid.arrangedSubviews { v.removeFromSuperview() }
        let shown = filter.isEmpty ? names : names.filter { $0.localizedCaseInsensitiveContains(filter) }
        var row: [NSView] = []
        for name in shown {
            let button = NSButton(image: Self.image(host, name, points: 20) ?? NSImage(), target: nil, action: nil)
            button.bezelStyle = .smallSquare
            button.setButtonType(.pushOnPushOff)
            button.state = name == selected ? .on : .off
            button.toolTip = name
            button.widthAnchor.constraint(equalToConstant: Self.cell).isActive = true
            button.heightAnchor.constraint(equalToConstant: Self.cell).isActive = true
            ControlAction.attach(to: button) { [weak self] _ in self?.onChoose(name) }
            row.append(button)
            if row.count == Self.columns { grid.addArrangedSubview(Self.line(row)); row = [] }
        }
        if !row.isEmpty { grid.addArrangedSubview(Self.line(row)) }
        if shown.isEmpty { grid.addArrangedSubview(NSTextField(labelWithString: "No icon matches.")) }
    }

    private static func line(_ views: [NSView]) -> NSStackView {
        let stack = NSStackView(views: views)
        stack.orientation = .horizontal
        stack.spacing = 2
        return stack
    }

    /// Icon `name` as a template image `points` square, so it follows the label colour.
    static func image(_ host: fv.desk.DeskHost, _ name: String, points: CGFloat) -> NSImage? {
        guard !name.isEmpty else { return nil }
        let scale = NSScreen.main?.backingScaleFactor ?? 2
        let size = Int((points * scale).rounded())
        var bytes = Data(count: size * size * 4)
        let copied = bytes.withUnsafeMutableBytes { buf in
            host.CopyRuleIcon(std.string(name), Int32(size), 0x0000_00ff,
                              buf.baseAddress!.assumingMemoryBound(to: UInt8.self), buf.count)
        }
        guard copied, let provider = CGDataProvider(data: bytes as CFData),
              let space = CGColorSpace(name: CGColorSpace.sRGB),
              let cg = CGImage(width: size, height: size, bitsPerComponent: 8, bitsPerPixel: 32,
                               bytesPerRow: size * 4, space: space,
                               bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                               provider: provider, decode: nil, shouldInterpolate: true,
                               intent: .defaultIntent) else { return nil }
        let image = NSImage(cgImage: cg, size: NSSize(width: points, height: points))
        image.isTemplate = true
        return image
    }
}

/// A control's action as a closure; the control keeps it alive.
@MainActor
final class ControlAction: NSObject {
    private let run: (NSControl) -> Void
    private static var key = 0

    private init(_ run: @escaping (NSControl) -> Void) { self.run = run }

    static func attach(to control: NSControl, _ run: @escaping (NSControl) -> Void) {
        let action = ControlAction(run)
        control.target = action
        control.action = #selector(fire(_:))
        objc_setAssociatedObject(control, &key, action, .OBJC_ASSOCIATION_RETAIN)
    }

    @objc private func fire(_ sender: NSControl) { run(sender) }
}

private extension NSStackView {
    func vertical() -> NSStackView {
        orientation = .vertical
        alignment = .leading
        spacing = 4
        return self
    }
}
