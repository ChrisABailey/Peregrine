// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "layer_style_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

#include "dialogs.h"

namespace peregrine {
namespace {

constexpr int kSwatchWidth = 36;
constexpr int kSwatchHeight = 18;

Gdk::RGBA ToRgba(uint32_t rgba) {
  Gdk::RGBA c;
  c.set_rgba(((rgba >> 24) & 0xff) / 255.0, ((rgba >> 16) & 0xff) / 255.0,
             ((rgba >> 8) & 0xff) / 255.0, (rgba & 0xff) / 255.0);
  return c;
}

uint32_t FromRgba(const Gdk::RGBA& c) {
  const auto byte = [](double v) {
    return static_cast<uint32_t>(std::clamp(std::lround(v * 255), 0L, 255L));
  };
  return byte(c.get_red()) << 24 | byte(c.get_green()) << 16 | byte(c.get_blue()) << 8 |
         byte(c.get_alpha());
}

/// A text entry that calls `commit` with its text on Enter or when it loses
/// focus with a changed value.
Gtk::Entry* CommitEntry(const std::string& text, std::function<void(const std::string&)> commit) {
  auto* entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(text);
  auto original = std::make_shared<std::string>(text);
  auto fire = [entry, original, commit] {
    const std::string now = entry->get_text();
    if (now == *original) return;
    *original = now;
    commit(now);
  };
  entry->signal_activate().connect(fire);
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect(fire);
  entry->add_controller(focus);
  return entry;
}

}  // namespace

LayerStyleWindow::LayerStyleWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_transient_for(parent);
  set_default_size(560, 520);
  set_size_request(480, 420);
  set_hide_on_close(true);
  Build();
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseLayerStyle();
        return false;
      },
      false);
  Reload();
}

void LayerStyleWindow::Build() {
  SetChoices(renderer_, {"Single symbol", "Categorised", "Graduated"}, 0);
  renderer_.property_selected().signal_changed().connect([this] {
    if (!loading_) Report(host_.SetStyleRenderer(SelectedIndex(renderer_)));
  });
  field_.property_selected().signal_changed().connect([this] {
    if (!loading_) Report(host_.SetStyleField(host_.StyleFieldName(SelectedIndex(field_))));
  });
  classify_.signal_clicked().connect([this] {
    Report(host_.ClassifyStyle(SelectedIndex(classes_) + 2, SelectedIndex(mode_), SelectedIndex(vary_)));
  });
  renderer_row_.append(*Gtk::make_managed<Gtk::Label>("Draw"));
  renderer_row_.append(renderer_);
  renderer_row_.append(by_);
  renderer_row_.append(field_);
  renderer_row_.append(classify_);

  std::vector<std::string> counts;
  for (int n = 2; n <= 10; ++n) counts.push_back(std::to_string(n));
  SetChoices(classes_, counts, 3);
  SetChoices(mode_, {"Equal intervals", "Quantiles"}, 0);
  SetChoices(vary_, {"Colour ramp", "Size"}, 0);
  graded_row_.append(*Gtk::make_managed<Gtk::Label>("Classes"));
  graded_row_.append(classes_);
  graded_row_.append(mode_);
  graded_row_.append(*Gtk::make_managed<Gtk::Label>("varying"));
  graded_row_.append(vary_);

  legend_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  legend_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (loading_ || !row) return;
    selected_ = row->get_index();
    ReloadSymbol();
  });
  legend_scroll_.set_child(legend_);
  legend_scroll_.set_has_frame(true);
  legend_scroll_.set_hexpand(true);
  legend_scroll_.set_size_request(-1, 180);
  legend_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  symbol_frame_.set_size_request(220, -1);
  auto* middle = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
  middle->set_vexpand(true);
  middle->append(legend_scroll_);
  middle->append(symbol_frame_);

  add_.signal_clicked().connect([this] { Report(host_.AddStyleEntry()); });
  remove_.signal_clicked().connect([this] {
    if (!legend_.get_selected_row()) {
      error_bell();
      return;
    }
    Report(host_.RemoveStyleEntry(legend_.get_selected_row()->get_index()));
  });
  clear_override_.signal_clicked().connect([this] { Report(host_.ClearStyleOverride()); });
  entry_buttons_.append(add_);
  entry_buttons_.append(remove_);
  entry_buttons_.append(clear_override_);

  label_field_.property_selected().signal_changed().connect([this] {
    if (!loading_) ApplyLabel();
  });
  label_size_.set_range(1, 200);
  label_size_.set_increments(1, 4);
  label_size_.set_digits(1);
  label_size_.signal_value_changed().connect([this] {
    if (!loading_) ApplyLabel();
  });
  auto* label_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  label_row->append(*Gtk::make_managed<Gtk::Label>("Labels"));
  label_row->append(label_field_);
  label_row->append(*Gtk::make_managed<Gtk::Label>("size"));
  label_row->append(label_size_);
  label_row->append(*Gtk::make_managed<Gtk::Label>("px"));

  message_.set_xalign(0);
  message_.set_wrap(true);
  message_.add_css_class("error");
  message_.set_visible(false);
  revert_.signal_clicked().connect([this] { Revert(); });
  cancel_.signal_clicked().connect([this] { close(); });
  apply_.signal_clicked().connect([this] { Apply(); });
  ok_.signal_clicked().connect([this] { Ok(); });
  ok_.add_css_class("suggested-action");
  set_default_widget(ok_);

  SetDialogMargins(box_);
  box_.append(renderer_row_);
  box_.append(graded_row_);
  box_.append(*middle);
  box_.append(entry_buttons_);
  box_.append(*label_row);
  box_.append(message_);
  box_.append(*ButtonRow({&revert_}, {&cancel_, &apply_, &ok_}));
  set_child(box_);
}

void LayerStyleWindow::Reload() {
  reload_pending_ = false;
  loading_ = true;
  set_title(host_.LayerStyleTitle());
  const bool feature = host_.LayerStyleIsFeature();
  const int renderer = host_.StyleRenderer();
  renderer_row_.set_visible(!feature);
  renderer_.set_selected(static_cast<guint>(std::max(renderer, 0)));
  std::vector<std::string> fields;
  int field = -1;
  for (int i = 0; i < host_.StyleFieldCount(); ++i) {
    fields.push_back(host_.StyleFieldTitle(i));
    if (host_.StyleFieldName(i) == host_.StyleField()) field = i;
  }
  SetChoices(field_, fields, field);
  by_.set_visible(renderer != 0);
  field_.set_visible(renderer != 0);
  classify_.set_visible(renderer != 0);
  graded_row_.set_visible(!feature && renderer == 2);
  classes_.set_selected(static_cast<guint>(std::clamp(host_.StyleClassCount() - 2, 0, 8)));
  mode_.set_selected(static_cast<guint>(std::max(host_.StyleClassMode(), 0)));
  vary_.set_selected(static_cast<guint>(std::max(host_.StyleClassVary(), 0)));
  add_.set_visible(!feature && renderer != 0);
  remove_.set_visible(!feature && renderer != 0);
  clear_override_.set_visible(feature);
  clear_override_.set_sensitive(host_.StyleHasOverride());

  std::vector<std::string> labels{"None"};
  int label = 0;
  for (int i = 0; i < host_.StyleLabelFieldCount(); ++i) {
    labels.push_back(host_.StyleLabelFieldTitle(i));
    if (host_.StyleLabelFieldName(i) == host_.StyleLabelField()) label = i + 1;
  }
  SetChoices(label_field_, labels, label);
  label_size_.set_value(host_.StyleLabelSize());

  while (Gtk::Widget* row = legend_.get_first_child()) legend_.remove(*row);
  const int count = host_.StyleEntryCount();
  for (int i = 0; i < count; ++i) legend_.append(*MakeLegendRow(i));
  selected_ = std::min(std::max(selected_, 0), count - 1);
  if (selected_ >= 0) legend_.select_row(*legend_.get_row_at_index(selected_));
  loading_ = false;
  ReloadSymbol();
}

/// One legend row: the swatch, the class value and the legend label; the
/// texts are editable for a class of a categorised or graduated layer.
Gtk::Widget* LayerStyleWindow::MakeLegendRow(int i) {
  auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  const int scale = std::max(get_scale_factor(), 1);
  const int w = kSwatchWidth * scale, h = kSwatchHeight * scale;
  std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
  auto* swatch = Gtk::make_managed<Gtk::Picture>();
  swatch->set_size_request(kSwatchWidth, kSwatchHeight);
  swatch->set_can_shrink(true);
  if (host_.CopyStyleSwatch(i, w, h, scale, pixels.data(), pixels.size())) {
    GBytes* bytes = g_bytes_new(pixels.data(), pixels.size());
    GdkTexture* texture =
        gdk_memory_texture_new(w, h, GDK_MEMORY_R8G8B8A8, bytes, static_cast<size_t>(w) * 4);
    g_bytes_unref(bytes);
    swatch->set_paintable(Glib::wrap(GDK_PAINTABLE(texture)));
  }
  row->append(*swatch);

  const bool editable =
      !host_.StyleEntryIsOther(i) && host_.StyleRenderer() != 0 && !host_.LayerStyleIsFeature();
  if (editable) {
    auto* value = CommitEntry(host_.StyleEntryValue(i), [this, i](const std::string& text) {
      Report(host_.SetStyleEntryValue(i, text));
    });
    value->set_size_request(140, -1);
    auto* label = CommitEntry(host_.StyleEntryLabel(i), [this, i](const std::string& text) {
      Report(host_.SetStyleEntryLabel(i, text));
    });
    label->set_hexpand(true);
    row->append(*value);
    row->append(*label);
  } else {
    for (const std::string& text : {host_.StyleEntryValue(i), host_.StyleEntryLabel(i)}) {
      auto* label = Gtk::make_managed<Gtk::Label>(text);
      label->set_xalign(0);
      label->set_hexpand(text.empty() ? false : true);
      row->append(*label);
    }
  }
  return row;
}

/// Rebuilds the symbol rows for the selected entry, grouped under headings.
void LayerStyleWindow::ReloadSymbol() {
  symbol_grid_ = Gtk::make_managed<Gtk::Grid>();
  symbol_grid_->set_row_spacing(6);
  symbol_grid_->set_column_spacing(8);
  symbol_grid_->set_margin_top(8);
  symbol_grid_->set_margin_bottom(8);
  symbol_grid_->set_margin_start(8);
  symbol_grid_->set_margin_end(8);
  std::string group;
  int line = 0;
  for (int k = 0; k < host_.StylePropertyCount(); ++k) {
    const std::string g = host_.StylePropertyGroup(k);
    if (g != group) {
      group = g;
      auto* heading = Gtk::make_managed<Gtk::Label>(g);
      heading->set_xalign(0);
      heading->add_css_class("heading");
      symbol_grid_->attach(*heading, 0, line++, 2, 1);
    }
    auto* title = Gtk::make_managed<Gtk::Label>(host_.StylePropertyTitle(k));
    title->set_xalign(0);
    symbol_grid_->attach(*title, 0, line);
    symbol_grid_->attach(*MakePropertyEditor(k), 1, line++);
  }
  symbol_frame_.set_child(*symbol_grid_);
}

/// The editor for property `k` of the selected entry.
Gtk::Widget* LayerStyleWindow::MakePropertyEditor(int k) {
  const int entry = std::max(selected_, 0);
  switch (host_.StylePropertyKind(k)) {
    case 0: {
      auto dialog = Gtk::ColorDialog::create();
      dialog->set_with_alpha(true);
      auto* well = Gtk::make_managed<Gtk::ColorDialogButton>(dialog);
      well->set_rgba(ToRgba(host_.StyleColor(entry, k)));
      well->set_halign(Gtk::Align::START);
      // Connected after the value is set, so filling it is not an edit.
      well->property_rgba().signal_changed().connect([this, well, entry, k] {
        Report(host_.SetStyleColor(entry, k, FromRgba(well->get_rgba())));
      });
      return well;
    }
    case 2: {
      std::vector<std::string> choices;
      for (int c = 0; c < host_.StylePropertyChoiceCount(k); ++c)
        choices.push_back(host_.StylePropertyChoice(k, c));
      auto* popup = Gtk::make_managed<Gtk::DropDown>();
      SetChoices(*popup, choices, host_.StyleChoice(entry, k));
      popup->property_selected().signal_changed().connect([this, popup, entry, k] {
        Report(host_.SetStyleChoice(entry, k, SelectedIndex(*popup)));
      });
      return popup;
    }
    default: {
      char text[32];
      std::snprintf(text, sizeof text, "%g", host_.StyleNumber(entry, k));
      auto* field = CommitEntry(text, [this, entry, k](const std::string& value) {
        char* end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        if (value.empty() || *end != '\0') Report("enter a number");
        else Report(host_.SetStyleNumber(entry, k, number));
      });
      field->set_width_chars(6);
      field->set_halign(Gtk::Align::START);
      return field;
    }
  }
}

/// Shows `error` (or clears the message) and redraws the legend once the
/// signal that made the edit has returned, since the rebuild replaces the
/// widget that sent it.
void LayerStyleWindow::Report(const std::string& error) {
  message_.set_text(error);
  message_.set_visible(!error.empty());
  if (reload_pending_) return;
  reload_pending_ = true;
  // A slot on the window is dropped if the window goes first.
  Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayerStyleWindow::ReloadIfPending));
}

void LayerStyleWindow::ReloadIfPending() {
  if (reload_pending_) Reload();
}

void LayerStyleWindow::ApplyLabel() {
  const int i = SelectedIndex(label_field_);
  const std::string name = i <= 0 ? std::string() : host_.StyleLabelFieldName(i - 1);
  Report(host_.SetStyleLabel(name, label_size_.get_value()));
}

void LayerStyleWindow::SelectEntry(int row) {
  if (Gtk::ListBoxRow* r = legend_.get_row_at_index(row)) legend_.select_row(*r);
}

bool LayerStyleWindow::Apply() {
  const std::string error = host_.ApplyLayerStyle();
  Report(error);
  return error.empty();
}

void LayerStyleWindow::Ok() {
  if (Apply()) close();
}

void LayerStyleWindow::Revert() { Report(host_.RevertLayerStyle()); }

std::string LayerStyleWindow::message() const {
  return message_.get_visible() ? std::string(message_.get_text()) : std::string();
}

}  // namespace peregrine
