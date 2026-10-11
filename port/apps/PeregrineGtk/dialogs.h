// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// dialogs.h — answers to the core's questions and the progress dialog of a
/// background job, the counterpart of Dialogs.swift.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// Answers the host's pending request (`DeskHost::PendingRequest`) with a
/// modal dialog over `parent`. Runs a nested main loop until the user
/// answers, since the host expects the answer before its call returns.
void AnswerRequest(fv::desk::DeskHost& host, Gtk::Window& parent);

/// The host's answer to a save prompt (0 save, 1 discard, 2 cancel) for
/// the prompt's button `index`, left to right Don't Save, Cancel, Save.
int AskSaveAnswer(int index);

/// The file filters of the pending file request, one per filter item, in
/// order; a "*" or "*.*" pattern matches every file. Empty when the request
/// has no filters.
Glib::RefPtr<Gio::ListStore<Gtk::FileFilter>> RequestFilters(const fv::desk::DeskHost& host);

/// Makes Escape close `window`, as its close button would.
void CloseOnEscape(Gtk::Window& window);

/// A row of dialog buttons: `leading` at the left, then a gap, then
/// `trailing` left to right.
Gtk::Box* ButtonRow(const std::vector<Gtk::Widget*>& leading,
                    const std::vector<Gtk::Widget*>& trailing);

/// Sets the 16/20 pt margins dialogs use around their content.
void SetDialogMargins(Gtk::Widget& content);

/// Replaces `drop_down`'s items with `items`; nothing is selected when
/// `selected` is out of range.
void SetChoices(Gtk::DropDown& drop_down, const std::vector<std::string>& items, int selected);

/// The selected item of a drop-down as an index, -1 when none.
int SelectedIndex(const Gtk::DropDown& drop_down);

/// A modal message with buttons and, optionally, a drop-down of rows. Closing
/// the window or pressing Escape answers `cancel_button`.
class ChoiceDialog : public Gtk::Window {
 public:
  /// @param buttons labels left to right; `default_button` is activated by
  ///   Enter and styled as the suggested action, `destructive_button` (or -1)
  ///   as a destructive one.
  ChoiceDialog(Gtk::Window& parent, const std::string& message, const std::string& detail,
               const std::vector<std::string>& buttons, int default_button, int cancel_button,
               int destructive_button = -1);

  /// Adds a drop-down of `rows` under the message, the first row selected.
  void SetRows(const std::vector<std::string>& rows);
  /// The selected row, or -1 when there are none.
  int selected_row() const;
  /// Ends the dialog with button `index`, as a click would.
  void Respond(int index);

  /// Shows the dialog and runs a nested main loop until it is answered;
  /// returns the button index.
  int Run();

 private:
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 10};
  Gtk::Label message_;
  Gtk::Label detail_;
  Gtk::DropDown* rows_ = nullptr;
  Gtk::Box buttons_{Gtk::Orientation::HORIZONTAL, 8};
  int cancel_button_ = 0;
  std::optional<int> response_;
  Glib::RefPtr<Glib::MainLoop> loop_;
};

/// A modal dialog showing a background job's progress, with a Cancel button.
class JobDialog : public Gtk::Window {
 public:
  JobDialog(Gtk::Window& parent, const std::string& title, std::function<void()> on_cancel);

  /// @param fraction 0…1.
  void Update(double fraction, const std::string& text);

 private:
  void Cancel();

  Gtk::Box box_{Gtk::Orientation::VERTICAL, 8};
  Gtk::Label heading_;
  Gtk::ProgressBar bar_;
  Gtk::Label text_;
  Gtk::Button cancel_{"Cancel"};
  std::function<void()> on_cancel_;
  bool cancelled_ = false;
};

}  // namespace peregrine
