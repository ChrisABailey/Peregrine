// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The Get Info window: the sheet the map menu's Get Info… last asked for,
/// read-only, a heading per section and selectable values. Reloaded in place
/// when another Get Info arrives.
@MainActor
final class ItemInfoWindowController: NSWindowController, NSWindowDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    /// Replaced on each reload: NSGridView traps when it removes a row whose
    /// cells are merged, as the section headings are.
    private var grid = NSGridView(numberOfColumns: 2, rows: 0)
    private let sheetView = InfoFlippedView()
    private let scroll = NSScrollView()

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 420, height: 380),
                             styleMask: [.titled, .closable, .resizable, .utilityWindow],
                             backing: .buffered, defer: true)
        window.minSize = NSSize(width: 300, height: 200)
        window.hidesOnDeactivate = false
        super.init(window: window)
        window.delegate = self
        sheetView.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView = sheetView
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        window.contentView = scroll
        sheetView.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor).isActive = true
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the title and rows from the host.
    func reload() {
        window?.title = "Info \u{2014} " + String(host.ItemInfoTitle())
        grid.removeFromSuperview()
        grid = NSGridView(numberOfColumns: 2, rows: 0)
        grid.rowSpacing = 4
        grid.columnSpacing = 12
        grid.rowAlignment = .firstBaseline
        grid.column(at: 0).xPlacement = .trailing
        grid.translatesAutoresizingMaskIntoConstraints = false
        sheetView.addSubview(grid)
        NSLayoutConstraint.activate([
            grid.leadingAnchor.constraint(equalTo: sheetView.leadingAnchor, constant: 12),
            grid.trailingAnchor.constraint(lessThanOrEqualTo: sheetView.trailingAnchor, constant: -12),
            grid.topAnchor.constraint(equalTo: sheetView.topAnchor, constant: 12),
            grid.bottomAnchor.constraint(equalTo: sheetView.bottomAnchor, constant: -12),
        ])
        var section = ""
        for i in 0..<host.ItemInfoCount() {
            let s = String(host.ItemInfoSection(i))
            if i == 0 || s != section {
                section = s
                let heading = NSTextField(labelWithString: s)
                heading.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
                let row = grid.addRow(with: [heading, NSGridCell.emptyContentView])
                grid.mergeCells(inHorizontalRange: NSRange(location: 0, length: 2),
                                verticalRange: NSRange(location: grid.numberOfRows - 1, length: 1))
                grid.cell(for: heading)?.xPlacement = .leading
                if grid.numberOfRows > 1 { row.topPadding = 10 }
            }
            let key = NSTextField(labelWithString: String(host.ItemInfoKey(i)))
            key.textColor = .secondaryLabelColor
            key.alignment = .right
            let value = NSTextField(wrappingLabelWithString: String(host.ItemInfoValue(i)))
            value.isSelectable = true
            value.preferredMaxLayoutWidth = 280
            grid.addRow(with: [key, value])
        }
        scroll.contentView.scroll(to: .zero)
    }

    func windowWillClose(_ notification: Notification) { onClose?() }
}

private final class InfoFlippedView: NSView {
    override var isFlipped: Bool { true }
}
