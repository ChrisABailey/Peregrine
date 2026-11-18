// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/app/manager_host.h — an OverlayHost over a bare OverlayManager, for
/// applications without a Desk (scripts, PythonView, tests).
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/app/overlay_host.h"
#include "fvkit/app/undo.h"

namespace fv {

class OverlayManager;

namespace app {

/// Mouse capture and snapping through `manager`, an undo stack of its own,
/// and settable elevation, label font and display scale. The manager must
/// outlive the host. Destroying the host unbinds the overlays in the manager
/// that still hold it; an overlay bound but never added must be unbound first.
class ManagerHost : public OverlayHost {
 public:
  explicit ManagerHost(OverlayManager& manager) : manager_(manager) {}
  ~ManagerHost() override;
  ManagerHost(const ManagerHost&) = delete;
  ManagerHost& operator=(const ManagerHost&) = delete;

  /// Binds `overlay` when it is HostAware; false otherwise.
  bool Bind(Overlay& overlay);
  /// Unbinds `overlay` and drops its undo entries.
  void Unbind(Overlay& overlay);

  /// True once since the last call when an overlay asked for a redraw.
  bool TakeRedraw() { return redraw_.exchange(false); }
  /// The source `OpenElevation` hands out and `ElevationAt` reads; null for none.
  void SetElevation(std::shared_ptr<IElevationSource> source) { elevation_ = std::move(source); }
  void SetLabelFont(std::string path) { label_font_ = std::move(path); }
  void SetDisplayScale(double scale) { display_scale_ = scale > 0 ? scale : 1.0; }
  /// The catalog `Coverage` reads; null for none.
  void SetCatalog(std::shared_ptr<const Catalog> catalog) { catalog_ = std::move(catalog); }
  /// The fix `OwnshipFix` reports; one without a position clears it.
  void SetOwnship(const PositionFix& fix) {
    std::lock_guard<std::mutex> lock(mutex_);
    ownship_ = fix;
  }
  /// The alarms raised since the last call, oldest first.
  std::vector<std::string> TakeAlarms() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(alarms_, {});
  }

  void RequestRedraw() override { redraw_.store(true); }
  Status CaptureMouse(Overlay& overlay) override;
  void ReleaseMouse(Overlay& overlay) override;
  bool ElevationAt(const GeoPoint& p, double* metres) override;
  /// The shared source set by SetElevation; a ManagerHost has one source.
  std::shared_ptr<IElevationSource> OpenElevation() override { return elevation_; }
  std::string LabelFont() const override { return label_font_; }
  double DisplayScale() const override { return display_scale_; }
  void PushUndo(UndoEntry entry) override { undo_.Push(std::move(entry)); }
  UndoStack& History() override { return undo_; }
  std::vector<SnapToItem> SnapCandidates(const MapProjection& proj, PixelPoint p,
                                         double tolerance_px) override;
  void ShowStatus(const std::string& text) override { status_ = text; }
  /// The last text an overlay showed.
  const std::string& status() const { return status_; }
  std::vector<CoverageRow> Coverage(const std::string& format, const GeoRect& area) override;
  bool OwnshipFix(PositionFix* out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ownship_.has_position) return false;
    *out = ownship_;
    return true;
  }
  void RaiseAlarm(const std::string& text) override {
    std::lock_guard<std::mutex> lock(mutex_);
    alarms_.push_back(text);
  }

 private:
  OverlayManager& manager_;
  UndoStack undo_;
  std::atomic<bool> redraw_{false};
  std::shared_ptr<IElevationSource> elevation_;
  std::string label_font_;
  double display_scale_ = 1.0;
  std::string status_;
  std::shared_ptr<const Catalog> catalog_;
  std::mutex mutex_;  ///< guards ownship_ and alarms_, which overlays reach from their own threads
  PositionFix ownship_;
  std::vector<std::string> alarms_;
};

}  // namespace app
}  // namespace fv
