// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/app/overlay_host.h — the services an application offers an overlay
/// (port/plugin-parity-plan.md PX-D3).
///
/// An overlay that wants them returns itself from `Overlay::AsHostAware()`;
/// the application calls `SetHost` when the overlay joins the stack, again
/// whenever a service's answer changes (elevation data, display scale), and
/// `SetHost(nullptr)` when it leaves. Built-in overlays and the plugin
/// adapters (C, Python) all receive their services this way.
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <vector>

#include "fvkit/app/capabilities.h"  // SnapToItem
#include "fvkit/app/undo.h"
#include "fvkit/catalog/catalog.h"  // CoverageRow
#include "fvkit/geo.h"
#include "fvkit/nav/position.h"

namespace fv {

class Overlay;
struct IElevationSource;

namespace app {

/// What an overlay may ask of the application. Every call except
/// `RequestRedraw` is made on the UI thread.
class OverlayHost {
 public:
  virtual ~OverlayHost() = default;

  /// Schedules a repaint of the map. Safe from any thread while the host
  /// lives; requests made before the next frame coalesce.
  virtual void RequestRedraw() = 0;
  /// Sends every mouse event to `overlay` until `ReleaseMouse`. Fails when
  /// the overlay is not in the stack.
  virtual Status CaptureMouse(Overlay& overlay) = 0;
  /// Ends `overlay`'s capture; does nothing when it does not hold the mouse.
  virtual void ReleaseMouse(Overlay& overlay) = 0;
  /// Ground elevation at `p` in metres. False off coverage, or when the
  /// catalog has no elevation data.
  virtual bool ElevationAt(const GeoPoint& p, double* metres) = 0;
  /// A new source over the catalog's elevation data (lidar over DTED), owned
  /// by the caller and safe to hand to one worker thread. Null when the
  /// catalog has none. `HostAware::SetHost` is called again when it changes.
  virtual std::shared_ptr<IElevationSource> OpenElevation() = 0;
  /// A TrueType font path for labels, or "" when the machine has none.
  virtual std::string LabelFont() const = 0;
  /// Device pixels per layout pixel; 1 on a standard display.
  virtual double DisplayScale() const = 0;
  /// Records an edit already made, so Edit ▸ Undo can reverse it. `entry.owner`
  /// should be the overlay: its entries are dropped when it leaves the stack.
  virtual void PushUndo(UndoEntry entry) = 0;
  /// The window's undo stack, for an overlay that keeps its history there.
  virtual UndoStack& History() = 0;
  /// The points of every overlay in the stack that a cursor at `p` could snap
  /// to, nearest first (see app::SnapCandidates).
  virtual std::vector<SnapToItem> SnapCandidates(const MapProjection& proj, PixelPoint p,
                                                 double tolerance_px) = 0;
  /// Shows `text` in the status bar until the next hover replaces it.
  virtual void ShowStatus(const std::string& text) = 0;
  /// The catalog's coverage rows of `format` ("osm", "enc", "vpf", ...) that
  /// meet `area`, as the base map sees them. Empty without a catalog.
  /// `HostAware::SetHost` is called again when the catalog changes.
  virtual std::vector<CoverageRow> Coverage(const std::string& format, const GeoRect& area) = 0;
  /// The ownship's latest fix from the moving map (live or replay), for
  /// range and collision checks. False when there is no ownship. Safe from any
  /// thread.
  virtual bool OwnshipFix(PositionFix* out) {
    (void)out;
    return false;
  }
  /// Puts `text` in front of the user as an alarm (a collision warning), not
  /// a status line that the next hover replaces. Safe from any thread; the
  /// application shows it on its UI thread. A host without alarms drops it.
  virtual void RaiseAlarm(const std::string& text) { (void)text; }
};

/// The capability an overlay implements to receive the host. The pointer is
/// atomic so a worker thread may read it for `RequestRedraw`.
class HostAware {
 public:
  virtual ~HostAware() = default;
  /// The host when the overlay is in a stack, else null.
  virtual void SetHost(OverlayHost* host) { host_.store(host); }
  OverlayHost* host() const { return host_.load(); }
  /// Whether the overlay can work with the host it was given; the application
  /// reports an error when it joins the stack (e.g. no elevation data).
  virtual Status HostStatus() const { return Status::Ok(); }

 private:
  std::atomic<OverlayHost*> host_{nullptr};
};

}  // namespace app
}  // namespace fv
