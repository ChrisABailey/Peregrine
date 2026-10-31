// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The query builder over one of `DeskHost`'s query models: clauses of field,
/// comparison and value joined by all or any, the query as a sentence, its
/// filter expression (editable by hand), and Verify. Edits go to the model's
/// draft as they are made; the owning dialog applies them.
@MainActor
final class QueryBuilderPanel: NSObject, NSTextFieldDelegate, NSComboBoxDelegate {
    /// `DeskHost::kQueryDefinition` and `DeskHost::kQuerySelect`, which Swift does not import.
    static let definition: Int32 = 0
    static let select: Int32 = 1
    /// `DeskHost::kQueryRule`: a style rule's filter.
    static let rule: Int32 = 2

    let host: fv.desk.DeskHost
    /// `definition`, `select` or `rule`.
    let which: Int32
    /// The panel's root view.
    let view = NSView()
    /// Receives each edit's error message, "" for success.
    var onReport: ((String) -> Void)?
    /// Called after every edit of the draft.
    var onChange: (() -> Void)?

    private let matchPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let rowsStack = NSStackView()
    private let customNote = NSTextField(wrappingLabelWithString:
        "The query is an expression the builder cannot show as clauses. Edit it below, or add a clause to start over.")
    private let sentenceLabel = NSTextField(wrappingLabelWithString: "")
    private let expressionField = NSTextField(string: "")
    private let verifyLabel = NSTextField(labelWithString: "")
    /// One entry per clause row, in clause order.
    private var rows: [ClauseRow] = []

    @MainActor
    private final class ClauseRow {
        let field = NSPopUpButton(frame: .zero, pullsDown: false)
        let op = NSPopUpButton(frame: .zero, pullsDown: false)
        let value = NSComboBox()
        let remove = NSButton(title: "\u{2212}", target: nil, action: nil)
        var stack: NSStackView!
    }

    init(host: fv.desk.DeskHost, which: Int32) {
        self.host = host
        self.which = which
        super.init()
        buildLayout()
        reload()
    }

    // MARK: Layout

    private func buildLayout() {
        matchPopup.addItems(withTitles: ["all of the clauses (and)", "any of the clauses (or)"])
        matchPopup.target = self
        matchPopup.action = #selector(matchChanged)
        let matchRow = NSStackView(views: [NSTextField(labelWithString: "Features matching"), matchPopup])
        matchRow.orientation = .horizontal

        rowsStack.orientation = .vertical
        rowsStack.alignment = .leading
        rowsStack.spacing = 6
        let rowsDocument = FlippedStackHost(stack: rowsStack)
        let rowsScroll = NSScrollView()
        rowsScroll.documentView = rowsDocument
        rowsScroll.hasVerticalScroller = true
        rowsScroll.borderType = .bezelBorder
        rowsScroll.drawsBackground = false
        rowsDocument.translatesAutoresizingMaskIntoConstraints = false
        rowsDocument.widthAnchor.constraint(equalTo: rowsScroll.contentView.widthAnchor).isActive = true
        rowsScroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 120).isActive = true
        rowsScroll.setContentHuggingPriority(.defaultLow, for: .vertical)

        let add = NSButton(title: "Add Clause", target: self, action: #selector(addClause))
        customNote.textColor = .secondaryLabelColor
        sentenceLabel.textColor = .secondaryLabelColor
        sentenceLabel.isSelectable = true
        expressionField.placeholderString = "No query: every feature"
        expressionField.font = .monospacedSystemFont(ofSize: NSFont.smallSystemFontSize, weight: .regular)
        expressionField.delegate = self
        let expressionLabel = NSTextField(labelWithString: "Expression")
        expressionLabel.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
        let verify = NSButton(title: "Verify", target: self, action: #selector(verifyQuery))
        let verifyRow = NSStackView(views: [verify, verifyLabel])
        verifyRow.orientation = .horizontal

        let stack = NSStackView(views: [matchRow, customNote, rowsScroll, add, sentenceLabel, expressionLabel,
                                        expressionField, verifyRow])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.setCustomSpacing(14, after: sentenceLabel)
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.topAnchor),
            stack.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            rowsScroll.widthAnchor.constraint(equalTo: stack.widthAnchor),
            customNote.widthAnchor.constraint(equalTo: stack.widthAnchor),
            sentenceLabel.widthAnchor.constraint(equalTo: stack.widthAnchor),
            expressionField.widthAnchor.constraint(equalTo: stack.widthAnchor),
        ])
    }

    private func makeRow() -> ClauseRow {
        let row = ClauseRow()
        for i in 0..<host.QueryFieldCount(which) {
            row.field.addItem(withTitle: String(host.QueryFieldTitle(which, i)))
            row.field.lastItem?.representedObject = String(host.QueryFieldName(which, i))
        }
        for k in 0..<host.QueryOpCount() { row.op.addItem(withTitle: String(host.QueryOpTitle(k))) }
        row.field.target = self
        row.field.action = #selector(clauseChanged(_:))
        row.op.target = self
        row.op.action = #selector(clauseChanged(_:))
        row.value.delegate = self
        row.value.completes = true
        row.value.numberOfVisibleItems = 12
        row.value.widthAnchor.constraint(greaterThanOrEqualToConstant: 180).isActive = true
        row.remove.target = self
        row.remove.action = #selector(removeClause(_:))
        row.stack = NSStackView(views: [row.field, row.op, row.value, row.remove])
        row.stack.orientation = .horizontal
        row.stack.spacing = 6
        return row
    }

    // MARK: Reload

    /// Rebuilds the clause rows and the expression from the model.
    func reload() {
        for row in rows { row.stack.removeFromSuperview() }
        rows = []
        let open = host.QueryOpen(which)
        view.isHidden = !open
        guard open else { return }
        matchPopup.selectItem(at: host.QueryMatchAny(which) ? 1 : 0)
        let custom = host.QueryIsCustom(which)
        customNote.isHidden = !custom
        for i in 0..<host.QueryClauseCount(which) {
            let row = makeRow()
            let field = String(host.QueryClauseField(which, i))
            if let index = row.field.itemArray.firstIndex(where: { ($0.representedObject as? String) == field }) {
                row.field.selectItem(at: index)
            }
            row.op.selectItem(at: Int(host.QueryClauseOp(which, i)))
            row.value.stringValue = String(host.QueryClauseValue(which, i))
            fillValues(row)
            rows.append(row)
            rowsStack.addArrangedSubview(row.stack)
        }
        reloadText()
    }

    /// Re-reads the sentence and the expression, leaving the rows as they are.
    private func reloadText() {
        let custom = host.QueryIsCustom(which)
        let sentence = String(host.QueryText(which))
        sentenceLabel.stringValue = custom ? "" : (sentence.isEmpty ? "Every feature" : sentence)
        sentenceLabel.isHidden = custom
        let error = String(host.QueryExpressionError(which))
        // An incomplete clause has no expression; keep the last one shown.
        if error.isEmpty { expressionField.stringValue = String(host.QueryExpression(which)) }
        verifyLabel.stringValue = error
        verifyLabel.textColor = error.isEmpty ? .labelColor : .systemRed
        for row in rows { row.value.isEnabled = host.QueryOpTakesValue(Int32(row.op.indexOfSelectedItem)) }
    }

    /// The field's values as the value box's suggestions.
    private func fillValues(_ row: ClauseRow) {
        row.value.removeAllItems()
        guard let field = row.field.selectedItem?.representedObject as? String else { return }
        let values = String(host.QuerySampleValues(which, std.string(field)))
        if !values.isEmpty { row.value.addItems(withObjectValues: values.components(separatedBy: "\n")) }
        row.value.isEnabled = host.QueryOpTakesValue(Int32(row.op.indexOfSelectedItem))
    }

    // MARK: Edits

    private func edited(_ error: String) {
        onReport?(error)
        reloadText()
        onChange?()
    }

    @objc private func matchChanged() {
        host.SetQueryMatchAny(which, matchPopup.indexOfSelectedItem == 1)
        edited("")
    }

    @objc private func addClause() {
        view.window?.makeFirstResponder(nil)
        _ = host.AddQueryClause(which)
        reload()
        onChange?()
    }

    @objc private func removeClause(_ sender: NSButton) {
        guard let i = rows.firstIndex(where: { $0.remove === sender }) else { return }
        let error = String(host.RemoveQueryClause(which, Int32(i)))
        reload()
        edited(error)
    }

    @objc private func clauseChanged(_ sender: NSPopUpButton) {
        guard let i = rows.firstIndex(where: { $0.field === sender || $0.op === sender }) else { return }
        if rows[i].field === sender { fillValues(rows[i]) }
        commit(i)
    }

    private func commit(_ i: Int) {
        let row = rows[i]
        let field = row.field.selectedItem?.representedObject as? String ?? ""
        edited(String(host.SetQueryClause(which, Int32(i), std.string(field), Int32(row.op.indexOfSelectedItem),
                                          std.string(row.value.stringValue))))
    }

    func comboBoxSelectionDidChange(_ notification: Notification) {
        guard let box = notification.object as? NSComboBox,
              let i = rows.firstIndex(where: { $0.value === box }) else { return }
        // The selection lands in stringValue only after this notification.
        DispatchQueue.main.async { [weak self] in self?.commit(i) }
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        if let box = obj.object as? NSComboBox, let i = rows.firstIndex(where: { $0.value === box }) {
            commit(i)
        } else if obj.object as? NSTextField === expressionField {
            let error = String(host.SetQueryExpression(which, std.string(expressionField.stringValue)))
            reload()
            edited(error)
        }
    }

    /// Writes any value being typed to the draft; the dialog calls it before applying.
    func commitEditing() {
        view.window?.makeFirstResponder(nil)
    }

    @objc private func verifyQuery() {
        commitEditing()
        let result = String(host.VerifyQuery(which))
        verifyLabel.stringValue = result
        verifyLabel.textColor = result.contains(" match") ? .labelColor : .systemRed
    }
}

/// A flipped document view holding a stack, so clause rows fill from the top.
final class FlippedStackHost: NSView {
    override var isFlipped: Bool { true }

    init(stack: NSStackView) {
        super.init(frame: .zero)
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -8),
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }
}
