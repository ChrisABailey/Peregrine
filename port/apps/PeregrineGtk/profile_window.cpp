// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "profile_window.h"

#include <vector>

#include "dialogs.h"

namespace peregrine {
namespace {

// Adwaita's green 4 and blue 3.
constexpr double kTerrain[3] = {0x2e / 255.0, 0xc2 / 255.0, 0x7e / 255.0};
constexpr double kCursor[3] = {0x35 / 255.0, 0x84 / 255.0, 0xe4 / 255.0};

}  // namespace

ProfileWindow::ProfileWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_transient_for(parent);
  set_default_size(720, 400);
  set_size_request(420, 260);
  set_hide_on_close(true);

  // The "view" style class paints the chart's background.
  chart_.add_css_class("view");
  chart_.set_vexpand(true);
  chart_.set_hexpand(true);
  chart_.set_draw_func(sigc::mem_fun(*this, &ProfileWindow::Draw));
  chart_.signal_resize().connect([this](int, int) { RefreshReadout(); });
  auto motion = Gtk::EventControllerMotion::create();
  motion->signal_motion().connect([this](double x, double) { SetCursor(x); });
  motion->signal_enter().connect([this](double x, double) { SetCursor(x); });
  motion->signal_leave().connect([this] { SetCursor(std::nullopt); });
  chart_.add_controller(motion);

  for (Gtk::Label* label : {&readout_, &stats_}) {
    label->set_xalign(0);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    label->add_css_class("monospace");
    label->add_css_class("caption");
    label->set_margin_start(12);
    label->set_margin_end(12);
  }
  stats_.add_css_class("dim-label");
  stats_.set_margin_bottom(8);
  box_.append(chart_);
  box_.append(readout_);
  box_.append(stats_);
  set_child(box_);
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseRouteProfile();
        return false;
      },
      false);
  Reload();
}

void ProfileWindow::Reload() {
  const std::string title = host_.RouteProfileTitle();
  set_title(title.empty() ? "Terrain Profile" : title);
  stats_.set_text(host_.RouteProfileStats());
  RefreshReadout();
  chart_.queue_draw();
}

void ProfileWindow::SetCursor(std::optional<double> x) {
  cursor_x_ = x;
  RefreshReadout();
  chart_.queue_draw();
}

/// Re-reads the readout at the cursor, for a chart re-sampled under it.
void ProfileWindow::RefreshReadout() {
  host_.LayoutRouteProfile(chart_.get_width(), chart_.get_height());
  readout_.set_text(cursor_x_ ? host_.RouteProfileReadout(*cursor_x_) : std::string());
}

/// Draws `text` vertically centred on (x, y), aligned to x by `align`.
void ProfileWindow::DrawText(const Cairo::RefPtr<Cairo::Context>& cr, const std::string& text,
                             double x, double y, Pango::Alignment align, bool small) {
  auto layout = chart_.create_pango_layout(text);
  Pango::FontDescription font = chart_.get_pango_context()->get_font_description();
  font.set_size(font.get_size() * (small ? 0.8 : 1.0));
  if (small) font.set_family("Monospace");
  layout->set_font_description(font);
  int w = 0, h = 0;
  layout->get_pixel_size(w, h);
  double left = x;
  if (align == Pango::Alignment::RIGHT) left -= w;
  else if (align == Pango::Alignment::CENTER) left -= w / 2.0;
  cr->move_to(left, y - h / 2.0);
  layout->show_in_cairo_context(cr);
}

void ProfileWindow::Draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
  GdkRGBA fg;
  gtk_widget_get_color(GTK_WIDGET(chart_.gobj()), &fg);
  const auto ink = [&](double alpha) { cr->set_source_rgba(fg.red, fg.green, fg.blue, fg.alpha * alpha); };
  host_.LayoutRouteProfile(width, height);
  const fv::desk::HostChartBox box = host_.RouteProfileBox();
  if (!box.valid) {
    ink(0.6);
    DrawText(cr, host_.RouteProfileMessage(), width / 2.0, height / 2.0, Pango::Alignment::CENTER, false);
    return;
  }

  cr->set_line_width(1);
  ink(0.15);
  for (int i = 0; i < host_.RouteProfileTickCount(1); ++i) {
    const double y = host_.RouteProfileTickPos(1, i);
    cr->move_to(box.x0, y);
    cr->line_to(box.x1, y);
  }
  for (int i = 0; i < host_.RouteProfileTickCount(0); ++i) {
    const double x = host_.RouteProfileTickPos(0, i);
    cr->move_to(x, box.y0);
    cr->line_to(x, box.y1);
  }
  cr->stroke();
  ink(0.6);
  for (int i = 0; i < host_.RouteProfileTickCount(1); ++i)
    DrawText(cr, host_.RouteProfileTickLabel(1, i), box.x0 - 6, host_.RouteProfileTickPos(1, i),
             Pango::Alignment::RIGHT, true);
  for (int i = 0; i < host_.RouteProfileTickCount(0); ++i)
    DrawText(cr, host_.RouteProfileTickLabel(0, i), host_.RouteProfileTickPos(0, i), box.y1 + 12,
             Pango::Alignment::CENTER, true);

  // Terrain: each run filled down to the axis, then its outline.
  for (int run = 0; run < host_.RouteProfileRunCount(); ++run) {
    const int n = host_.RouteProfileRunLength(run);
    if (n <= 0) continue;
    std::vector<std::pair<double, double>> points;
    for (int k = 0; k < n; ++k) points.emplace_back(host_.RouteProfileRunX(run, k), host_.RouteProfileRunY(run, k));
    if (n == 1) {
      cr->set_source_rgb(kTerrain[0], kTerrain[1], kTerrain[2]);
      cr->arc(points[0].first, points[0].second, 2, 0, 2 * G_PI);
      cr->fill();
      continue;
    }
    cr->move_to(points.front().first, box.y1);
    for (const auto& [x, y] : points) cr->line_to(x, y);
    cr->line_to(points.back().first, box.y1);
    cr->close_path();
    cr->set_source_rgba(kTerrain[0], kTerrain[1], kTerrain[2], 0.25);
    cr->fill();
    cr->move_to(points.front().first, points.front().second);
    for (const auto& [x, y] : points) cr->line_to(x, y);
    cr->set_line_width(2);
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    cr->set_source_rgb(kTerrain[0], kTerrain[1], kTerrain[2]);
    cr->stroke();
  }

  cr->set_line_width(1);
  cr->set_dash(std::vector<double>{2, 3}, 0);
  ink(0.35);
  for (int i = 0; i < host_.RouteProfileVertexCount(); ++i) {
    const double x = host_.RouteProfileVertexX(i);
    cr->move_to(x, box.y0);
    cr->line_to(x, box.y1);
  }
  cr->stroke();
  cr->unset_dash();

  ink(0.6);
  cr->rectangle(box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0);
  cr->stroke();

  if (cursor_x_ && *cursor_x_ >= box.x0 && *cursor_x_ <= box.x1) {
    cr->set_source_rgb(kCursor[0], kCursor[1], kCursor[2]);
    cr->move_to(*cursor_x_, box.y0);
    cr->line_to(*cursor_x_, box.y1);
    cr->stroke();
  }
}

}  // namespace peregrine
