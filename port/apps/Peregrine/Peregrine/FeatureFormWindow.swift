// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The feature form (Edit Feature…, Edit Point…): one row per control of
/// `DeskHost`'s form model, built from the layer's schema. Every edit is
/// pushed to the model as it is typed, so a rebuild loses nothing; Apply
/// writes the feature and OK applies and closes.
@MainActor
final class FeatureFormWindowController: NSWindowController, NSWindowDelegate, NSTextFieldDelegate,
    NSTextViewDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let grid = NSGridView()
    private let messageLabel = NSTextField(wrappingLabelWithString: "")
    private var errorLabels: [Int: NSTextField] = [:]
    /// Tags at or above this offset are a date range's end field.
    private static let endTag = 10_000

    /// `FormControl` values (fv_desk_feature_form.h).
    private enum Control: Int32 {
        case text = 0, multiline, integer, number, checkBox, date, dateTime, dateRange, choice,
             phone, url, email, readOnly
    }

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 440, height: 320),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: true)
        window.minSize = NSSize(width: 360, height: 200)
        super.init(window: window)
        window.delegate = self
        buildLayout(in: window)
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Rebuilds the rows from the form model.
    func reload() {
        window?.title = String(host.FeatureFormTitle())
        // removeRow(at:) leaves the cells' views in the grid; they must be removed separately.
        grid.subviews.forEach { $0.removeFromSuperview() }
        while grid.numberOfRows > 0 { grid.removeRow(at: 0) }
        errorLabels = [:]
        for i in 0..<host.FormFieldCount() {
            let title = String(host.FormFieldTitle(i)) + (host.FormFieldRequired(i) ? " *" : "")
            let label = NSTextField(labelWithString: title)
            label.toolTip = String(host.FormFieldDescription(i))
            let error = NSTextField(labelWithString: "")
            error.textColor = .systemRed
            error.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
            error.isHidden = true
            errorLabels[Int(i)] = error
            let editor = NSStackView(views: [control(for: i), error])
            editor.orientation = .vertical
            editor.alignment = .leading
            editor.spacing = 2
            grid.addRow(with: [label, editor])
        }
        showErrors()
    }

    // MARK: Layout

    private func buildLayout(in window: NSWindow) {
        grid.rowSpacing = 8
        grid.columnSpacing = 10
        grid.rowAlignment = .firstBaseline
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        let document = FlippedView()
        document.translatesAutoresizingMaskIntoConstraints = false
        grid.translatesAutoresizingMaskIntoConstraints = false
        document.addSubview(grid)
        scroll.documentView = document

        messageLabel.textColor = .systemRed
        let revert = NSButton(title: "Revert", target: self, action: #selector(revertForm))
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelForm))
        cancel.keyEquivalent = "\u{1b}"
        let apply = NSButton(title: "Apply", target: self, action: #selector(applyForm))
        let ok = NSButton(title: "OK", target: self, action: #selector(okForm))
        ok.keyEquivalent = "\r"
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let buttons = NSStackView(views: [revert, spacer, cancel, apply, ok])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [scroll, messageLabel, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
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
            scroll.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            messageLabel.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            buttons.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -32),
            document.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
            grid.leadingAnchor.constraint(equalTo: document.leadingAnchor),
            grid.trailingAnchor.constraint(lessThanOrEqualTo: document.trailingAnchor),
            grid.topAnchor.constraint(equalTo: document.topAnchor),
            grid.bottomAnchor.constraint(equalTo: document.bottomAnchor),
        ])
    }

    /// The editor for control `i`, chosen by its kind.
    private func control(for i: Int32) -> NSView {
        let text = String(host.FormFieldText(i))
        let kind = Control(rawValue: host.FormFieldControl(i)) ?? .readOnly
        switch kind {
        case .readOnly:
            return NSTextField(labelWithString: text)
        case .checkBox:
            let box = NSButton(checkboxWithTitle: "", target: self, action: #selector(checkChanged(_:)))
            box.tag = Int(i)
            box.state = text == "Yes" ? .on : .off
            return box
        case .choice:
            let popup = NSPopUpButton(frame: .zero, pullsDown: false)
            popup.tag = Int(i)
            if !host.FormFieldRequired(i) || text.isEmpty { popup.addItem(withTitle: "") }
            for c in 0..<host.FormFieldChoiceCount(i) { popup.addItem(withTitle: String(host.FormFieldChoice(i, c))) }
            if popup.itemTitles.contains(text) {
                popup.selectItem(withTitle: text)
            } else if !text.isEmpty {
                // A value outside the list stays visible until the user picks one.
                popup.addItem(withTitle: text)
                popup.selectItem(withTitle: text)
            }
            popup.target = self
            popup.action = #selector(choiceChanged(_:))
            return popup
        case .multiline:
            let view = NSTextView()
            view.string = text
            view.isRichText = false
            view.font = .systemFont(ofSize: NSFont.systemFontSize)
            view.delegate = self
            view.identifier = .init(String(i))
            let scroll = NSScrollView()
            scroll.documentView = view
            scroll.hasVerticalScroller = true
            scroll.borderType = .bezelBorder
            view.autoresizingMask = [.width]
            scroll.widthAnchor.constraint(equalToConstant: 260).isActive = true
            scroll.heightAnchor.constraint(equalToConstant: 64).isActive = true
            return scroll
        case .dateRange:
            let from = field(text, tag: Int(i), placeholder: "YYYY-MM-DD", width: 110)
            let to = field(String(host.FormFieldEndText(i)), tag: Self.endTag + Int(i),
                           placeholder: "YYYY-MM-DD", width: 110)
            let row = NSStackView(views: [from, NSTextField(labelWithString: "to"), to])
            row.orientation = .horizontal
            return row
        default:
            let placeholder: String
            switch kind {
            case .date: placeholder = "YYYY-MM-DD"
            case .dateTime: placeholder = "YYYY-MM-DDThh:mm"
            default: placeholder = ""
            }
            let numeric = kind == .integer || kind == .number || kind == .date || kind == .dateTime
            let input = field(text, tag: Int(i), placeholder: placeholder, width: numeric ? 140 : 260)
            var views: [NSView] = [input]
            let unit = String(host.FormFieldUnit(i))
            if !unit.isEmpty { views.append(NSTextField(labelWithString: unit)) }
            let action: String? = kind == .phone ? "Call" : kind == .url ? "Open" : kind == .email ? "Email" : nil
            if let action {
                let button = NSButton(title: action, target: self, action: #selector(openAction(_:)))
                button.tag = Int(i)
                button.controlSize = .small
                views.append(button)
            }
            let row = NSStackView(views: views)
            row.orientation = .horizontal
            return row
        }
    }

    private func field(_ text: String, tag: Int, placeholder: String, width: CGFloat) -> NSTextField {
        let f = NSTextField(string: text)
        f.tag = tag
        f.placeholderString = placeholder
        f.delegate = self
        f.widthAnchor.constraint(equalToConstant: width).isActive = true
        return f
    }

    /// Shows each control's error from the last validation under it.
    private func showErrors() {
        for (i, label) in errorLabels {
            let error = String(host.FormFieldError(Int32(i)))
            label.stringValue = error
            label.isHidden = error.isEmpty
        }
        messageLabel.stringValue = ""
        messageLabel.isHidden = true
    }

    // MARK: Edits

    func controlTextDidChange(_ notification: Notification) {
        guard let f = notification.object as? NSTextField else { return }
        if f.tag >= Self.endTag {
            _ = host.SetFormFieldEndText(Int32(f.tag - Self.endTag), std.string(f.stringValue))
        } else {
            _ = host.SetFormFieldText(Int32(f.tag), std.string(f.stringValue))
        }
    }

    func textDidChange(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, let id = view.identifier,
              let i = Int32(id.rawValue) else { return }
        _ = host.SetFormFieldText(i, std.string(view.string))
    }

    @objc private func checkChanged(_ sender: NSButton) {
        _ = host.SetFormFieldText(Int32(sender.tag), std.string(sender.state == .on ? "Yes" : "No"))
    }

    @objc private func choiceChanged(_ sender: NSPopUpButton) {
        _ = host.SetFormFieldText(Int32(sender.tag), std.string(sender.titleOfSelectedItem ?? ""))
    }

    @objc private func openAction(_ sender: NSButton) {
        let link = String(host.FormFieldActionUrl(Int32(sender.tag)))
        guard let url = URL(string: link) else {
            NSSound.beep()
            return
        }
        NSWorkspace.shared.open(url)
    }

    // MARK: Buttons

    /// Applies the form; false, with the errors shown, when a value is bad.
    @discardableResult
    private func apply() -> Bool {
        window?.makeFirstResponder(nil)  // ends an edit in progress
        let error = String(host.ApplyFeatureForm())
        if error.isEmpty {
            reload()
            return true
        }
        showErrors()
        messageLabel.stringValue = error
        messageLabel.isHidden = false
        return false
    }

    @objc private func applyForm() { apply() }

    @objc private func okForm() {
        if apply() { close() }
    }

    @objc private func cancelForm() { close() }

    @objc private func revertForm() {
        _ = host.RevertFeatureForm()
        reload()
    }

    func windowWillClose(_ notification: Notification) {
        host.CloseFeatureForm()
        onClose?()
    }
}

/// A view whose origin is the top left, so a scroll view shows the top of the form first.
private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}
