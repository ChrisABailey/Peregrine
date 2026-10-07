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

    private let frameLayer = CALayer()
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
        host.PointerDown(p.x, p.y)
    }

    override func mouseDragged(with e: NSEvent) {
        let p = point(e)
        host.PointerDrag(p.x, p.y)
    }

    override func mouseUp(with e: NSEvent) {
        let p = point(e)
        host.PointerUp(p.x, p.y)
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
