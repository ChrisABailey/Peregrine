// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// map_canvas.h — the map widget, the counterpart of MapCanvasView.swift.
#pragma once

#include <gtkmm.h>

#include <string>

#include "fv_desk_host.h"

namespace peregrine {

/// The frame's placement (`DeskHost::Placement`) as a GSK transform that
/// every GSK renderer draws; the caller unrefs it.
GskTransform* PlacementTransform(const fv::desk::FramePlacement& p);

/// Translates GTK input into `DeskHost` calls and paints the host's last
/// frame through the host's preview transform, so a gesture moves the
/// picture before the next frame is ready.
class MapCanvas : public Gtk::Widget {
 public:
  /// `host` must outlive the widget.
  explicit MapCanvas(fv::desk::DeskHost& host);
  ~MapCanvas() override;

  /// Emitted when the status bar text may have changed.
  sigc::signal<void()>& signal_status_changed() { return status_changed_; }
  /// Emitted with each error the core reported.
  sigc::signal<void(const std::string&)>& signal_error() { return error_; }
  /// Emitted with every tick, after the frame is shown.
  sigc::signal<void(const fv::desk::HostTick&)>& signal_tick() { return tick_; }

  /// Renders the current view, waits for the frame and shows it.
  void RenderNow();
  /// While held the widget makes no host calls but keeps painting the last
  /// frame; a size change is passed on at release. The window holds it while
  /// a host call waits on a dialog, so the host is not re-entered.
  void SetHeld(bool held);

 protected:
  void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;
  void size_allocate_vfunc(int width, int height, int baseline) override;
  void on_realize() override;
  void on_unrealize() override;

 private:
  bool OnTick(const Glib::RefPtr<Gdk::FrameClock>& clock);
  void Pump();
  void ShowFrame();
  void PushSurface();
  double DisplayScale() const;
  double MmPerPoint() const;
  void InstallControllers();
  bool OnScroll(double dx, double dy);
  bool OnKeyPressed(guint keyval, guint keycode, Gdk::ModifierType state);

  fv::desk::DeskHost& host_;
  GdkTexture* texture_ = nullptr;
  double texture_width_pt_ = 0;
  double texture_height_pt_ = 0;
  guint tick_id_ = 0;
  bool held_ = false;
  double pointer_x_ = 0;
  double pointer_y_ = 0;
  double drag_x_ = 0;
  double drag_y_ = 0;
  double wheel_accum_ = 0;
  double pinch_x_ = 0;
  double pinch_y_ = 0;
  double pinch_scale_ = 1;
  Glib::RefPtr<Gtk::EventControllerScroll> scroll_;
  Glib::RefPtr<Gtk::GestureZoom> zoom_;

  sigc::signal<void()> status_changed_;
  sigc::signal<void(const std::string&)> error_;
  sigc::signal<void(const fv::desk::HostTick&)> tick_;
};

}  // namespace peregrine
