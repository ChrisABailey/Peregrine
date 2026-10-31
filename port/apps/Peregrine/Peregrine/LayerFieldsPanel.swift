// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The Layer Fields editor over `DeskHost`'s fields model, the Fields tab of
/// a layer's Properties: the field list with add, delete and reorder, and an
/// editor for the selected field. Edits go to the draft as they are made; the
/// dialog applies them.
@MainActor
final class LayerFieldsPanel: NSObject, NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate,
    NSTextViewDelegate {
    let host: fv.desk.DeskHost
    /// The panel's root view.
    let view = NSView()
    /// Receives each edit's error message, "" for success.
    var onReport: ((String) -> Void)?

    private let table = NSTableView()
    private let addButton = NSButton(title: "+", target: nil, action: nil)
    private let removeButton = NSButton(title: "\u{2212}", target: nil, action: nil)
    private let upButton = NSButton(title: "\u{2191}", target: nil, action: nil)
    private let downButton = NSButton(title: "\u{2193}", target: nil, action: nil)
    private let nameField = NSTextField(string: "")
    private let titleField = NSTextField(string: "")
    private let typePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let rolePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let unitField = NSTextField(string: "")
    private let requiredBox = NSButton(checkboxWithTitle: "Required", target: nil, action: nil)
    private let defaultField = NSTextField(string: "")
    private let minField = NSTextField(string: "")
    private let maxField = NSTextField(string: "")
    private let choicesView = NSTextView()
    private let descriptionField = NSTextField(string: "")
    private let originLabel = NSTextField(labelWithString: "")
    private let editorGrid = NSGridView()

    private static let nameColumn = NSUserInterfaceItemIdentifier("name")
    private static let typeColumn = NSUserInterfaceItemIdentifier("type")

    init(host: fv.desk.DeskHost) {
        self.host = host
        super.init()
        buildLayout()
        reload()
    }

    /// The selected field's index, or -1.
    private var selected: Int32 { Int32(table.selectedRow) }

    /// Re-reads the field list, keeping the selection where it can.
    func reload() {
        let row = table.selectedRow
        table.reloadData()
        let count = Int(host.LayerFieldCount())
        let keep = min(max(row, 0), count - 1)
        if keep >= 0 { table.selectRowIndexes(IndexSet(integer: keep), byExtendingSelection: false) }
        reloadEditor()
    }

    /// Fills the editor from the selected field.
    private func reloadEditor() {
        let i = selected
        editorGrid.isHidden = i < 0
        removeButton.isEnabled = i >= 0
        upButton.isEnabled = i > 0
        downButton.isEnabled = i >= 0 && i + 1 < host.LayerFieldCount()
        guard i >= 0 else { return }
        nameField.stringValue = String(host.LayerFieldName(i))
        titleField.stringValue = String(host.LayerFieldTitle(i))
        let type = Int(host.LayerFieldType(i))
        typePopup.isEnabled = type >= 0
        if type >= 0 { typePopup.selectItem(at: type) }
        let role = Int(host.LayerFieldRole(i))
        rolePopup.selectItem(at: role)
        unitField.stringValue = String(host.LayerFieldUnit(i))
        requiredBox.state = host.LayerFieldRequired(i) ? .on : .off
        defaultField.stringValue = String(host.LayerFieldDefault(i))
        minField.stringValue = String(host.LayerFieldMin(i))
        maxField.stringValue = String(host.LayerFieldMax(i))
        choicesView.string = String(host.LayerFieldChoices(i))
        descriptionField.stringValue = String(host.LayerFieldDescription(i))
        let original = String(host.LayerFieldOriginalName(i))
        originLabel.stringValue = original.isEmpty ? "New field" : "Was \u{201c}\(original)\u{201d}"

        // Rows that do not apply to the current type or role are hidden.
        let numeric = type == 1 || type == 2
        setRow(of: unitField, hidden: role != 5)
        setRow(of: minField, hidden: !numeric)
        setRow(of: maxField, hidden: !numeric)
        setRow(of: choicesView.enclosingScrollView!, hidden: role != 4)
    }

    private func setRow(of view: NSView, hidden: Bool) {
        editorGrid.cell(for: view)?.row?.isHidden = hidden
    }

    // MARK: Layout

    private func buildLayout() {
        for (id, title, width) in [(Self.nameColumn, "Field", 150.0), (Self.typeColumn, "Type", 110.0)] {
            let column = NSTableColumn(identifier: id)
            column.title = title
            column.width = width
            table.addTableColumn(column)
        }
        table.dataSource = self
        table.delegate = self
        table.usesAlternatingRowBackgroundColors = true
        let tableScroll = NSScrollView()
        tableScroll.documentView = table
        tableScroll.hasVerticalScroller = true
        tableScroll.borderType = .bezelBorder

        for (button, action) in [(addButton, #selector(addField)), (removeButton, #selector(removeField)),
                                 (upButton, #selector(moveFieldUp)), (downButton, #selector(moveFieldDown))] {
            button.target = self
            button.action = action
        }
        let listButtons = NSStackView(views: [addButton, removeButton, upButton, downButton])
        listButtons.orientation = .horizontal
        let list = NSStackView(views: [tableScroll, listButtons])
        list.orientation = .vertical
        list.alignment = .leading

        for k in 0..<host.FieldTypeCount() { typePopup.addItem(withTitle: String(host.FieldTypeTitle(k))) }
        for k in 0..<host.FieldRoleCount() { rolePopup.addItem(withTitle: String(host.FieldRoleTitle(k))) }
        for popup in [typePopup, rolePopup] {
            popup.target = self
            popup.action = #selector(editorChanged)
        }
        requiredBox.target = self
        requiredBox.action = #selector(editorChanged)
        for field in [nameField, titleField, unitField, defaultField, minField, maxField, descriptionField] {
            field.delegate = self
        }
        choicesView.isRichText = false
        choicesView.font = NSFont.systemFont(ofSize: NSFont.systemFontSize)
        choicesView.delegate = self
        let choicesScroll = NSScrollView()
        choicesScroll.documentView = choicesView
        choicesScroll.hasVerticalScroller = true
        choicesScroll.borderType = .bezelBorder
        choicesView.autoresizingMask = [.width]
        choicesScroll.heightAnchor.constraint(equalToConstant: 80).isActive = true
        originLabel.textColor = .secondaryLabelColor

        let rows: [(String, NSView)] = [
            ("Name", nameField), ("Title", titleField), ("Type", typePopup), ("Meaning", rolePopup),
            ("Unit", unitField), ("", requiredBox), ("Default", defaultField), ("Minimum", minField),
            ("Maximum", maxField), ("Choices", choicesScroll), ("Description", descriptionField), ("", originLabel),
        ]
        for (title, view) in rows { editorGrid.addRow(with: [NSTextField(labelWithString: title), view]) }
        editorGrid.rowSpacing = 6
        editorGrid.columnSpacing = 8
        editorGrid.rowAlignment = .firstBaseline
        editorGrid.column(at: 0).xPlacement = .trailing
        editorGrid.column(at: 1).width = 260

        let middle = NSStackView(views: [list, editorGrid])
        middle.orientation = .horizontal
        middle.alignment = .top

        middle.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(middle)
        NSLayoutConstraint.activate([
            middle.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            middle.trailingAnchor.constraint(lessThanOrEqualTo: view.trailingAnchor),
            middle.topAnchor.constraint(equalTo: view.topAnchor),
            middle.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            tableScroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 300),
            tableScroll.widthAnchor.constraint(equalToConstant: 270),
        ])
    }

    // MARK: Field list

    func numberOfRows(in tableView: NSTableView) -> Int { Int(host.LayerFieldCount()) }

    func tableView(_ tableView: NSTableView, viewFor column: NSTableColumn?, row: Int) -> NSView? {
        let i = Int32(row)
        let text: String
        if column?.identifier == Self.typeColumn {
            let type = host.LayerFieldType(i)
            text = type >= 0 ? String(host.FieldTypeTitle(type)) : "Binary"
        } else {
            text = String(host.LayerFieldName(i))
        }
        return NSTextField(labelWithString: text)
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        onReport?("")
        reloadEditor()
    }

    // MARK: Edits

    /// Passes `error` on and re-reads the model.
    private func report(_ error: std.string) {
        onReport?(String(error))
        reload()
    }

    /// Writes every editor control to the selected field.
    @objc private func editorChanged() {
        let i = selected
        guard i >= 0 else { return }
        report(host.SetLayerField(i, std.string(nameField.stringValue), Int32(typePopup.indexOfSelectedItem),
                                  Int32(rolePopup.indexOfSelectedItem), std.string(titleField.stringValue),
                                  std.string(descriptionField.stringValue), std.string(unitField.stringValue),
                                  requiredBox.state == .on, std.string(defaultField.stringValue),
                                  std.string(minField.stringValue), std.string(maxField.stringValue),
                                  std.string(choicesView.string)))
    }

    func controlTextDidEndEditing(_ obj: Notification) { editorChanged() }

    func textDidEndEditing(_ notification: Notification) { editorChanged() }

    @objc private func addField() {
        view.window?.makeFirstResponder(nil)
        let i = Int(host.AddLayerField())
        table.reloadData()
        if i >= 0 { table.selectRowIndexes(IndexSet(integer: i), byExtendingSelection: false) }
        reload()
        view.window?.makeFirstResponder(nameField)
    }

    @objc private func removeField() {
        guard selected >= 0 else { return NSSound.beep() }
        report(host.RemoveLayerField(selected))
    }

    @objc private func moveFieldUp() { move(by: -1) }

    @objc private func moveFieldDown() { move(by: 1) }

    private func move(by step: Int32) {
        let from = selected
        guard from >= 0 else { return }
        let error = host.MoveLayerField(from, from + step)
        if String(error).isEmpty {
            table.selectRowIndexes(IndexSet(integer: Int(from + step)), byExtendingSelection: false)
        }
        report(error)
    }
}
