// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "options_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "dialogs.h"

namespace peregrine {
namespace {

/// "#RRGGBBAA" for a colour.
std::string ColorText(const Gdk::RGBA& c) {
  const auto byte = [](double v) { return static_cast<int>(std::clamp(std::lround(v * 255), 0L, 255L)); };
  char text[16];
  std::snprintf(text, sizeof text, "#%02X%02X%02X%02X", byte(c.get_red()), byte(c.get_green()),
                byte(c.get_blue()), byte(c.get_alpha()));
  return text;
}

/// The colour of "#RRGGBBAA"; black for anything else.
Gdk::RGBA ColorOf(const std::string& text) {
  Gdk::RGBA c("black");
  if (text.size() != 9 || text[0] != '#') return c;
  char* end = nullptr;
  const unsigned long v = std::strtoul(text.c_str() + 1, &end, 16);
  if (*end != '\0') return c;
  c.set_rgba(((v >> 24) & 0xff) / 255.0, ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0,
             (v & 0xff) / 255.0);
  return c;
}

}  // namespace

OptionsWindow::OptionsWindow(fv::desk::DeskHost& host, int kind, Gtk::Window& parent)
    : host_(host), kind_(kind) {
  set_title(kind == fv::desk::kOptionsMap ? "Map Options" : "Overlay Options");
  set_transient_for(parent);
  set_default_size(680, 480);
  set_size_request(520, 320);
  set_hide_on_close(true);

  sidebar_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  sidebar_.add_css_class("navigation-sidebar");
  sidebar_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (loading_ || !row || row->get_index() == page_) return;
    page_ = row->get_index();
    ShowPage();
  });
  side_scroll_.set_child(sidebar_);
  side_scroll_.set_size_request(180, -1);
  side_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  form_scroll_.set_hexpand(true);
  form_scroll_.set_vexpand(true);
  form_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  panes_.append(side_scroll_);
  panes_.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
  panes_.append(form_scroll_);
  panes_.set_vexpand(true);

  defaults_.signal_clicked().connect([this] { RestoreDefaults(); });
  cancel_.signal_clicked().connect([this] { close(); });
  apply_.signal_clicked().connect([this] { Apply(); });
  ok_.signal_clicked().connect([this] { Ok(); });
  ok_.add_css_class("suggested-action");
  auto* buttons = ButtonRow({&defaults_}, {&cancel_, &apply_, &ok_});
  buttons->set_margin_top(10);
  buttons->set_margin_bottom(14);
  buttons->set_margin_start(20);
  buttons->set_margin_end(20);

  box_.append(panes_);
  box_.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
  box_.append(*buttons);
  set_child(box_);
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.RevertOptions(kind_);
        host_.CloseOptions(kind_);
        return false;
      },
      false);
  Reload();
}

void OptionsWindow::Reload() {
  loading_ = true;
  while (Gtk::Widget* row = sidebar_.get_first_child()) sidebar_.remove(*row);
  const int count = host_.OptionsPageCount(kind_);
  for (int p = 0; p < count; ++p) {
    auto* label = Gtk::make_managed<Gtk::Label>(host_.OptionsPageTitle(kind_, p));
    label->set_xalign(0);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    sidebar_.append(*label);
  }
  if (page_ >= count) page_ = 0;
  if (count > 0) sidebar_.select_row(*sidebar_.get_row_at_index(page_));
  loading_ = false;
  ShowPage();
}

void OptionsWindow::SelectPage(int page) {
  if (Gtk::ListBoxRow* row = sidebar_.get_row_at_index(page)) sidebar_.select_row(*row);
}

/// Rebuilds the form for the current page: one grid of label and control
/// rows, with a heading spanning both columns where a section starts.
void OptionsWindow::ShowPage() {
  controls_.clear();
  field_index_.clear();
  fill_.clear();
  auto* form = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
  form->set_margin_top(16);
  form->set_margin_bottom(16);
  form->set_margin_start(20);
  form->set_margin_end(20);
  if (host_.OptionsPageCount(kind_) == 0) {
    auto* empty = Gtk::make_managed<Gtk::Label>("Nothing here has options yet.");
    empty->set_xalign(0);
    form->append(*empty);
    form_scroll_.set_child(*form);
    UpdateApply();
    return;
  }
  auto* grid = Gtk::make_managed<Gtk::Grid>();
  grid->set_row_spacing(8);
  grid->set_column_spacing(10);
  form->append(*grid);
  int line = 0;
  bool first = true;
  std::string section;
  loading_ = true;
  for (int i = 0; i < host_.OptionsFieldCount(kind_, page_); ++i) {
    const fv::desk::HostOptionsField f = host_.OptionsFieldAt(kind_, page_, i);
    if (first || f.section != section) {
      section = f.section;
      if (!section.empty()) {
        auto* heading = Gtk::make_managed<Gtk::Label>(section);
        heading->set_xalign(0);
        heading->add_css_class("heading");
        if (line > 0) heading->set_margin_top(10);
        grid->attach(*heading, 0, line++, 2, 1);
      }
    }
    first = false;
    Gtk::Widget* control = MakeControl(f, i);
    if (!f.help.empty()) control->set_tooltip_text(f.help);
    controls_[f.key] = control;
    field_index_[f.key] = i;
    if (f.type != fv::desk::kFieldBool) {
      auto* label = Gtk::make_managed<Gtk::Label>(f.label + ":");
      label->set_xalign(1);
      grid->attach(*label, 0, line);
    }
    grid->attach(*control, 1, line++);
    fill_[f.key](f.value);
  }
  loading_ = false;
  form_scroll_.set_child(*form);
  UpdateApply();
}

/// An entry that offers its text to the model on Enter or when it loses
/// focus with a changed value.
Gtk::Entry* OptionsWindow::MakeEntry(const std::string& key) {
  auto* entry = Gtk::make_managed<Gtk::Entry>();
  auto dirty = std::make_shared<bool>(false);
  entry->signal_changed().connect([this, dirty] {
    if (!loading_) *dirty = true;
  });
  auto commit = [this, key, entry, dirty] {
    if (!*dirty) return;
    *dirty = false;
    Set(key, entry->get_text());
  };
  entry->signal_activate().connect(commit);
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect(commit);
  entry->add_controller(focus);
  return entry;
}

/// One control for a field, its filler registered and its edits wired.
Gtk::Widget* OptionsWindow::MakeControl(const fv::desk::HostOptionsField& f, int index) {
  const std::string key = f.key;
  switch (f.type) {
    case fv::desk::kFieldBool: {
      auto* check = Gtk::make_managed<Gtk::CheckButton>(f.label);
      check->signal_toggled().connect([this, key, check] {
        if (!loading_) Set(key, check->get_active() ? "true" : "false");
      });
      fill_[key] = [check](const std::string& v) { check->set_active(v == "true"); };
      return check;
    }
    case fv::desk::kFieldColor: {
      auto dialog = Gtk::ColorDialog::create();
      dialog->set_with_alpha(true);
      auto* well = Gtk::make_managed<Gtk::ColorDialogButton>(dialog);
      well->set_halign(Gtk::Align::START);
      well->property_rgba().signal_changed().connect([this, key, well] {
        if (!loading_) Set(key, ColorText(well->get_rgba()));
      });
      fill_[key] = [well](const std::string& v) { well->set_rgba(ColorOf(v)); };
      return well;
    }
    case fv::desk::kFieldChoice: {
      std::vector<std::string> choices;
      for (int c = 0; c < f.choice_count; ++c) choices.push_back(host_.OptionsChoiceLabel(kind_, page_, index, c));
      auto* popup = Gtk::make_managed<Gtk::DropDown>();
      SetChoices(*popup, choices, -1);
      popup->set_halign(Gtk::Align::START);
      popup->property_selected().signal_changed().connect([this, key, popup] {
        if (!loading_ && SelectedIndex(*popup) >= 0) Set(key, std::to_string(SelectedIndex(*popup)));
      });
      fill_[key] = [popup](const std::string& v) {
        popup->set_selected(static_cast<guint>(std::max(std::atoi(v.c_str()), 0)));
      };
      return popup;
    }
    case fv::desk::kFieldPath: {
      auto* entry = MakeEntry(key);
      entry->set_size_request(260, -1);
      auto* choose = Gtk::make_managed<Gtk::Button>("Choose…");
      choose->signal_clicked().connect([this, key] { ChoosePath(key); });
      auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
      row->append(*entry);
      row->append(*choose);
      fill_[key] = [entry](const std::string& v) { entry->set_text(v); };
      return row;
    }
    case fv::desk::kFieldInt:
      if (f.min != f.max) {
        auto* spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_range(f.min, f.max);
        spin->set_increments(1, 10);
        spin->set_digits(0);
        spin->set_numeric(true);
        spin->set_halign(Gtk::Align::START);
        spin->signal_value_changed().connect([this, key, spin] {
          if (!loading_) Set(key, std::to_string(spin->get_value_as_int()));
        });
        fill_[key] = [spin](const std::string& v) { spin->set_value(std::atof(v.c_str())); };
        return spin;
      }
      [[fallthrough]];
    default: {
      const bool numeric = f.type == fv::desk::kFieldInt || f.type == fv::desk::kFieldDouble;
      auto* entry = MakeEntry(key);
      entry->set_size_request(numeric ? 90 : 260, -1);
      entry->set_halign(Gtk::Align::START);
      fill_[key] = [entry](const std::string& v) { entry->set_text(v); };
      return entry;
    }
  }
}

void OptionsWindow::ChoosePath(const std::string& key) {
  const auto it = field_index_.find(key);
  if (it == field_index_.end()) return;
  const fv::desk::HostOptionsField f = host_.OptionsFieldAt(kind_, page_, it->second);
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  dialog->set_title(f.label);
  if (!f.value.empty()) {
    const std::filesystem::path current(f.value);
    dialog->set_initial_folder(Gio::File::create_for_path(
        (f.directory ? current : current.parent_path()).string()));
  }
  if (!f.directory && !f.path_patterns.empty()) {
    auto filter = Gtk::FileFilter::create();
    filter->set_name(f.path_patterns);
    size_t start = 0;
    while (start <= f.path_patterns.size()) {
      const size_t end = std::min(f.path_patterns.find(';', start), f.path_patterns.size());
      if (end > start) filter->add_pattern(f.path_patterns.substr(start, end - start));
      start = end + 1;
    }
    dialog->set_default_filter(filter);
  }
  const int page = page_;
  auto done = [this, dialog, key, page, directory = f.directory](const Glib::RefPtr<Gio::AsyncResult>& r) {
    try {
      auto file = directory ? dialog->select_folder_finish(r) : dialog->open_finish(r);
      if (file && !file->get_path().empty() && page == page_) Set(key, file->get_path());
    } catch (const Glib::Error&) {
      // dismissed
    }
  };
  if (f.directory) dialog->select_folder(*this, done);
  else dialog->open(*this, done);
}

/// Offers a value to the model, then shows what it kept.
void OptionsWindow::Set(const std::string& key, const std::string& value) {
  if (!host_.SetOptionValue(kind_, page_, key, value).empty()) error_bell();
  const auto index = field_index_.find(key);
  const auto fill = fill_.find(key);
  if (index != field_index_.end() && fill != fill_.end()) {
    loading_ = true;
    fill->second(host_.OptionsFieldAt(kind_, page_, index->second).value);
    loading_ = false;
  }
  UpdateApply();
}

void OptionsWindow::UpdateApply() { apply_.set_sensitive(host_.OptionsDirty(kind_)); }

void OptionsWindow::RestoreDefaults() {
  if (!host_.ResetOptionsPage(kind_, page_).empty()) error_bell();
  ShowPage();
}

bool OptionsWindow::Apply() {
  const std::string error = host_.ApplyOptions(kind_);
  UpdateApply();
  if (error.empty()) return true;
  auto alert = Gtk::AlertDialog::create("The options could not be saved");
  alert->set_detail(error);
  alert->show(*this);
  return false;
}

void OptionsWindow::Ok() {
  if (Apply()) close();
}

Gtk::Widget* OptionsWindow::control(const std::string& key) const {
  const auto it = controls_.find(key);
  return it == controls_.end() ? nullptr : it->second;
}

}  // namespace peregrine
