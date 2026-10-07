// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_host.h — the one object a native shell drives.
///
/// `DeskHost` owns a `Desk`, its settings, a shell that queues what the core
/// tells the UI, and the render worker. The interface is plain values (doubles,
/// strings, a pixel pointer) so a Swift, GTK or WinUI shell calls it directly;
/// this header includes nothing but the standard library. Single-threaded
/// apart from the render worker: call every method from the UI thread.
///
/// A shell calls `Tick()` once per display refresh, redraws when it says so,
/// and paints the last frame through `Placement()` so a gesture moves the
/// picture before the next frame is ready.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#if __has_include(<swift/bridging>)
#include <swift/bridging>
#else
#define SWIFT_SHARED_REFERENCE(_retain, _release)
#define SWIFT_RETURNS_RETAINED
#endif

namespace fv {
namespace desk {
class DeskHost;
}  // namespace desk
}  // namespace fv

/// Reference counting for Swift's import of `DeskHost` as a class.
void fv_desk_host_retain(fv::desk::DeskHost* host);
void fv_desk_host_release(fv::desk::DeskHost* host);

namespace fv {
namespace desk {

/// What changed since the previous `Tick()`.
struct HostTick {
  bool new_frame = false;       ///< a newer frame is available
  bool redraw = false;          ///< the widget should repaint
  bool status_changed = false;  ///< the status bar text changed
};

/// Where the shown frame belongs under the live view, in points:
/// x' = a*x + c*y + tx, y' = b*x + d*y + ty. Invalid when there is nothing to
/// place (no frame, or no common ground).
struct FramePlacement {
  double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
  bool valid = false;
};

class SWIFT_SHARED_REFERENCE(fv_desk_host_retain, fv_desk_host_release) DeskHost {
 public:
  /// A host with an empty catalog and a reference count of one. Registers
  /// fvkit's built-in raster formats.
  static DeskHost* Create() SWIFT_RETURNS_RETAINED;

  DeskHost(const DeskHost&) = delete;
  DeskHost& operator=(const DeskHost&) = delete;

  // MARK: Catalog and commands

  /// Opens a catalog database and centres the view on its data. Returns an
  /// empty string on success, else the error message.
  std::string OpenCatalog(const std::string& path);
  std::string CatalogPath() const;
  /// Moves the camera and chooses the product there (`Desk::GoTo`).
  void GoTo(double lat, double lon, double scale_denom);
  double CenterLat() const;
  double CenterLon() const;
  double ScaleDenom() const;
  /// Executes a DeskKit command by id; returns the error message or "".
  std::string Execute(const std::string& command_id);
  /// The next error the core reported through the shell, or "" when none.
  std::string TakeError();

  // MARK: Surface and input (points, top-left origin)

  void Resize(double width_pt, double height_pt, double display_scale,
              double mm_per_point);
  void Hover(double x, double y);
  void HoverExit();
  void PointerDown(double x, double y);
  void PointerDrag(double x, double y);
  void PointerUp(double x, double y);
  /// `precise` (trackpad) pans by (dx, dy); a wheel steps the ladder once per
  /// notch of `dy`, positive zooming in.
  void Scroll(double x, double y, double dx, double dy, bool precise);
  void MagnifyBegin(double x, double y);
  /// `factor` > 1 zooms in, relative to the previous event of the pinch.
  void Magnify(double x, double y, double factor);
  void MagnifyEnd(double x, double y);
  /// One ladder step about the cursor (Page Up is +1).
  void Step(int direction);

  // MARK: Frames

  /// Requests a frame when the view changed and picks up a finished one.
  HostTick Tick();
  bool HasFrame() const;
  int FrameWidth() const;   ///< pixels
  int FrameHeight() const;  ///< pixels
  /// RGBA8, premultiplied, rows of `FrameWidth() * 4` bytes. Valid until the
  /// next `Tick()`; null when there is no frame.
  const uint8_t* FramePixels() const;
  /// Copies the frame's pixels into `out`, which holds `capacity` bytes.
  /// False when there is no frame or it does not fit. The Swift shell's
  /// accessor: a pointer into a reference type is not imported.
  bool CopyFrame(uint8_t* out, size_t capacity) const;
  /// The display scale the frame was rendered at (pixels per point).
  double FrameDisplayScale() const;
  FramePlacement Placement() const;
  /// Blocks until the render worker is idle; for tests and screenshots.
  void WaitForRender();

  // MARK: Status bar

  std::string StatusScale() const;     ///< "1:50,000"
  std::string StatusProduct() const;   ///< "cadrg GNC", or "" when none
  std::string StatusMessage() const;   ///< e.g. the end of the raster ladder
  std::string StatusPosition() const;  ///< the position under the cursor, or ""

 private:
  friend void ::fv_desk_host_retain(DeskHost*);
  friend void ::fv_desk_host_release(DeskHost*);
  struct Impl;

  DeskHost();
  ~DeskHost();

  std::atomic<int> refs_{1};
  std::unique_ptr<Impl> impl_;
};

}  // namespace desk
}  // namespace fv
