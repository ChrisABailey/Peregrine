// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// profile_window.h — the route elevation profile window, the counterpart
/// of ProfileWindow.swift.
#pragma once

#include <gtkmm.h>

#include <optional>
#include <string>

#include "fv_desk_host.h"

namespace peregrine {

/// Terrain along the route, a readout of the sample under the cursor and a
/// summary line. The chart's layout comes from `DeskHost`; this window only
/// draws it with cairo and re-reads it when the core re-samples. Closing the
/// window stops the re-sampling.
class ProfileWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  ProfileWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Re-reads the title, the summary and the chart after the core re-sampled.
  void Reload();
  /// Moves the cursor line to `x` (points in the chart), or removes it.
  void SetCursor(std::optional<double> x);

  Gtk::DrawingArea& chart() { return chart_; }
  std::string readout() const { return readout_.get_text(); }
  std::string stats() const { return stats_.get_text(); }

 private:
  void Draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
  void RefreshReadout();
  void DrawText(const Cairo::RefPtr<Cairo::Context>& cr, const std::string& text, double x, double y,
                Pango::Alignment align, bool small);

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 4};
  Gtk::DrawingArea chart_;
  Gtk::Label readout_;
  Gtk::Label stats_;
  std::optional<double> cursor_x_;
};

}  // namespace peregrine
