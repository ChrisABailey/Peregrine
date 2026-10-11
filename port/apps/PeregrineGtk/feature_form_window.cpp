// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "feature_form_window.h"

#include "dialogs.h"

namespace peregrine {

FeatureFormWindow::FeatureFormWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_transient_for(parent);
  set_default_size(440, 320);
  set_size_request(360, 200);
  set_hide_on_close(true);

  scroll_.set_vexpand(true);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll_.set_propagate_natural_height(true);
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
  box_.append(scroll_);
  box_.append(message_);
  box_.append(*ButtonRow({&revert_}, {&cancel_, &apply_, &ok_}));
  set_child(box_);
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseFeatureForm();
        return false;
      },
      false);
  Reload();
}

void FeatureFormWindow::Reload() {
  set_title(host_.FeatureFormTitle());
  editors_.clear();
  errors_.clear();
  grid_ = Gtk::make_managed<Gtk::Grid>();
  grid_->set_row_spacing(8);
  grid_->set_column_spacing(10);
  const int count = host_.FormFieldCount();
  for (int i = 0; i < count; ++i) {
    auto* label = Gtk::make_managed<Gtk::Label>(host_.FormFieldTitle(i) +
                                                (host_.FormFieldRequired(i) ? " *" : ""));
    label->set_xalign(1);
    label->set_valign(Gtk::Align::START);
    label->set_margin_top(6);
    label->set_tooltip_text(host_.FormFieldDescription(i));
    auto* error = Gtk::make_managed<Gtk::Label>();
    error->set_xalign(0);
    error->add_css_class("error");
    error->add_css_class("caption");
    error->set_visible(false);
    errors_.push_back(error);
    auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    column->append(*MakeControl(i));
    column->append(*error);
    grid_->attach(*label, 0, i);
    grid_->attach(*column, 1, i);
  }
  scroll_.set_child(*grid_);
  ShowErrors();
}

Gtk::Entry* FeatureFormWindow::MakeEntry(int i, bool end, const std::string& text,
                                         const std::string& placeholder, int width) {
  auto* entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(text);
  entry->set_placeholder_text(placeholder);
  entry->set_size_request(width, -1);
  entry->set_activates_default(true);
  entry->signal_changed().connect([this, i, end, entry] {
    if (end) host_.SetFormFieldEndText(i, entry->get_text());
    else host_.SetFormFieldText(i, entry->get_text());
  });
  return entry;
}

/// The editor for control `i`, chosen by its kind.
Gtk::Widget* FeatureFormWindow::MakeControl(int i) {
  const std::string text = host_.FormFieldText(i);
  const int kind = host_.FormFieldControl(i);
  switch (kind) {
    case kCheckBox: {
      auto* box = Gtk::make_managed<Gtk::CheckButton>();
      box->set_active(text == "Yes");
      box->signal_toggled().connect(
          [this, i, box] { host_.SetFormFieldText(i, box->get_active() ? "Yes" : "No"); });
      editors_.push_back(box);
      return box;
    }
    case kChoice: {
      std::vector<Glib::ustring> items;
      if (!host_.FormFieldRequired(i) || text.empty()) items.emplace_back();
      for (int c = 0; c < host_.FormFieldChoiceCount(i); ++c) items.push_back(host_.FormFieldChoice(i, c));
      guint selected = GTK_INVALID_LIST_POSITION;
      for (guint k = 0; k < items.size(); ++k)
        if (items[k].raw() == text) selected = k;
      // A value outside the list stays visible until the user picks one.
      if (selected == GTK_INVALID_LIST_POSITION && !text.empty()) {
        items.push_back(text);
        selected = items.size() - 1;
      }
      auto model = Gtk::StringList::create(items);
      auto* popup = Gtk::make_managed<Gtk::DropDown>(model);
      popup->set_halign(Gtk::Align::START);
      if (selected != GTK_INVALID_LIST_POSITION) popup->set_selected(selected);
      popup->property_selected().signal_changed().connect([this, i, popup, model] {
        const guint k = popup->get_selected();
        host_.SetFormFieldText(i, k < model->get_n_items() ? model->get_string(k) : Glib::ustring());
      });
      editors_.push_back(popup);
      return popup;
    }
    case kMultiline: {
      auto* view = Gtk::make_managed<Gtk::TextView>();
      view->get_buffer()->set_text(text);
      view->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
      view->get_buffer()->signal_changed().connect(
          [this, i, view] { host_.SetFormFieldText(i, view->get_buffer()->get_text()); });
      auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
      scroll->set_child(*view);
      scroll->set_has_frame(true);
      scroll->set_size_request(260, 64);
      editors_.push_back(view);
      return scroll;
    }
    case kDateRange: {
      auto* from = MakeEntry(i, false, text, "YYYY-MM-DD", 110);
      auto* to = MakeEntry(i, true, host_.FormFieldEndText(i), "YYYY-MM-DD", 110);
      auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
      row->append(*from);
      row->append(*Gtk::make_managed<Gtk::Label>("to"));
      row->append(*to);
      editors_.push_back(from);
      return row;
    }
    case kReadOnly: {
      auto* label = Gtk::make_managed<Gtk::Label>(text);
      label->set_xalign(0);
      label->set_selectable(true);
      label->set_margin_top(6);
      editors_.push_back(label);
      return label;
    }
    default: {
      const std::string placeholder =
          kind == kDate ? "YYYY-MM-DD" : kind == kDateTime ? "YYYY-MM-DDThh:mm" : "";
      const bool numeric = kind == kInteger || kind == kNumber || kind == kDate || kind == kDateTime;
      auto* entry = MakeEntry(i, false, text, placeholder, numeric ? 140 : 260);
      auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
      row->append(*entry);
      const std::string unit = host_.FormFieldUnit(i);
      if (!unit.empty()) row->append(*Gtk::make_managed<Gtk::Label>(unit));
      const char* action = kind == kPhone ? "Call" : kind == kUrl ? "Open" : kind == kEmail ? "Email" : nullptr;
      if (action) {
        auto* button = Gtk::make_managed<Gtk::Button>(action);
        button->signal_clicked().connect([this, i] {
          const std::string link = host_.FormFieldActionUrl(i);
          if (link.empty()) {
            error_bell();
            return;
          }
          Gtk::UriLauncher::create(link)->launch(*this, [](const Glib::RefPtr<Gio::AsyncResult>&) {});
        });
        row->append(*button);
      }
      editors_.push_back(entry);
      return row;
    }
  }
}

/// Shows each control's error from the last validation under it.
void FeatureFormWindow::ShowErrors() {
  for (size_t i = 0; i < errors_.size(); ++i) {
    const std::string error = host_.FormFieldError(static_cast<int>(i));
    errors_[i]->set_text(error);
    errors_[i]->set_visible(!error.empty());
  }
  message_.set_text("");
  message_.set_visible(false);
}

bool FeatureFormWindow::Apply() {
  const std::string error = host_.ApplyFeatureForm();
  if (error.empty()) {
    Reload();
    return true;
  }
  ShowErrors();
  message_.set_text(error);
  message_.set_visible(true);
  return false;
}

void FeatureFormWindow::Ok() {
  if (Apply()) close();
}

void FeatureFormWindow::Revert() {
  host_.RevertFeatureForm();
  Reload();
}

Gtk::Widget* FeatureFormWindow::editor(int i) const {
  return i >= 0 && i < static_cast<int>(editors_.size()) ? editors_[i] : nullptr;
}

std::string FeatureFormWindow::message() const {
  return message_.get_visible() ? std::string(message_.get_text()) : std::string();
}

}  // namespace peregrine
