// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The layer manager, docked at the right of the map window, in two views. Sources: each data source with its
/// layers under it, a tri-state check box per source that shows or hides all
/// its layers, and Move Up/Down and dragging that move a source, or a layer
/// within its source. Layers: the overlay stack top first, ungrouped, each row
/// with a visibility check box, its name and, where the layer has one, an
/// opacity slider. A feature layer in the Sources view discloses its style
/// rules, top-drawn first, each with a check box; rules move within their
/// layer. Selecting a row makes it current; a right click shows DeskKit's
/// source, layer or rule menu. Rows come from `DeskHost`'s layer snapshot.
@MainActor
final class LayersPanelController: NSViewController, NSTableViewDataSource,
    NSTableViewDelegate, NSOutlineViewDataSource, NSOutlineViewDelegate, NSMenuDelegate {
    let host: fv.desk.DeskHost
    /// Runs a command chosen from a row's menu.
    let execute: (String) -> Void

    /// A source in the Sources view; `key` names it across reloads.
    private final class SourceNode {
        let source: Int32
        let key: String
        var layers: [LayerNode] = []
        init(source: Int32, key: String) { self.source = source; self.key = key }
    }

    /// A layer of a source: its layer row and its place in the source.
    private final class LayerNode {
        let source: SourceNode
        let row: Int32
        let index: Int
        var rules: [RuleNode] = []
        init(source: SourceNode, row: Int32, index: Int) { self.source = source; self.row = row; self.index = index }
        /// Names the layer across reloads, for its disclosure state.
        var key: String { "\(source.key)|\(index)" }
    }

    /// A style rule of a layer: its row among the layer's rules, top-drawn first.
    private final class RuleNode {
        let layer: LayerNode
        let rule: Int32
        init(layer: LayerNode, rule: Int32) { self.layer = layer; self.rule = rule }
    }

    private let viewSwitch = NSSegmentedControl(labels: ["Sources", "Layers"], trackingMode: .selectOne,
                                                target: nil, action: nil)
    private let pages = NSTabView()
    private let outline = NSOutlineView()
    private let table = NSTableView()
    private let upButton = NSButton(title: "Move Up", target: nil, action: nil)
    private let downButton = NSButton(title: "Move Down", target: nil, action: nil)
    private static let rowType = NSPasteboard.PasteboardType("org.peregrine.layer-row")
    private static let sourceType = NSPasteboard.PasteboardType("org.peregrine.source-entry")
    private var sources: [SourceNode] = []
    /// Keys of the sources whose layers, and layers whose rules, are folded away.
    private var collapsed: Set<String> = []
    /// The Sources row to keep selected across a reload: a source key, and a
    /// layer index within it or -1 for the source itself, and a rule of that layer or -1.
    private var selected: (key: String, index: Int, rule: Int32)?
    /// Set while the views are being reloaded, so the selection they restore is not a click.
    private var reloading = false
    private var showingSources: Bool { viewSwitch.selectedSegment == 0 }

    init(host: fv.desk.DeskHost, execute: @escaping (String) -> Void) {
        self.host = host
        self.execute = execute
        super.init(nibName: nil, bundle: nil)
        view = buildLayout()
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads both views; the current overlay's row is selected and each view keeps its scroll position.
    func reload() {
        // Rebuilt nodes reload collapsed, which clamps the outline to the top before it re-expands.
        let clips = [table, outline].compactMap { $0.enclosingScrollView?.contentView }
        let origins = clips.map { $0.bounds.origin }
        defer {
            for (clip, origin) in zip(clips, origins) {
                clip.scroll(to: clip.constrainBoundsRect(NSRect(origin: origin, size: clip.bounds.size)).origin)
                clip.enclosingScrollView?.reflectScrolledClipView(clip)
            }
        }
        reloading = true
        table.reloadData()
        let count = Int(host.LayerCount())
        if let current = (0..<count).first(where: { host.LayerCurrent(Int32($0)) }) {
            table.selectRowIndexes(IndexSet(integer: current), byExtendingSelection: false)
        } else {
            table.deselectAll(nil)
        }
        reloadSources()
        reloading = false
        updateButtons()
    }

    private func reloadSources() {
        sources = (0..<host.SourceCount()).map { s in
            let node = SourceNode(source: s, key: sourceKey(s))
            node.layers = (0..<host.SourceLayerCount(s)).map {
                let layer = LayerNode(source: node, row: host.SourceLayerRow(s, $0), index: Int($0))
                layer.rules = (0..<host.RuleCount(layer.row)).map { RuleNode(layer: layer, rule: $0) }
                return layer
            }
            return node
        }
        outline.reloadData()
        for node in sources where !collapsed.contains(node.key) {
            outline.expandItem(node)
            for layer in node.layers where !layer.rules.isEmpty && !collapsed.contains(layer.key) {
                outline.expandItem(layer)
            }
        }
        var keep: Any?
        var current: Any?
        let currentRule = host.CurrentRule()
        for node in sources {
            if let selected, selected.key == node.key, selected.index < 0 { keep = node }
            if host.SourceCurrent(node.source) && current == nil { current = node }
            for layer in node.layers {
                if let selected, selected.key == node.key, selected.index == layer.index {
                    keep = selected.rule >= 0 ? layer.rules.first { $0.rule == selected.rule } ?? layer : layer
                }
                if host.LayerCurrent(layer.row) && outline.row(forItem: layer) >= 0 {
                    current = layer
                    if let rule = layer.rules.first(where: { $0.rule == currentRule }),
                       outline.row(forItem: rule) >= 0 { current = rule }
                }
            }
        }
        let row = outline.row(forItem: keep ?? current)
        if row >= 0 {
            outline.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        } else {
            outline.deselectAll(nil)
        }
    }

    /// A source's identity across reloads: its path, or its name without a file.
    private func sourceKey(_ source: Int32) -> String {
        let path = String(host.SourcePath(source))
        return path.isEmpty ? String(host.SourceName(source)) : path
    }

    // MARK: Layout

    private func buildLayout() -> NSView {
        viewSwitch.selectedSegment = 0
        viewSwitch.target = self
        viewSwitch.action = #selector(switchView)

        let sourcesColumn = NSTableColumn(identifier: .init("source"))
        sourcesColumn.resizingMask = .autoresizingMask
        outline.addTableColumn(sourcesColumn)
        outline.outlineTableColumn = sourcesColumn
        outline.headerView = nil
        outline.rowHeight = 24
        outline.allowsMultipleSelection = false
        outline.dataSource = self
        outline.delegate = self
        outline.registerForDraggedTypes([Self.sourceType])
        outline.draggingDestinationFeedbackStyle = .gap
        outline.menu = rowMenu()

        for (id, width) in [("visible", 24.0), ("name", 180.0), ("opacity", 110.0)] {
            let column = NSTableColumn(identifier: .init(id))
            column.width = width
            column.resizingMask = id == "name" ? .autoresizingMask : []
            table.addTableColumn(column)
        }
        table.headerView = nil
        table.rowHeight = 24
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        table.registerForDraggedTypes([Self.rowType])
        table.draggingDestinationFeedbackStyle = .gap
        table.menu = rowMenu()

        pages.tabViewType = .noTabsNoBorder
        for (id, view) in [("sources", outline), ("layers", table)] as [(String, NSView)] {
            let scroll = NSScrollView()
            scroll.documentView = view
            scroll.hasVerticalScroller = true
            scroll.borderType = .bezelBorder
            let item = NSTabViewItem(identifier: id)
            item.view = scroll
            pages.addTabViewItem(item)
        }

        upButton.target = self
        upButton.action = #selector(moveSelectedUp)
        downButton.target = self
        downButton.action = #selector(moveSelectedDown)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [upButton, downButton, spacer])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [viewSwitch, pages, buttons])
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        stack.translatesAutoresizingMaskIntoConstraints = false
        let content = NSView(frame: NSRect(x: 0, y: 0, width: 340, height: 400))
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            content.widthAnchor.constraint(greaterThanOrEqualToConstant: 260),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            pages.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -24),
        ])
        pages.setContentHuggingPriority(.defaultLow, for: .vertical)
        return content
    }

    private func rowMenu() -> NSMenu {
        let menu = NSMenu()
        menu.autoenablesItems = false
        menu.delegate = self
        return menu
    }

    @objc private func switchView() {
        pages.selectTabViewItem(at: viewSwitch.selectedSegment)
        updateButtons()
    }

    private func updateButtons() {
        if showingSources {
            switch outline.item(atRow: outline.selectedRow) {
            case let node as SourceNode:
                let i = sources.firstIndex { $0 === node } ?? -1
                upButton.isEnabled = i > 0
                downButton.isEnabled = i >= 0 && i + 1 < sources.count
            case let layer as LayerNode:
                upButton.isEnabled = layer.index > 0
                downButton.isEnabled = layer.index + 1 < layer.source.layers.count
            case let rule as RuleNode:
                upButton.isEnabled = rule.rule > 0
                downButton.isEnabled = Int(rule.rule) + 1 < rule.layer.rules.count
            default:
                upButton.isEnabled = false
                downButton.isEnabled = false
            }
        } else {
            let row = table.selectedRow
            upButton.isEnabled = row > 0
            downButton.isEnabled = row >= 0 && row + 1 < Int(host.LayerCount())
        }
    }

    // MARK: Actions

    /// Reports `error` with a beep and re-reads the views; true when it is empty.
    @discardableResult
    private func finish(_ error: std.string) -> Bool {
        let failed = !String(error).isEmpty
        if failed { NSSound.beep() }
        reload()
        return !failed
    }

    private func move(from: Int, to: Int) {
        finish(host.MoveLayer(Int32(from), Int32(to)))
    }

    /// Moves `rule` to row `to` among its layer's rules.
    @discardableResult
    private func move(rule: RuleNode, to: Int) -> Bool {
        selected = (rule.layer.source.key, rule.layer.index, Int32(to))
        return finish(host.MoveRule(rule.layer.row, rule.rule, Int32(to)))
    }

    /// Moves source `node` to place `to` among the sources.
    @discardableResult
    private func move(source node: SourceNode, to: Int) -> Bool {
        selected = (node.key, -1, -1)
        return finish(host.MoveSource(node.source, Int32(to)))
    }

    /// Moves `layer` to place `to` within its source.
    @discardableResult
    private func move(layer: LayerNode, to: Int) -> Bool {
        selected = (layer.source.key, to, -1)
        return finish(host.MoveLayer(layer.row, host.SourceLayerRow(layer.source.source, Int32(to))))
    }

    private func moveSelected(by step: Int) {
        if showingSources {
            switch outline.item(atRow: outline.selectedRow) {
            case let node as SourceNode:
                guard let i = sources.firstIndex(where: { $0 === node }),
                      sources.indices.contains(i + step) else { return }
                move(source: node, to: i + step)
            case let layer as LayerNode:
                guard layer.source.layers.indices.contains(layer.index + step) else { return }
                move(layer: layer, to: layer.index + step)
            case let rule as RuleNode:
                guard rule.layer.rules.indices.contains(Int(rule.rule) + step) else { return }
                move(rule: rule, to: Int(rule.rule) + step)
            default:
                return
            }
        } else {
            let row = table.selectedRow
            if row >= 0 && row + step >= 0 { move(from: row, to: row + step) }
        }
    }

    @objc private func moveSelectedUp() { moveSelected(by: -1) }

    @objc private func moveSelectedDown() { moveSelected(by: 1) }

    @objc private func toggleVisible(_ sender: NSButton) {
        let row = sender.tag
        guard row >= 0 else { return }
        _ = host.SetLayerVisible(Int32(row), sender.state == .on)
        // The source's check box follows its layers.
        reload()
    }

    @objc private func toggleSourceVisible(_ sender: NSButton) {
        let source = Int32(sender.tag)
        // A mixed or hidden source shows every layer; a fully shown one hides them.
        finish(host.SetSourceVisible(source, host.SourceShown(source) != 2))
    }

    @objc private func toggleRuleShown(_ sender: NSButton) {
        selected = nil
        finish(host.SetRuleShown(Int32(sender.tag >> 16), Int32(sender.tag & 0xffff), sender.state == .on))
    }

    @objc private func opacityChanged(_ sender: NSSlider) {
        let row = sender.tag
        guard row >= 0 else { return }
        _ = host.SetLayerOpacity(Int32(row), Int32(sender.integerValue))
        sender.toolTip = "Opacity \(sender.integerValue)%"
    }

    // MARK: Row menus

    /// Fills the clicked row's menu: the row is made current first, since the
    /// menu's commands act on the current layer and its source.
    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        let isSource: Bool
        var isRule = false
        if menu === outline.menu {
            let row = outline.clickedRow
            guard row >= 0 else { return }
            // No reload while the menu is up: re-expanding rows shifts the outline's
            // context-menu row past the end and AppKit throws drawing its highlight.
            // menuDidClose reloads instead.
            reloading = true
            outline.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
            reloading = false
            switch outline.item(atRow: row) {
            case let node as SourceNode:
                selected = (node.key, -1, -1)
                _ = host.SelectSource(node.source)
                isSource = true
            case let layer as LayerNode:
                selected = (layer.source.key, layer.index, -1)
                _ = host.SelectLayer(layer.row)
                isSource = false
            case let rule as RuleNode:
                selected = (rule.layer.source.key, rule.layer.index, rule.rule)
                _ = host.SelectRule(rule.layer.row, rule.rule)
                isSource = false
                isRule = true
            default:
                return
            }
        } else {
            let row = table.clickedRow
            guard row >= 0 else { return }
            _ = host.SelectLayer(Int32(row))
            isSource = false
        }
        let count = isRule ? host.RuleMenuEntryCount() : isSource ? host.SourceMenuEntryCount()
            : host.LayerMenuEntryCount()
        for i in 0..<count {
            let entry = isRule ? host.RuleMenuEntryAt(i) : isSource ? host.SourceMenuEntryAt(i) : host.LayerMenuEntryAt(i)
            switch entry.kind {
            case fv.desk.kMenuSeparator:
                if menu.items.last.map({ !$0.isSeparatorItem }) ?? false { menu.addItem(.separator()) }
            case fv.desk.kMenuCommand:
                let id = String(entry.id)
                let item = NSMenuItem(title: String(entry.label), action: #selector(runMenuCommand(_:)),
                                      keyEquivalent: "")
                item.target = self
                item.representedObject = id
                item.isEnabled = host.IsEnabled(entry.id)
                menu.addItem(item)
            default:
                break
            }
        }
    }

    /// Shows the row the menu made current (its bold name, the buttons) once the menu is gone.
    func menuDidClose(_ menu: NSMenu) {
        guard menu === outline.menu else { return }
        DispatchQueue.main.async { [weak self] in self?.reload() }
    }

    @objc private func runMenuCommand(_ sender: NSMenuItem) {
        guard let id = sender.representedObject as? String else { return }
        execute(id)
    }

    // MARK: Row content

    /// A layer's visibility box, icon and name (bold when current, a bullet
    /// when unsaved) and, with `withOpacity`, its opacity slider.
    private func layerCell(_ r: Int32, withOpacity: Bool) -> NSView {
        let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleVisible(_:)))
        box.state = host.LayerVisible(r) ? .on : .off
        box.toolTip = "Show or hide"
        box.tag = Int(r)
        var views: [NSView] = [box, nameCell(name: String(host.LayerName(r)), dirty: host.LayerDirty(r),
                                             tip: String(host.LayerTypeName(r)), icon: String(host.LayerIcon(r)),
                                             bold: host.LayerCurrent(r))]
        if withOpacity, let slider = opacitySlider(r) { views.append(slider) }
        let cell = NSStackView(views: views)
        cell.orientation = .horizontal
        cell.spacing = 6
        return cell
    }

    private func nameCell(name: String, dirty: Bool, tip: String, icon: String, bold: Bool) -> NSView {
        let label = NSTextField(labelWithString: name + (dirty ? " \u{2022}" : ""))
        label.lineBreakMode = .byTruncatingTail
        label.toolTip = tip
        label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        if bold { label.font = .boldSystemFont(ofSize: NSFont.systemFontSize) }
        guard let image = CommandToolbarItem.image(for: icon, label: icon) else { return label }
        let imageView = NSImageView(image: image)
        imageView.setContentHuggingPriority(.required, for: .horizontal)
        let cell = NSStackView(views: [imageView, label])
        cell.orientation = .horizontal
        cell.spacing = 6
        return cell
    }

    private func opacitySlider(_ r: Int32) -> NSSlider? {
        guard host.LayerHasOpacity(r) else { return nil }
        let slider = NSSlider(value: Double(host.LayerOpacity(r)), minValue: 0, maxValue: 100,
                              target: self, action: #selector(opacityChanged(_:)))
        slider.controlSize = .small
        slider.isContinuous = true
        slider.tag = Int(r)
        slider.toolTip = "Opacity \(host.LayerOpacity(r))%"
        slider.widthAnchor.constraint(equalToConstant: 90).isActive = true
        return slider
    }

    /// A rule's check box, name (bold when current) and type; its filter is the tool tip.
    private func ruleCell(_ node: RuleNode) -> NSView {
        let row = node.layer.row, r = node.rule
        let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleRuleShown(_:)))
        box.state = host.RuleShown(row, r) ? .on : .off
        box.toolTip = "Show or hide this rule"
        box.tag = Int(row) << 16 | Int(r)
        let name = NSTextField(labelWithString: String(host.RuleName(row, r)))
        name.lineBreakMode = .byTruncatingTail
        name.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        if host.LayerCurrent(row) && host.CurrentRule() == r { name.font = .boldSystemFont(ofSize: NSFont.systemFontSize) }
        let filter = String(host.RuleFilter(row, r))
        name.toolTip = filter.isEmpty ? "Every feature" : filter
        let type = NSTextField(labelWithString: String(host.RuleType(row, r)) + (filter.isEmpty ? "" : " \u{2022} filtered"))
        type.textColor = .secondaryLabelColor
        let cell = NSStackView(views: [box, name, type])
        cell.orientation = .horizontal
        cell.spacing = 6
        return cell
    }

    private func sourceCell(_ node: SourceNode) -> NSView {
        let s = node.source
        let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleSourceVisible(_:)))
        box.allowsMixedState = true
        box.state = [NSControl.StateValue.off, .mixed, .on][Int(max(0, min(2, host.SourceShown(s))))]
        box.toolTip = "Show or hide every layer"
        box.tag = Int(s)
        let path = String(host.SourcePath(s))
        var views: [NSView] = [box, nameCell(name: String(host.SourceName(s)), dirty: host.SourceDirty(s),
                                             tip: path.isEmpty ? String(host.SourceTypeName(s)) : path,
                                             icon: String(host.SourceIcon(s)), bold: false)]
        let layers = host.SourceLayerCount(s)
        if layers > 1 {
            let count = NSTextField(labelWithString: "\(layers) layers")
            count.textColor = .secondaryLabelColor
            views.append(count)
        }
        let cell = NSStackView(views: views)
        cell.orientation = .horizontal
        cell.spacing = 6
        return cell
    }

    // MARK: Layers table

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.LayerCount()) }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let r = Int32(row)
        switch tableColumn?.identifier.rawValue {
        case "visible":
            let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleVisible(_:)))
            box.state = host.LayerVisible(r) ? .on : .off
            box.toolTip = "Show or hide"
            box.tag = row
            return box
        case "opacity":
            return opacitySlider(r) ?? NSView()
        default:
            return nameCell(name: String(host.LayerName(r)), dirty: host.LayerDirty(r),
                            tip: String(host.LayerTypeName(r)), icon: String(host.LayerIcon(r)),
                            bold: host.LayerCurrent(r))
        }
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        updateButtons()
        guard !reloading, table.selectedRow >= 0 else { return }
        _ = host.SelectLayer(Int32(table.selectedRow))
        reload()
    }

    func tableView(_ tableView: NSTableView, pasteboardWriterForRow row: Int) -> NSPasteboardWriting? {
        let item = NSPasteboardItem()
        item.setString(String(row), forType: Self.rowType)
        return item
    }

    func tableView(_ tableView: NSTableView, validateDrop info: NSDraggingInfo, proposedRow row: Int,
                   proposedDropOperation dropOperation: NSTableView.DropOperation) -> NSDragOperation {
        dropOperation == .above ? .move : []
    }

    func tableView(_ tableView: NSTableView, acceptDrop info: NSDraggingInfo, row: Int,
                   dropOperation: NSTableView.DropOperation) -> Bool {
        guard let text = info.draggingPasteboard.pasteboardItems?.first?.string(forType: Self.rowType),
              let from = Int(text) else { return false }
        // A drop "above row" lands at that index, or one less once the source is taken out.
        let to = from < row ? row - 1 : row
        guard to != from, to >= 0, to < Int(host.LayerCount()) else { return false }
        move(from: from, to: to)
        return true
    }

    // MARK: Sources outline

    func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
        switch item {
        case nil: return sources.count
        case let node as SourceNode: return node.layers.count
        case let layer as LayerNode: return layer.rules.count
        default: return 0
        }
    }

    func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
        if let node = item as? SourceNode { return node.layers[index] }
        if let layer = item as? LayerNode { return layer.rules[index] }
        return sources[index]
    }

    func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
        switch item {
        case let node as SourceNode: return !node.layers.isEmpty
        case let layer as LayerNode: return !layer.rules.isEmpty
        default: return false
        }
    }

    func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
        switch item {
        case let node as SourceNode: return sourceCell(node)
        case let layer as LayerNode: return layerCell(layer.row, withOpacity: true)
        case let rule as RuleNode: return ruleCell(rule)
        default: return nil
        }
    }

    func outlineViewSelectionDidChange(_ notification: Notification) {
        updateButtons()
        guard !reloading else { return }
        switch outline.item(atRow: outline.selectedRow) {
        case let node as SourceNode:
            selected = (node.key, -1, -1)
            _ = host.SelectSource(node.source)
        case let layer as LayerNode:
            selected = (layer.source.key, layer.index, -1)
            _ = host.SelectLayer(layer.row)
        case let rule as RuleNode:
            selected = (rule.layer.source.key, rule.layer.index, rule.rule)
            _ = host.SelectRule(rule.layer.row, rule.rule)
        default:
            return
        }
        reload()
    }

    /// The disclosure key of a source or layer in a disclosure notification.
    private func disclosureKey(_ notification: Notification) -> String? {
        switch notification.userInfo?["NSObject"] {
        case let node as SourceNode: return node.key
        case let layer as LayerNode: return layer.key
        default: return nil
        }
    }

    func outlineViewItemDidCollapse(_ notification: Notification) {
        guard !reloading, let key = disclosureKey(notification) else { return }
        collapsed.insert(key)
    }

    func outlineViewItemDidExpand(_ notification: Notification) {
        guard !reloading, let key = disclosureKey(notification) else { return }
        collapsed.remove(key)
    }

    /// A dragged source is written as "s<index>", a layer as "l<source>,<index>",
    /// a rule as "r<source>,<layer index>,<rule>".
    func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> NSPasteboardWriting? {
        let entry: String
        switch item {
        case let node as SourceNode:
            guard let i = sources.firstIndex(where: { $0 === node }) else { return nil }
            entry = "s\(i)"
        case let layer as LayerNode:
            guard let s = sources.firstIndex(where: { $0 === layer.source }) else { return nil }
            entry = "l\(s),\(layer.index)"
        case let rule as RuleNode:
            guard let s = sources.firstIndex(where: { $0 === rule.layer.source }) else { return nil }
            entry = "r\(s),\(rule.layer.index),\(rule.rule)"
        default:
            return nil
        }
        let pasteboardItem = NSPasteboardItem()
        pasteboardItem.setString(entry, forType: Self.sourceType)
        return pasteboardItem
    }

    /// The dragged entry: a source, a layer of a source, or a rule of a layer.
    private func draggedEntry(_ info: NSDraggingInfo) -> (source: Int, layer: Int?, rule: Int?)? {
        guard let text = info.draggingPasteboard.pasteboardItems?.first?.string(forType: Self.sourceType),
              let kind = text.first else { return nil }
        let numbers = text.dropFirst().split(separator: ",").compactMap { Int($0) }
        if kind == "s", numbers.count == 1, sources.indices.contains(numbers[0]) { return (numbers[0], nil, nil) }
        guard numbers.count >= 2, sources.indices.contains(numbers[0]),
              sources[numbers[0]].layers.indices.contains(numbers[1]) else { return nil }
        if kind == "l", numbers.count == 2 { return (numbers[0], numbers[1], nil) }
        if kind == "r", numbers.count == 3,
           sources[numbers[0]].layers[numbers[1]].rules.indices.contains(numbers[2]) {
            return (numbers[0], numbers[1], numbers[2])
        }
        return nil
    }

    /// Where a drop between rows lands: a source among the sources, a layer
    /// among its own source's layers, a rule among its own layer's rules; nil for anywhere else.
    private func dropTarget(_ info: NSDraggingInfo, item: Any?, index: Int) -> Int? {
        guard index >= 0, let entry = draggedEntry(info) else { return nil }
        let from: Int
        let count: Int
        if let rule = entry.rule, let layer = entry.layer {
            guard let target = item as? LayerNode, target === sources[entry.source].layers[layer] else { return nil }
            from = rule
            count = target.rules.count
        } else if let layer = entry.layer {
            guard let node = item as? SourceNode, node === sources[entry.source] else { return nil }
            from = layer
            count = node.layers.count
        } else {
            guard item == nil else { return nil }
            from = entry.source
            count = sources.count
        }
        // A drop above a row lands at its index, or one less once the dragged row is taken out.
        let to = from < index ? index - 1 : index
        return to != from && to >= 0 && to < count ? to : nil
    }

    func outlineView(_ outlineView: NSOutlineView, validateDrop info: NSDraggingInfo, proposedItem item: Any?,
                     proposedChildIndex index: Int) -> NSDragOperation {
        dropTarget(info, item: item, index: index) == nil ? [] : .move
    }

    func outlineView(_ outlineView: NSOutlineView, acceptDrop info: NSDraggingInfo, item: Any?,
                     childIndex index: Int) -> Bool {
        guard let to = dropTarget(info, item: item, index: index), let entry = draggedEntry(info) else { return false }
        let node = sources[entry.source]
        if let rule = entry.rule, let layer = entry.layer { return move(rule: node.layers[layer].rules[rule], to: to) }
        if let layer = entry.layer { return move(layer: node.layers[layer], to: to) }
        return move(source: node, to: to)
    }
}
