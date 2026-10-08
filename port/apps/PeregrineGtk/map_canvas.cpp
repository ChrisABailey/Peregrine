// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "map_canvas.h"

#include <cmath>

namespace peregrine {

MapCanvas::MapCanvas(fv::desk::DeskHost& host) : host_(host) {
  set_name("map-canvas");
  set_focusable(true);
  set_hexpand(true);
  set_vexpand(true);
  set_overflow(Gtk::Overflow::HIDDEN);
  property_scale_factor().signal_changed().connect([this] { PushSurface(); });
  InstallControllers();
}

MapCanvas::~MapCanvas() {
  if (texture_) g_object_unref(texture_);
}

// MARK: Surface

void MapCanvas::size_allocate_vfunc(int width, int height, int baseline) {
  Gtk::Widget::size_allocate_vfunc(width, height, baseline);
  PushSurface();
}

/// Hands the host the size in points, the display scale and the monitor's
/// physical pitch, so 1:N on the status bar is 1:N on the glass.
void MapCanvas::PushSurface() {
  const int w = get_width();
  const int h = get_height();
  if (w <= 0 || h <= 0 || !get_native()) return;
  host_.Resize(w, h, DisplayScale(), MmPerPoint());
}

/// Pixels per point of the surface the widget is on.
double MapCanvas::DisplayScale() const {
#if GTK_CHECK_VERSION(4, 12, 0)
  // Fractional scales are reported by the surface from GTK 4.12.
  if (GtkNative* native = gtk_widget_get_native(const_cast<GtkWidget*>(gobj()))) {
    if (GdkSurface* surface = gtk_native_get_surface(native)) return gdk_surface_get_scale(surface);
  }
#endif
  return get_scale_factor();
}

/// The physical size of one point on the widget's monitor; 0.25 mm when the
/// monitor does not report its size.
double MapCanvas::MmPerPoint() const {
  constexpr double kUnknown = 0.25;
  const auto* native = get_native();
  if (!native) return kUnknown;
  auto surface = const_cast<Gtk::Native*>(native)->get_surface();
  if (!surface) return kUnknown;
  auto monitor = get_display()->get_monitor_at_surface(surface);
  if (!monitor) return kUnknown;
  Gdk::Rectangle geometry;
  monitor->get_geometry(geometry);
  const int mm = monitor->get_width_mm();
  if (mm <= 0 || geometry.get_width() <= 0) return kUnknown;
  return static_cast<double>(mm) / geometry.get_width();
}

// MARK: Frames

void MapCanvas::on_realize() {
  Gtk::Widget::on_realize();
  tick_id_ = add_tick_callback(sigc::mem_fun(*this, &MapCanvas::OnTick));
  PushSurface();
}

void MapCanvas::on_unrealize() {
  if (tick_id_) remove_tick_callback(tick_id_);
  tick_id_ = 0;
  Gtk::Widget::on_unrealize();
}

bool MapCanvas::OnTick(const Glib::RefPtr<Gdk::FrameClock>&) {
  Pump();
  return G_SOURCE_CONTINUE;
}

/// Picks up what the host has for the UI: a frame, a repaint, status text.
void MapCanvas::Pump() {
  const fv::desk::HostTick t = host_.Tick();
  if (t.new_frame) ShowFrame();
  if (t.new_frame || t.redraw) queue_draw();
  if (t.status_changed) status_changed_.emit();
  const std::string error = host_.TakeError();
  if (!error.empty()) error_.emit(error);
  tick_.emit(t);
}

/// Copies the host's newest frame into a texture sized in points.
void MapCanvas::ShowFrame() {
  const uint8_t* pixels = host_.FramePixels();
  const int w = host_.FrameWidth();
  const int h = host_.FrameHeight();
  if (!pixels || w <= 0 || h <= 0) return;
  const size_t stride = static_cast<size_t>(w) * 4;
  GBytes* bytes = g_bytes_new(pixels, stride * static_cast<size_t>(h));
  GdkTexture* texture =
      gdk_memory_texture_new(w, h, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED, bytes, stride);
  g_bytes_unref(bytes);
  if (texture_) g_object_unref(texture_);
  texture_ = texture;
  const double scale = host_.FrameDisplayScale() > 0 ? host_.FrameDisplayScale() : 1.0;
  texture_width_pt_ = w / scale;
  texture_height_pt_ = h / scale;
}

// Built as translate · rotate · skew · scale: GSK's cairo renderer, the
// fallback without GL, draws such a transform but paints one made from a
// general matrix pink.
GskTransform* PlacementTransform(const fv::desk::FramePlacement& p) {
  constexpr double kDegrees = 180.0 / 3.14159265358979323846;
  // Columns (a, b) and (c, d) are R(angle) applied to (sx, 0) and (k, sy).
  const double sx = std::hypot(p.a, p.b);
  const double angle = std::atan2(p.b, p.a);
  const double cos_a = std::cos(angle), sin_a = std::sin(angle);
  const double k = cos_a * p.c + sin_a * p.d;
  const double sy = -sin_a * p.c + cos_a * p.d;
  const graphene_point_t offset =
      GRAPHENE_POINT_INIT(static_cast<float>(p.tx), static_cast<float>(p.ty));
  GskTransform* t = gsk_transform_translate(nullptr, &offset);
  if (angle != 0) t = gsk_transform_rotate(t, static_cast<float>(angle * kDegrees));
  if (k != 0 && sy != 0)
    t = gsk_transform_skew(t, static_cast<float>(std::atan(k / sy) * kDegrees), 0.f);
  return gsk_transform_scale(t, static_cast<float>(sx), static_cast<float>(sy));
}

/// Paints black, then the last frame where it belongs under the live view.
void MapCanvas::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) {
  GtkSnapshot* s = snapshot->gobj();
  const GdkRGBA black = {0, 0, 0, 1};
  const graphene_rect_t bounds =
      GRAPHENE_RECT_INIT(0.f, 0.f, static_cast<float>(get_width()), static_cast<float>(get_height()));
  gtk_snapshot_append_color(s, &black, &bounds);
  const fv::desk::FramePlacement p = host_.Placement();
  if (!p.valid || !texture_) return;
  const graphene_rect_t frame = GRAPHENE_RECT_INIT(0.f, 0.f, static_cast<float>(texture_width_pt_),
                                                   static_cast<float>(texture_height_pt_));
  gtk_snapshot_save(s);
  GskTransform* t = PlacementTransform(p);
  gtk_snapshot_transform(s, t);
  gsk_transform_unref(t);
  gtk_snapshot_append_texture(s, texture_, &frame);
  gtk_snapshot_restore(s);
}

void MapCanvas::RenderNow() {
  host_.Tick();
  host_.WaitForRender();
  Pump();
}

// MARK: Input

void MapCanvas::InstallControllers() {
  auto motion = Gtk::EventControllerMotion::create();
  motion->signal_enter().connect([this](double x, double y) {
    pointer_x_ = x;
    pointer_y_ = y;
    host_.Hover(x, y);
  });
  motion->signal_motion().connect([this](double x, double y) {
    pointer_x_ = x;
    pointer_y_ = y;
    host_.Hover(x, y);
  });
  motion->signal_leave().connect([this] { host_.HoverExit(); });
  add_controller(motion);

  auto drag = Gtk::GestureDrag::create();
  drag->set_button(GDK_BUTTON_PRIMARY);
  drag->signal_drag_begin().connect([this](double x, double y) {
    grab_focus();
    drag_x_ = x;
    drag_y_ = y;
    host_.PointerDown(x, y);
  });
  drag->signal_drag_update().connect(
      [this](double dx, double dy) { host_.PointerDrag(drag_x_ + dx, drag_y_ + dy); });
  drag->signal_drag_end().connect(
      [this](double dx, double dy) { host_.PointerUp(drag_x_ + dx, drag_y_ + dy); });
  add_controller(drag);

  scroll_ = Gtk::EventControllerScroll::create();
  scroll_->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
  scroll_->signal_scroll().connect(sigc::mem_fun(*this, &MapCanvas::OnScroll), false);
  add_controller(scroll_);

  zoom_ = Gtk::GestureZoom::create();
  zoom_->signal_begin().connect([this](Gdk::EventSequence*) {
    if (!zoom_->get_bounding_box_center(pinch_x_, pinch_y_)) {
      pinch_x_ = pointer_x_;
      pinch_y_ = pointer_y_;
    }
    pinch_scale_ = 1;
    host_.MagnifyBegin(pinch_x_, pinch_y_);
  });
  // GTK reports the scale since the pinch began; the host wants the change
  // since the previous event.
  zoom_->signal_scale_changed().connect([this](double scale) {
    if (scale <= 0 || pinch_scale_ <= 0) return;
    host_.Magnify(pinch_x_, pinch_y_, scale / pinch_scale_);
    pinch_scale_ = scale;
  });
  zoom_->signal_end().connect(
      [this](Gdk::EventSequence*) { host_.MagnifyEnd(pinch_x_, pinch_y_); });
  add_controller(zoom_);

  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(sigc::mem_fun(*this, &MapCanvas::OnKeyPressed), false);
  add_controller(keys);
}

/// A touchpad pans; a wheel takes one ladder step per notch, rolled away from
/// the user zooming in.
bool MapCanvas::OnScroll(double dx, double dy) {
  // GTK's deltas move the viewport; the host's pan and notch move the map,
  // so both are negated.
  if (gtk_event_controller_scroll_get_unit(scroll_->gobj()) == GDK_SCROLL_UNIT_SURFACE) {
    host_.Scroll(pointer_x_, pointer_y_, -dx, -dy, true);
    return true;
  }
  // A high-resolution wheel reports fractions of a notch; the host steps
  // once per event, so whole notches are collected first.
  wheel_accum_ -= dy;
  const double notches = std::trunc(wheel_accum_);
  if (notches != 0) {
    wheel_accum_ -= notches;
    host_.Scroll(pointer_x_, pointer_y_, 0, notches, false);
  }
  return true;
}

/// Page Up and Page Down step the ladder about the cursor.
bool MapCanvas::OnKeyPressed(guint keyval, guint, Gdk::ModifierType) {
  switch (keyval) {
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
      host_.Step(1);
      return true;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
      host_.Step(-1);
      return true;
    default:
      return false;
  }
}

}  // namespace peregrine
