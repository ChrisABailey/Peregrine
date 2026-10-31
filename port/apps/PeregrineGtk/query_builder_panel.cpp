// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "query_builder_panel.h"

#include <algorithm>
#include <sstream>

#include "dialogs.h"

namespace peregrine {
namespace {

/// Calls `commit` when `w` loses the keyboard focus.
void OnFocusLeave(Gtk::Widget& w, std::function<void()> commit) {
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect(std::move(commit));
  w.add_controller(focus);
}

}  // namespace

QueryBuilderPanel::QueryBuilderPanel(fv::desk::DeskHost& host, int which, ReportFn report)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 8), host_(host), which_(which), report_(std::move(report)) {
  SetChoices(match_, {"all of the clauses (and)", "any of the clauses (or)"}, 0);
  match_.property_selected().signal_changed().connect([this] {
    if (loading_) return;
    host_.SetQueryMatchAny(which_, match_.get_selected() == 1);
    Edited("");
  });
  auto* match_label = Gtk::make_managed<Gtk::Label>("Features matching");
  auto* match_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  match_row->append(*match_label);
  match_row->append(match_);

  custom_note_.set_text(
      "The query is an expression the builder cannot show as clauses. Edit it below, or add a clause to start over.");
  custom_note_.set_wrap(true);
  custom_note_.set_xalign(0);
  custom_note_.add_css_class("dim-label");

  rows_box_.set_margin_top(8);
  rows_box_.set_margin_bottom(8);
  rows_box_.set_margin_start(8);
  rows_box_.set_margin_end(8);
  rows_scroll_.set_child(rows_box_);
  rows_scroll_.set_has_frame(true);
  rows_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
  rows_scroll_.set_size_request(-1, 120);
  rows_scroll_.set_vexpand(true);
  add_.set_halign(Gtk::Align::START);
  add_.signal_clicked().connect([this] { AddClause(); });

  sentence_.set_wrap(true);
  sentence_.set_xalign(0);
  sentence_.set_selectable(true);
  sentence_.add_css_class("dim-label");
  sentence_.set_margin_bottom(6);
  auto* expression_label = Gtk::make_managed<Gtk::Label>("Expression");
  expression_label->set_xalign(0);
  expression_label->add_css_class("heading");
  expression_.set_placeholder_text("No query: every feature");
  expression_.add_css_class("monospace");
  expression_.signal_changed().connect([this] {
    if (!loading_) expression_dirty_ = true;
  });
  expression_.signal_activate().connect([this] { CommitExpression(); });
  OnFocusLeave(expression_, [this] {
    if (expression_dirty_) CommitExpression();
  });
  verify_button_.signal_clicked().connect([this] { Verify(); });
  verify_.set_xalign(0);
  verify_.set_ellipsize(Pango::EllipsizeMode::END);
  verify_.set_hexpand(true);
  auto* verify_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  verify_row->append(verify_button_);
  verify_row->append(verify_);

  append(*match_row);
  append(custom_note_);
  append(rows_scroll_);
  append(add_);
  append(sentence_);
  append(*expression_label);
  append(expression_);
  append(*verify_row);
  Reload();
}

/// A clause row over clause `i`: field, comparison, value with the field's
/// sample values in a menu, and −.
QueryBuilderPanel::ClauseRow QueryBuilderPanel::MakeRow(int i) {
  ClauseRow row;
  row.box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  std::vector<std::string> titles;
  for (int f = 0; f < host_.QueryFieldCount(which_); ++f) titles.push_back(host_.QueryFieldTitle(which_, f));
  row.field = Gtk::make_managed<Gtk::DropDown>();
  SetChoices(*row.field, titles, -1);
  std::vector<std::string> ops;
  for (int k = 0; k < host_.QueryOpCount(); ++k) ops.push_back(host_.QueryOpTitle(k));
  row.op = Gtk::make_managed<Gtk::DropDown>();
  SetChoices(*row.op, ops, 0);
  row.value = Gtk::make_managed<Gtk::Entry>();
  row.value->set_size_request(180, -1);
  row.value->set_hexpand(true);
  row.values = Gtk::make_managed<Gtk::MenuButton>();
  row.values->set_icon_name("pan-down-symbolic");
  row.values->set_tooltip_text("Values of the field");
  auto* remove = Gtk::make_managed<Gtk::Button>("−");
  row.box->append(*row.field);
  row.box->append(*row.op);
  row.box->append(*row.value);
  row.box->append(*row.values);
  row.box->append(*remove);

  row.field->property_selected().signal_changed().connect([this, i] {
    if (loading_ || i >= clause_count()) return;
    FillValues(rows_[static_cast<size_t>(i)]);
    CommitClause(i);
  });
  row.op->property_selected().signal_changed().connect([this, i] {
    if (!loading_ && i < clause_count()) CommitClause(i);
  });
  row.value->signal_changed().connect([this, i] {
    if (!loading_ && i < clause_count()) rows_[static_cast<size_t>(i)].dirty = true;
  });
  row.value->signal_activate().connect([this, i] { CommitClause(i); });
  OnFocusLeave(*row.value, [this, i] {
    if (i < clause_count() && rows_[static_cast<size_t>(i)].dirty) CommitClause(i);
  });
  // The row is rebuilt after the click has returned: the rebuild destroys the button.
  // A slot on the panel, so a panel destroyed first takes the idle call with it.
  remove->signal_clicked().connect([this, i] {
    Glib::signal_idle().connect_once(sigc::bind(sigc::mem_fun(*this, &QueryBuilderPanel::RemoveClause), i));
  });
  return row;
}

void QueryBuilderPanel::Reload() {
  loading_ = true;
  while (Gtk::Widget* child = rows_box_.get_first_child()) rows_box_.remove(*child);
  rows_.clear();
  const bool open = host_.QueryOpen(which_);
  set_visible(open);
  if (!open) {
    loading_ = false;
    return;
  }
  field_names_.clear();
  for (int f = 0; f < host_.QueryFieldCount(which_); ++f) field_names_.push_back(host_.QueryFieldName(which_, f));
  match_.set_selected(host_.QueryMatchAny(which_) ? 1 : 0);
  custom_note_.set_visible(host_.QueryIsCustom(which_));
  for (int i = 0; i < host_.QueryClauseCount(which_); ++i) {
    ClauseRow row = MakeRow(i);
    const std::string field = host_.QueryClauseField(which_, i);
    for (size_t f = 0; f < field_names_.size(); ++f)
      if (field_names_[f] == field) row.field->set_selected(static_cast<guint>(f));
    row.op->set_selected(static_cast<guint>(std::max(0, host_.QueryClauseOp(which_, i))));
    row.value->set_text(host_.QueryClauseValue(which_, i));
    FillValues(row);
    rows_box_.append(*row.box);
    rows_.push_back(row);
  }
  loading_ = false;
  ReloadText();
}

/// Re-reads the sentence and the expression, leaving the rows as they are.
void QueryBuilderPanel::ReloadText() {
  const bool custom = host_.QueryIsCustom(which_);
  const std::string sentence = host_.QueryText(which_);
  sentence_.set_text(custom ? "" : (sentence.empty() ? "Every feature" : sentence));
  sentence_.set_visible(!custom);
  const std::string error = host_.QueryExpressionError(which_);
  // An incomplete clause has no expression; keep the last one shown.
  if (error.empty()) {
    loading_ = true;
    expression_.set_text(host_.QueryExpression(which_));
    loading_ = false;
    expression_dirty_ = false;
  }
  verify_.set_text(error);
  if (error.empty()) verify_.remove_css_class("error");
  else verify_.add_css_class("error");
  for (ClauseRow& row : rows_) {
    const bool takes = host_.QueryOpTakesValue(static_cast<int>(row.op->get_selected()));
    row.value->set_sensitive(takes);
    row.values->set_sensitive(takes && row.values->get_popover() != nullptr);
  }
}

/// Puts the selected field's sample values in the row's value menu.
void QueryBuilderPanel::FillValues(ClauseRow& row) {
  const guint f = row.field->get_selected();
  row.values->unset_popover();
  if (f == GTK_INVALID_LIST_POSITION || f >= field_names_.size()) return;
  const std::string values = host_.QuerySampleValues(which_, field_names_[f]);
  if (values.empty()) return;
  auto* list = Gtk::make_managed<Gtk::ListBox>();
  list->set_selection_mode(Gtk::SelectionMode::NONE);
  std::istringstream lines(values);
  for (std::string line; std::getline(lines, line);) {
    auto* label = Gtk::make_managed<Gtk::Label>(line);
    label->set_xalign(0);
    list->append(*label);
  }
  list->set_activate_on_single_click(true);
  Gtk::Entry* entry = row.value;
  auto* popover = Gtk::make_managed<Gtk::Popover>();
  list->signal_row_activated().connect([this, entry, popover](Gtk::ListBoxRow* r) {
    if (auto* label = dynamic_cast<Gtk::Label*>(r->get_child())) entry->set_text(label->get_text());
    popover->popdown();
    for (int i = 0; i < clause_count(); ++i)
      if (rows_[static_cast<size_t>(i)].value == entry) CommitClause(i);
  });
  auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_child(*list);
  scroll->set_propagate_natural_height(true);
  scroll->set_max_content_height(300);
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  popover->set_child(*scroll);
  row.values->set_popover(*popover);
  row.values->set_sensitive(host_.QueryOpTakesValue(static_cast<int>(row.op->get_selected())));
}

void QueryBuilderPanel::Edited(const std::string& error) {
  if (report_) report_(error);
  ReloadText();
  if (on_change_) on_change_();
}

void QueryBuilderPanel::AddClause() {
  CommitPending();
  host_.AddQueryClause(which_);
  Reload();
  if (on_change_) on_change_();
}

void QueryBuilderPanel::RemoveClause(int i) {
  if (i < 0 || i >= clause_count()) return;
  const std::string error = host_.RemoveQueryClause(which_, i);
  Reload();
  Edited(error);
}

void QueryBuilderPanel::CommitClause(int i) {
  if (i < 0 || i >= clause_count()) return;
  ClauseRow& row = rows_[static_cast<size_t>(i)];
  row.dirty = false;
  const guint f = row.field->get_selected();
  const std::string field = f < field_names_.size() ? field_names_[f] : std::string();
  Edited(host_.SetQueryClause(which_, i, field, static_cast<int>(row.op->get_selected()), row.value->get_text()));
}

void QueryBuilderPanel::CommitExpression() {
  expression_dirty_ = false;
  const std::string error = host_.SetQueryExpression(which_, expression_.get_text());
  Reload();
  Edited(error);
}

void QueryBuilderPanel::CommitPending() {
  for (int i = 0; i < clause_count(); ++i)
    if (rows_[static_cast<size_t>(i)].dirty) CommitClause(i);
  if (expression_dirty_) CommitExpression();
}

void QueryBuilderPanel::Verify() {
  CommitPending();
  const std::string result = host_.VerifyQuery(which_);
  verify_.set_text(result);
  if (result.find(" match") != std::string::npos) verify_.remove_css_class("error");
  else verify_.add_css_class("error");
}

}  // namespace peregrine
