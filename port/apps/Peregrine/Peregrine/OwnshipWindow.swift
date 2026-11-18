// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The Ownship window (View ▸ Ownship…): the ownship's read-outs and the
/// replay clock's time row. Everything shown comes formatted from
/// `DeskHost.OwnshipPanel`; the window re-reads it on `ownship_changed`.
@MainActor
final class OwnshipWindowController: NSWindowController, NSWindowDelegate {
    let host: fv.desk.DeskHost
    /// Called once the window has closed.
    var onClose: (() -> Void)?

    private let status = NSTextField(labelWithString: "")
    private let source = NSTextField(labelWithString: "")
    private let time = NSTextField(labelWithString: "")
    private let position = NSTextField(labelWithString: "")
    private let altitude = NSTextField(labelWithString: "")
    private let heading = NSTextField(labelWithString: "")
    private let derived = NSTextField(labelWithString: "derived")
    private let speed = NSTextField(labelWithString: "")

    private let toStart = NSButton()
    private let playPause = NSButton()
    private let toEnd = NSButton()
    private let slider = NSSlider(value: 0, minValue: 0, maxValue: 1, target: nil, action: nil)
    private let clock = NSTextField(labelWithString: "")
    private let rate = NSPopUpButton(frame: .zero, pullsDown: false)

    private var panel = fv.desk.HostOwnshipPanel()

    init(host: fv.desk.DeskHost) {
        self.host = host
        let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 420, height: 240),
                             styleMask: [.titled, .closable, .utilityWindow],
                             backing: .buffered, defer: true)
        window.title = "Ownship"
        window.isFloatingPanel = true
        window.hidesOnDeactivate = false
        window.becomesKeyOnlyIfNeeded = true
        super.init(window: window)
        window.delegate = self

        let mono = NSFont.monospacedDigitSystemFont(ofSize: NSFont.systemFontSize, weight: .regular)
        for label in [status, source, time, position, altitude, heading, speed, clock] {
            label.font = mono
            label.lineBreakMode = .byTruncatingTail
        }
        status.font = .boldSystemFont(ofSize: NSFont.systemFontSize)
        derived.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        derived.textColor = .secondaryLabelColor

        let headingRow = NSStackView(views: [heading, derived])
        headingRow.spacing = 6
        let rows: [(String, NSView)] = [
            ("Mode", status), ("Source", source), ("Time", time), ("Position", position),
            ("Altitude", altitude), ("Heading", headingRow), ("Speed", speed),
        ]
        let grid = NSGridView(views: rows.map { title, view in
            let caption = NSTextField(labelWithString: title + ":")
            caption.textColor = .secondaryLabelColor
            caption.alignment = .right
            return [caption, view]
        })
        grid.column(at: 0).xPlacement = .trailing
        grid.rowSpacing = 4
        grid.columnSpacing = 8

        configure(toStart, symbol: "backward.end.fill", tip: "Go to the start",
                  action: #selector(goToStart))
        configure(playPause, symbol: "play.fill", tip: "Play", action: #selector(togglePlay))
        configure(toEnd, symbol: "forward.end.fill", tip: "Go to the end", action: #selector(goToEnd))
        slider.target = self
        slider.action = #selector(scrub)
        slider.isContinuous = true
        slider.setContentHuggingPriority(.defaultLow, for: .horizontal)
        for i in 0..<host.OwnshipRateCount() {
            rate.addItem(withTitle: String(host.OwnshipRateLabel(i)))
        }
        rate.target = self
        rate.action = #selector(chooseRate)
        rate.toolTip = "Playback speed"

        let timeRow = NSStackView(views: [toStart, playPause, toEnd, slider, clock, rate])
        timeRow.spacing = 6

        let stack = NSStackView(views: [grid, timeRow])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        timeRow.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 12).isActive = true
        timeRow.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -12).isActive = true
        window.contentView = stack
        reload()
        window.setContentSize(NSSize(width: 420, height: stack.fittingSize.height))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    private func configure(_ button: NSButton, symbol: String, tip: String, action: Selector) {
        button.bezelStyle = .texturedRounded
        button.image = NSImage(systemSymbolName: symbol, accessibilityDescription: tip)
        button.toolTip = tip
        button.target = self
        button.action = action
    }

    /// Re-reads the panel from the core and updates every control.
    func reload() {
        panel = host.OwnshipPanel()
        status.stringValue = String(panel.status)
        source.stringValue = panel.source.empty() ? "\u{2014}" : String(panel.source)
        time.stringValue = String(panel.time)
        position.stringValue = String(panel.position)
        altitude.stringValue = String(panel.altitude)
        heading.stringValue = String(panel.heading)
        derived.isHidden = !panel.heading_derived
        speed.stringValue = String(panel.speed)

        let enabled = panel.clock_enabled
        for control in [toStart, playPause, toEnd, slider, rate] as [NSControl] {
            control.isEnabled = enabled
        }
        let playing = enabled && panel.playing
        playPause.image = NSImage(systemSymbolName: playing ? "pause.fill" : "play.fill",
                                  accessibilityDescription: playing ? "Pause" : "Play")
        playPause.toolTip = playing ? "Pause" : "Play"
        clock.stringValue = String(panel.clock_text)
        if enabled {
            slider.minValue = panel.start_s
            slider.maxValue = max(panel.end_s, panel.start_s + 1)
            slider.doubleValue = panel.clock_s
        }
        if panel.rate_index >= 0 {
            rate.selectItem(at: Int(panel.rate_index))
        } else {
            rate.select(nil)
            rate.setTitle(String(panel.rate_label))
        }
    }

    @objc private func goToStart() { host.SeekReplay(panel.start_s) }
    @objc private func goToEnd() { host.SeekReplay(panel.end_s) }
    @objc private func togglePlay() { host.SetReplayPlaying(!panel.playing) }
    @objc private func scrub() { host.SeekReplay(slider.doubleValue) }
    @objc private func chooseRate() { host.SetOwnshipRate(Int32(rate.indexOfSelectedItem)) }

    func windowWillClose(_ notification: Notification) {
        onClose?()
    }
}
