// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The route elevation profile window: terrain along the route, a readout of
/// the sample under the cursor and a summary line. The chart's layout comes
/// from `DeskHost`; this window only draws it and re-reads it when the core
/// re-samples.
@MainActor
final class ProfileWindowController: NSWindowController, NSWindowDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let chart: ProfileChartView
    private let readout = NSTextField(labelWithString: "")
    private let stats = NSTextField(labelWithString: "")

    init(host: fv.desk.DeskHost) {
        self.host = host
        chart = ProfileChartView(host: host)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 400),
                              styleMask: [.titled, .closable, .resizable, .miniaturizable],
                              backing: .buffered, defer: true)
        window.minSize = NSSize(width: 420, height: 260)
        window.tabbingMode = .disallowed
        super.init(window: window)
        window.delegate = self

        for label in [readout, stats] {
            label.font = .monospacedSystemFont(ofSize: NSFont.smallSystemFontSize, weight: .regular)
            label.lineBreakMode = .byTruncatingTail
            label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        }
        stats.textColor = .secondaryLabelColor
        chart.onReadout = { [weak self] text in self?.readout.stringValue = text }

        let stack = NSStackView(views: [chart, readout, stats])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 4
        stack.edgeInsets = NSEdgeInsets(top: 0, left: 0, bottom: 8, right: 0)
        for label in [readout, stats] {
            label.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 12).isActive = true
            label.trailingAnchor.constraint(lessThanOrEqualTo: stack.trailingAnchor,
                                            constant: -12).isActive = true
        }
        chart.leadingAnchor.constraint(equalTo: stack.leadingAnchor).isActive = true
        chart.trailingAnchor.constraint(equalTo: stack.trailingAnchor).isActive = true
        chart.setContentHuggingPriority(.defaultLow, for: .vertical)
        window.contentView = stack
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    /// Re-reads the title, the summary and the chart after the core re-sampled.
    func reload() {
        let title = String(host.RouteProfileTitle())
        window?.title = title.isEmpty ? "Terrain Profile" : title
        stats.stringValue = String(host.RouteProfileStats())
        chart.refreshReadout()
        chart.needsDisplay = true
    }

    func windowWillClose(_ notification: Notification) {
        host.CloseRouteProfile()
        onClose?()
    }
}

/// Draws the profile layout `DeskHost` computes for the view's size, with a
/// cursor line and readout that follow the mouse.
@MainActor
final class ProfileChartView: NSView {
    let host: fv.desk.DeskHost
    /// Receives the readout text for the cursor position, "" when outside the plot.
    var onReadout: ((String) -> Void)?
    private var cursorX: CGFloat?

    init(host: fv.desk.DeskHost) {
        self.host = host
        super.init(frame: NSRect(x: 0, y: 0, width: 720, height: 340))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    override var isFlipped: Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(rect: .zero,
                                       options: [.mouseMoved, .mouseEnteredAndExited,
                                                 .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    override func mouseMoved(with event: NSEvent) {
        cursorX = convert(event.locationInWindow, from: nil).x
        refreshReadout()
        needsDisplay = true
    }

    override func mouseExited(with event: NSEvent) {
        cursorX = nil
        onReadout?("")
        needsDisplay = true
    }

    /// Re-reads the readout at the cursor, for a chart that was re-sampled under it.
    func refreshReadout() {
        host.LayoutRouteProfile(Double(bounds.width), Double(bounds.height))
        guard let x = cursorX else { onReadout?(""); return }
        onReadout?(String(host.RouteProfileReadout(Double(x))))
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.textBackgroundColor.setFill()
        bounds.fill()
        host.LayoutRouteProfile(Double(bounds.width), Double(bounds.height))
        let box = host.RouteProfileBox()
        guard box.valid else {
            drawText(String(host.RouteProfileMessage()),
                     at: NSPoint(x: bounds.midX, y: bounds.midY), align: .center,
                     color: .secondaryLabelColor, size: NSFont.systemFontSize)
            return
        }
        let x0 = CGFloat(box.x0), y0 = CGFloat(box.y0), x1 = CGFloat(box.x1), y1 = CGFloat(box.y1)

        let grid = NSBezierPath()
        for i in 0..<host.RouteProfileTickCount(1) {
            let y = CGFloat(host.RouteProfileTickPos(1, i))
            grid.move(to: NSPoint(x: x0, y: y))
            grid.line(to: NSPoint(x: x1, y: y))
            drawText(String(host.RouteProfileTickLabel(1, i)), at: NSPoint(x: x0 - 6, y: y),
                     align: .right)
        }
        for i in 0..<host.RouteProfileTickCount(0) {
            let x = CGFloat(host.RouteProfileTickPos(0, i))
            grid.move(to: NSPoint(x: x, y: y0))
            grid.line(to: NSPoint(x: x, y: y1))
            drawText(String(host.RouteProfileTickLabel(0, i)), at: NSPoint(x: x, y: y1 + 12),
                     align: .center)
        }
        NSColor.separatorColor.setStroke()
        grid.lineWidth = 1
        grid.stroke()

        // Terrain: each run filled down to the axis, then its outline.
        let terrain = NSColor.systemGreen
        for run in 0..<host.RouteProfileRunCount() {
            let n = host.RouteProfileRunLength(run)
            let points = (0..<n).map {
                NSPoint(x: host.RouteProfileRunX(run, $0), y: host.RouteProfileRunY(run, $0))
            }
            if points.count == 1 {
                terrain.setFill()
                NSBezierPath(ovalIn: NSRect(x: points[0].x - 2, y: points[0].y - 2,
                                            width: 4, height: 4)).fill()
                continue
            }
            let fill = NSBezierPath()
            fill.move(to: NSPoint(x: points[0].x, y: y1))
            for p in points { fill.line(to: p) }
            fill.line(to: NSPoint(x: points[points.count - 1].x, y: y1))
            fill.close()
            terrain.withAlphaComponent(0.25).setFill()
            fill.fill()
            let line = NSBezierPath()
            line.move(to: points[0])
            for p in points.dropFirst() { line.line(to: p) }
            line.lineWidth = 2
            line.lineJoinStyle = .round
            terrain.setStroke()
            line.stroke()
        }

        let vertices = NSBezierPath()
        for i in 0..<host.RouteProfileVertexCount() {
            let x = CGFloat(host.RouteProfileVertexX(i))
            vertices.move(to: NSPoint(x: x, y: y0))
            vertices.line(to: NSPoint(x: x, y: y1))
        }
        vertices.setLineDash([2, 3], count: 2, phase: 0)
        NSColor.tertiaryLabelColor.setStroke()
        vertices.stroke()

        NSColor.secondaryLabelColor.setStroke()
        NSBezierPath(rect: NSRect(x: x0, y: y0, width: x1 - x0, height: y1 - y0)).stroke()

        if let x = cursorX, x >= x0, x <= x1 {
            let cursor = NSBezierPath()
            cursor.move(to: NSPoint(x: x, y: y0))
            cursor.line(to: NSPoint(x: x, y: y1))
            NSColor.systemBlue.setStroke()
            cursor.stroke()
        }
    }

    /// Draws `text` vertically centred on `point`, aligned to it by `align`.
    private func drawText(_ text: String, at point: NSPoint, align: NSTextAlignment,
                          color: NSColor = .secondaryLabelColor,
                          size: CGFloat = NSFont.smallSystemFontSize - 1) {
        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedDigitSystemFont(ofSize: size, weight: .regular),
            .foregroundColor: color,
        ]
        let s = NSAttributedString(string: text, attributes: attrs)
        let w = s.size()
        var origin = NSPoint(x: point.x, y: point.y - w.height / 2)
        switch align {
        case .right: origin.x -= w.width
        case .center: origin.x -= w.width / 2
        default: break
        }
        s.draw(at: origin)
    }
}
