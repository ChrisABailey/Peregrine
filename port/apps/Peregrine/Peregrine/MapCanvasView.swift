// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import AppKit
import CxxStdlib
import PeregrineCore

/// The map widget: translates AppKit events into `DeskHost` calls and shows
/// the host's last frame in a sublayer placed by the host's preview transform,
/// so a gesture moves the picture without redrawing it.
final class MapCanvasView: NSView {
    let host: fv.desk.DeskHost
    /// Called on the main thread when the status bar text may have changed.
    var onStatusChange: (() -> Void)?
    /// Called on the main thread with each error the core reported.
    var onError: ((String) -> Void)?
    /// Called on the main thread with every tick, after the frame is shown.
    var onTick: ((fv.desk.HostTick) -> Void)?

    private let frameLayer = CALayer()
    /// The space bar is down: a press grabs the map whatever overlay is edited.
    private var spaceHeld = false
    private var link: CADisplayLink?
    private var tracking: NSTrackingArea?

    init(host: fv.desk.DeskHost) {
        self.host = host
        super.init(frame: .zero)
        // Layer-hosting: the view owns its layer tree and AppKit draws nothing.
        let root = CALayer()
        root.backgroundColor = NSColor.black.cgColor
        root.isGeometryFlipped = true
        layer = root
        wantsLayer = true
        frameLayer.anchorPoint = .zero
        frameLayer.magnificationFilter = .linear
        frameLayer.isHidden = true
        root.addSublayer(frameLayer)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) is not used") }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    /// A click into an inactive window reaches the map: it sets the zoom
    /// anchor, and a plain click does not pan.
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: Surface

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        link?.invalidate()
        link = nil
        guard window != nil else { return }
        let l = displayLink(target: self, selector: #selector(tick(_:)))
        l.add(to: .main, forMode: .common)
        link = l
        pushSurface()
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        pushSurface()
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        pushSurface()
    }

    /// Hands the host the size in points, the backing scale and the screen's
    /// physical pitch, so 1:N on the status bar is 1:N on the glass.
    private func pushSurface() {
        guard let window, bounds.width > 0, bounds.height > 0 else { return }
        host.Resize(bounds.width, bounds.height, window.backingScaleFactor,
                    Self.mmPerPoint(window.screen))
    }

    /// The physical size of one point on `screen`; 0.25 mm when unknown.
    static func mmPerPoint(_ screen: NSScreen?) -> Double {
        guard let screen,
              let id = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber
        else { return 0.25 }
        let mm = CGDisplayScreenSize(CGDirectDisplayID(id.uint32Value))
        guard mm.width > 0, screen.frame.width > 0 else { return 0.25 }
        return mm.width / screen.frame.width
    }

    // MARK: Frames

    @objc private func tick(_ sender: CADisplayLink) { pump() }

    /// Picks up what the host has for the UI: a frame, a repaint, status text.
    private func pump() {
        let t = host.Tick()
        if t.new_frame || t.redraw {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            if t.new_frame { showFrame() }
            place()
            CATransaction.commit()
        }
        if t.status_changed { onStatusChange?() }
        let error = String(host.TakeError())
        if !error.isEmpty { onError?(error) }
        onTick?(t)
    }

    /// Puts the host's newest frame in the frame layer, sized in points.
    private func showFrame() {
        guard let image = makeImage() else { return }
        let scale = host.FrameDisplayScale()
        frameLayer.contents = image
        frameLayer.contentsScale = scale
        frameLayer.bounds = CGRect(x: 0, y: 0, width: CGFloat(image.width) / scale,
                                   height: CGFloat(image.height) / scale)
    }

    /// Moves the frame layer to where its frame belongs under the live view.
    private func place() {
        let p = host.Placement()
        frameLayer.isHidden = !p.valid || frameLayer.contents == nil
        guard p.valid else { return }
        frameLayer.position = .zero
        frameLayer.setAffineTransform(CGAffineTransform(a: p.a, b: p.b, c: p.c, d: p.d,
                                                        tx: p.tx, ty: p.ty))
    }

    /// Renders the current view, waits for the frame and paints it.
    func renderNow() {
        _ = host.Tick()
        host.WaitForRender()
        pump()
        CATransaction.flush()
    }

    private func makeImage() -> CGImage? {
        guard host.HasFrame() else { return nil }
        let w = Int(host.FrameWidth()), h = Int(host.FrameHeight())
        var bytes = Data(count: w * h * 4)
        let copied = bytes.withUnsafeMutableBytes { buf in
            host.CopyFrame(buf.baseAddress!.assumingMemoryBound(to: UInt8.self), buf.count)
        }
        guard copied else { return nil }
        let data = bytes as CFData
        guard let provider = CGDataProvider(data: data),
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
        return CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: w * 4, space: space,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false,
                       intent: .defaultIntent)
    }

    // MARK: Pointer

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        let t = NSTrackingArea(rect: .zero,
                               options: [.mouseMoved, .mouseEnteredAndExited,
                                         .activeInKeyWindow, .inVisibleRect],
                               owner: self, userInfo: nil)
        addTrackingArea(t)
        tracking = t
    }

    private func point(_ e: NSEvent) -> NSPoint { convert(e.locationInWindow, from: nil) }

    override func mouseMoved(with e: NSEvent) {
        let p = point(e)
        host.Hover(p.x, p.y)
    }

    override func mouseEntered(with e: NSEvent) { mouseMoved(with: e) }
    override func mouseExited(with e: NSEvent) { host.HoverExit() }

    override func mouseDown(with e: NSEvent) {
        window?.makeFirstResponder(self)
        let p = point(e)
        if spaceHeld {
            NSCursor.closedHand.set()
            host.PanPointerDown(p.x, p.y)
            return
        }
        host.PointerDown(p.x, p.y, e.modifierFlags.contains(.shift), Int32(min(e.clickCount, 2)))
    }

    override func mouseDragged(with e: NSEvent) {
        let p = point(e)
        host.PointerDrag(p.x, p.y)
    }

    override func mouseUp(with e: NSEvent) {
        let p = point(e)
        host.PointerUp(p.x, p.y)
        if spaceHeld { NSCursor.openHand.set() }
    }

    /// A trackpad pans; a wheel takes one ladder step per notch, rolled away
    /// from the user zooming in whatever the scroll-direction setting.
    override func scrollWheel(with e: NSEvent) {
        let p = point(e)
        if e.hasPreciseScrollingDeltas {
            host.Scroll(p.x, p.y, e.scrollingDeltaX, e.scrollingDeltaY, true)
        } else {
            let dy = e.isDirectionInvertedFromDevice ? -e.scrollingDeltaY : e.scrollingDeltaY
            host.Scroll(p.x, p.y, 0, dy, false)
        }
    }

    // MARK: Keys

    /// The overlay being edited sees a key first; otherwise Page Up and Page
    /// Down step the ladder about the cursor and the arrows pan half a screen.
    override func keyDown(with e: NSEvent) {
        let flags = e.modifierFlags
        if e.keyCode == Self.spaceKeyCode,
           flags.intersection([.command, .control, .option]).isEmpty {
            if !spaceHeld {
                spaceHeld = true
                NSCursor.openHand.set()
            }
            return
        }
        let text = e.characters?.unicodeScalars.first.map { $0.value } ?? 0
        if host.KeyDown(Int32(Self.keyCode(e)), UInt32(text >= 0xF700 ? 0 : text),
                        flags.contains(.shift), flags.contains(.control),
                        flags.contains(.option), flags.contains(.command)) {
            return
        }
        switch e.charactersIgnoringModifiers?.unicodeScalars.first.map({ Int($0.value) }) {
        case NSPageUpFunctionKey: host.Step(1)
        case NSPageDownFunctionKey: host.Step(-1)
        case NSUpArrowFunctionKey: _ = host.Execute(std.string("map.pan.north"))
        case NSDownArrowFunctionKey: _ = host.Execute(std.string("map.pan.south"))
        case NSRightArrowFunctionKey: _ = host.Execute(std.string("map.pan.east"))
        case NSLeftArrowFunctionKey: _ = host.Execute(std.string("map.pan.west"))
        default: super.keyDown(with: e)
        }
    }

    override func keyUp(with e: NSEvent) {
        guard e.keyCode == Self.spaceKeyCode else { return super.keyUp(with: e) }
        releaseSpace()
    }

    /// A key up lost to another window would leave the map in grab mode.
    override func resignFirstResponder() -> Bool {
        releaseSpace()
        return super.resignFirstResponder()
    }

    private func releaseSpace() {
        guard spaceHeld else { return }
        spaceHeld = false
        NSCursor.arrow.set()
    }

    private static let spaceKeyCode: UInt16 = 49

    /// Edit ▸ Undo and Redo while the map has focus: the workspace history.
    @objc func undo(_ sender: Any?) { _ = host.Execute(std.string("edit.undo")) }
    @objc func redo(_ sender: Any?) { _ = host.Execute(std.string("edit.redo")) }

    /// The fv::Key value (a Win32 virtual-key code) for `e`, or 0.
    private static func keyCode(_ e: NSEvent) -> Int {
        if e.keyCode == 53 { return 0x1B }  // Escape
        guard let c = e.charactersIgnoringModifiers?.uppercased().unicodeScalars.first else { return 0 }
        switch Int(c.value) {
        case 0x41...0x5A, 0x30...0x39: return Int(c.value)
        case 0x7F: return 0x08                // Delete (backspace)
        case 0x0D, 0x03: return 0x0D          // Return, Enter
        case 0x09: return 0x09
        case 0x20: return 0x20
        case NSDeleteFunctionKey: return 0x2E
        case NSLeftArrowFunctionKey: return 0x25
        case NSUpArrowFunctionKey: return 0x26
        case NSRightArrowFunctionKey: return 0x27
        case NSDownArrowFunctionKey: return 0x28
        case NSPageUpFunctionKey: return 0x21
        case NSPageDownFunctionKey: return 0x22
        case NSHomeFunctionKey: return 0x24
        case NSEndFunctionKey: return 0x23
        default: return 0
        }
    }

    override func magnify(with e: NSEvent) {
        let p = point(e)
        switch e.phase {
        case .began:
            host.MagnifyBegin(p.x, p.y)
            host.Magnify(p.x, p.y, 1 + e.magnification)
        case .ended, .cancelled:
            host.MagnifyEnd(p.x, p.y)
        default:
            host.Magnify(p.x, p.y, 1 + e.magnification)
        }
    }
}

extension MapCanvasView: NSMenuItemValidation {
    /// Undo and Redo are enabled by the workspace history; every other item
    /// this view answers for is enabled.
    func validateMenuItem(_ item: NSMenuItem) -> Bool {
        switch item.action {
        case Selector(("undo:")): return host.IsEnabled(std.string("edit.undo"))
        case Selector(("redo:")): return host.IsEnabled(std.string("edit.redo"))
        default: return true
        }
    }
}
