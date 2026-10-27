// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// query_builder_panel.h — the query builder, the counterpart of
/// QueryBuilderPanel.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <string>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// The query builder over one of `DeskHost`'s query models: clauses of field,
/// comparison and value joined by all or any, the query as a sentence, its
/// filter expression (editable by hand), and Verify. Edits go to the model's
/// draft as they are made; the owning dialog applies them. Hidden while the
/// model is closed.
class QueryBuilderPanel : public Gtk::Box {
 public:
  /// Receives each edit's error message, "" for success.
  using ReportFn = std::function<void(const std::string& error)>;

  /// `host` must outlive the panel; `which` is `DeskHost::kQueryDefinition`,
  /// `kQuerySelect` or `kQueryRule`.
  QueryBuilderPanel(fv::desk::DeskHost& host, int which, ReportFn report);

  /// Called after every edit of the draft.
  void set_on_change(std::function<void()> on_change) { on_change_ = std::move(on_change); }

  /// Rebuilds the clause rows and the expression from the model.
  void Reload();
  /// Writes a value or expression being typed to the draft; the dialog calls
  /// it before applying.
  void CommitPending();
  /// Adds a clause, as Add Clause does.
  void AddClause();
  /// Removes clause `i`, as its − button does.
  void RemoveClause(int i);
  /// Writes clause `i`'s row to the draft.
  void CommitClause(int i);
  /// Verifies the query and shows the answer beside the button.
  void Verify();

  int clause_count() const { return static_cast<int>(rows_.size()); }
  Gtk::DropDown& match_drop_down() { return match_; }
  Gtk::DropDown& field_drop_down(int i) { return *rows_[static_cast<size_t>(i)].field; }
  Gtk::DropDown& op_drop_down(int i) { return *rows_[static_cast<size_t>(i)].op; }
  Gtk::Entry& value_entry(int i) { return *rows_[static_cast<size_t>(i)].value; }
  Gtk::Entry& expression_entry() { return expression_; }
  std::string sentence_text() const { return sentence_.get_visible() ? sentence_.get_text() : ""; }
  std::string verify_text() const { return verify_.get_text(); }

 private:
  /// One clause's controls, owned by the rows box.
  struct ClauseRow {
    Gtk::Box* box = nullptr;
    Gtk::DropDown* field = nullptr;
    Gtk::DropDown* op = nullptr;
    Gtk::Entry* value = nullptr;
    Gtk::MenuButton* values = nullptr;
    /// The value was typed since the row was last written.
    bool dirty = false;
  };

  ClauseRow MakeRow(int i);
  void ReloadText();
  void FillValues(ClauseRow& row);
  void Edited(const std::string& error);
  void CommitExpression();

  fv::desk::DeskHost& host_;
  const int which_;
  ReportFn report_;
  std::function<void()> on_change_;
  Gtk::DropDown match_;
  Gtk::Label custom_note_;
  Gtk::ScrolledWindow rows_scroll_;
  Gtk::Box rows_box_{Gtk::Orientation::VERTICAL, 6};
  Gtk::Button add_{"Add Clause"};
  Gtk::Label sentence_;
  Gtk::Entry expression_;
  Gtk::Button verify_button_{"Verify"};
  Gtk::Label verify_;
  std::vector<ClauseRow> rows_;
  /// The field names behind each field drop-down's items.
  std::vector<std::string> field_names_;
  /// Set while controls are filled from the model, so their signals are not edits.
  bool loading_ = false;
  /// The expression was typed since it was last written.
  bool expression_dirty_ = false;
};

}  // namespace peregrine
