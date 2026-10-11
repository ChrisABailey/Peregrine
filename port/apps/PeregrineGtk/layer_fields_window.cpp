// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "layer_fields_window.h"

#include <algorithm>

#include "dialogs.h"

namespace peregrine {

LayerFieldsWindow::LayerFieldsWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_transient_for(parent);
  set_default_size(640, 480);
  set_size_request(560, 420);
  set_hide_on_close(true);
  confirm_ = [this](const std::string& loss) {
    ChoiceDialog ask(*this, "Change the layer’s fields?", loss, {"Cancel", "Change Fields"}, 1, 0, 1);
    return ask.Run() == 1;
  };

  list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  list_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
    if (loading_ || !row) return;
    if (editor_dirty_) CommitEditor();
    selected_ = row->get_index();
    message_.set_visible(false);
    ReloadEditor();
  });
  list_scroll_.set_child(list_);
  list_scroll_.set_has_frame(true);
  list_scroll_.set_vexpand(true);
  list_scroll_.set_size_request(270, 300);
  list_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  add_.signal_clicked().connect([this] { AddField(); });
  remove_.signal_clicked().connect([this] {
    if (selected() < 0) {
      error_bell();
      return;
    }
    Report(host_.RemoveLayerField(selected()));
  });
  up_.signal_clicked().connect([this] { Move(-1); });
  down_.signal_clicked().connect([this] { Move(1); });
  auto* list_buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  for (Gtk::Button* b : {&add_, &remove_, &up_, &down_}) list_buttons->append(*b);
  auto* list_column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  list_column->append(list_scroll_);
  list_column->append(*list_buttons);

  std::vector<std::string> types, roles;
  for (int k = 0; k < host_.FieldTypeCount(); ++k) types.push_back(host_.FieldTypeTitle(k));
  for (int k = 0; k < host_.FieldRoleCount(); ++k) roles.push_back(host_.FieldRoleTitle(k));
  SetChoices(type_, types, 0);
  SetChoices(role_, roles, 0);
  for (Gtk::DropDown* d : {&type_, &role_}) {
    d->set_halign(Gtk::Align::START);
    d->property_selected().signal_changed().connect([this] {
      if (!loading_) CommitEditor();
    });
  }
  required_.signal_toggled().connect([this] {
    if (!loading_) CommitEditor();
  });
  for (Gtk::Entry* e : {&name_, &title_, &unit_, &default_, &min_, &max_, &description_}) WatchEntry(*e);
  choices_.set_wrap_mode(Gtk::WrapMode::NONE);
  choices_.get_buffer()->signal_changed().connect([this] {
    if (!loading_) editor_dirty_ = true;
  });
  auto choices_focus = Gtk::EventControllerFocus::create();
  choices_focus->signal_leave().connect([this] {
    if (editor_dirty_) CommitEditor();
  });
  choices_.add_controller(choices_focus);
  choices_scroll_.set_child(choices_);
  choices_scroll_.set_has_frame(true);
  choices_scroll_.set_size_request(-1, 80);
  origin_.set_xalign(0);
  origin_.add_css_class("dim-label");

  editor_.set_row_spacing(6);
  editor_.set_column_spacing(8);
  editor_.set_hexpand(true);
  AddRow("Name", name_);
  AddRow("Title", title_);
  AddRow("Type", type_);
  AddRow("Meaning", role_);
  AddRow("Unit", unit_);
  AddRow("", required_);
  AddRow("Default", default_);
  AddRow("Minimum", min_);
  AddRow("Maximum", max_);
  AddRow("Choices", choices_scroll_);
  AddRow("Description", description_);
  AddRow("", origin_);

  auto* middle = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
  middle->set_vexpand(true);
  middle->append(*list_column);
  middle->append(editor_);

  message_.set_xalign(0);
  message_.set_wrap(true);
  message_.add_css_class("error");
  message_.set_visible(false);
  revert_.signal_clicked().connect([this] { Revert(); });
  cancel_.signal_clicked().connect([this] { close(); });
  apply_.signal_clicked().connect([this] { Apply(); });
  ok_.signal_clicked().connect([this] { Ok(); });
  ok_.add_css_class("suggested-action");

  SetDialogMargins(box_);
  box_.append(*middle);
  box_.append(message_);
  box_.append(*ButtonRow({&revert_}, {&cancel_, &apply_, &ok_}));
  set_child(box_);
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseLayerFields();
        return false;
      },
      false);
  Reload();
}

void LayerFieldsWindow::AddRow(const std::string& title, Gtk::Widget& control) {
  auto* label = Gtk::make_managed<Gtk::Label>(title);
  label->set_xalign(1);
  label->set_valign(Gtk::Align::START);
  label->set_margin_top(6);
  editor_.attach(*label, 0, editor_rows_);
  editor_.attach(control, 1, editor_rows_++);
  control.set_hexpand(true);
  row_labels_.emplace_back(&control, label);
}

void LayerFieldsWindow::ShowRow(Gtk::Widget& control, bool shown) {
  control.set_visible(shown);
  for (auto& [c, label] : row_labels_)
    if (c == &control) label->set_visible(shown);
}

/// Marks the editor dirty as the entry is typed in; Enter or leaving the
/// entry writes it to the model.
void LayerFieldsWindow::WatchEntry(Gtk::Entry& entry) {
  entry.signal_changed().connect([this] {
    if (!loading_) editor_dirty_ = true;
  });
  entry.signal_activate().connect([this] { CommitEditor(); });
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect([this] {
    if (editor_dirty_) CommitEditor();
  });
  entry.add_controller(focus);
}

void LayerFieldsWindow::Reload() {
  reload_pending_ = false;
  loading_ = true;
  set_title(host_.LayerFieldsTitle());
  while (Gtk::Widget* row = list_.get_first_child()) list_.remove(*row);
  const int count = host_.LayerFieldCount();
  for (int i = 0; i < count; ++i) {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* name = Gtk::make_managed<Gtk::Label>(host_.LayerFieldName(i));
    name->set_xalign(0);
    name->set_hexpand(true);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    const int type = host_.LayerFieldType(i);
    auto* kind = Gtk::make_managed<Gtk::Label>(type >= 0 ? host_.FieldTypeTitle(type) : "Binary");
    kind->add_css_class("dim-label");
    row->append(*name);
    row->append(*kind);
    list_.append(*row);
  }
  selected_ = std::min(std::max(selected_, 0), count - 1);
  if (selected_ >= 0) list_.select_row(*list_.get_row_at_index(selected_));
  loading_ = false;
  ReloadEditor();
}

/// Fills the editor from the selected field; rows that do not apply to its
/// type or meaning are hidden.
void LayerFieldsWindow::ReloadEditor() {
  const int i = selected();
  editor_.set_visible(i >= 0);
  remove_.set_sensitive(i >= 0);
  up_.set_sensitive(i > 0);
  down_.set_sensitive(i >= 0 && i + 1 < host_.LayerFieldCount());
  editor_dirty_ = false;
  if (i < 0) return;
  loading_ = true;
  name_.set_text(host_.LayerFieldName(i));
  title_.set_text(host_.LayerFieldTitle(i));
  const int type = host_.LayerFieldType(i);
  type_.set_sensitive(type >= 0);
  if (type >= 0) type_.set_selected(static_cast<guint>(type));
  const int role = host_.LayerFieldRole(i);
  role_.set_selected(static_cast<guint>(std::max(role, 0)));
  unit_.set_text(host_.LayerFieldUnit(i));
  required_.set_active(host_.LayerFieldRequired(i));
  default_.set_text(host_.LayerFieldDefault(i));
  min_.set_text(host_.LayerFieldMin(i));
  max_.set_text(host_.LayerFieldMax(i));
  choices_.get_buffer()->set_text(host_.LayerFieldChoices(i));
  description_.set_text(host_.LayerFieldDescription(i));
  const std::string original = host_.LayerFieldOriginalName(i);
  origin_.set_text(original.empty() ? "New field" : "Was “" + original + "”");
  const bool numeric = type == 1 || type == 2;
  ShowRow(unit_, role == 5);
  ShowRow(min_, numeric);
  ShowRow(max_, numeric);
  ShowRow(choices_scroll_, role == 4);
  loading_ = false;
}

int LayerFieldsWindow::selected() const {
  return selected_ >= 0 && selected_ < host_.LayerFieldCount() ? selected_ : -1;
}

void LayerFieldsWindow::SelectField(int i) {
  if (Gtk::ListBoxRow* row = list_.get_row_at_index(i)) list_.select_row(*row);
}

void LayerFieldsWindow::CommitEditor() {
  const int i = selected();
  editor_dirty_ = false;
  if (i < 0) return;
  Report(host_.SetLayerField(i, name_.get_text(), SelectedIndex(type_), SelectedIndex(role_),
                             title_.get_text(), description_.get_text(), unit_.get_text(),
                             required_.get_active(), default_.get_text(), min_.get_text(),
                             max_.get_text(), choices_.get_buffer()->get_text()));
}

void LayerFieldsWindow::AddField() {
  if (editor_dirty_) CommitEditor();
  const int i = host_.AddLayerField();
  if (i >= 0) selected_ = i;
  Reload();
  name_.grab_focus();
}

void LayerFieldsWindow::Move(int step) {
  const int from = selected();
  if (from < 0) return;
  if (editor_dirty_) CommitEditor();
  const std::string error = host_.MoveLayerField(from, from + step);
  if (error.empty()) selected_ = from + step;
  Report(error);
}

/// Shows `error` (or clears the message) and re-reads the model once the
/// signal that made the edit has returned.
void LayerFieldsWindow::Report(const std::string& error) {
  message_.set_text(error);
  message_.set_visible(!error.empty());
  if (reload_pending_) return;
  reload_pending_ = true;
  Glib::signal_idle().connect_once(sigc::mem_fun(*this, &LayerFieldsWindow::ReloadIfPending));
}

void LayerFieldsWindow::ReloadIfPending() {
  if (reload_pending_) Reload();
}

bool LayerFieldsWindow::Apply() {
  if (editor_dirty_) CommitEditor();
  if (message_.get_visible()) return false;
  const std::string loss = host_.LayerFieldsLoss();
  if (!loss.empty() && !(confirm_ && confirm_(loss))) return false;
  const std::string error = host_.ApplyLayerFields();
  Report(error);
  return error.empty();
}

void LayerFieldsWindow::Ok() {
  if (Apply()) close();
}

void LayerFieldsWindow::Revert() { Report(host_.RevertLayerFields()); }

std::string LayerFieldsWindow::message() const {
  return message_.get_visible() ? std::string(message_.get_text()) : std::string();
}

}  // namespace peregrine
