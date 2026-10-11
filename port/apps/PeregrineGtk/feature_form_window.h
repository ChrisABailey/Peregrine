// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// feature_form_window.h — the feature form (Edit Feature…, Edit Point…),
/// the counterpart of FeatureFormWindow.swift.
#pragma once

#include <gtkmm.h>

#include <string>
#include <vector>

#include "fv_desk_host.h"

namespace peregrine {

/// One row per control of `DeskHost`'s form model, built from the layer's
/// schema. Every edit is pushed to the model as it is typed, so a rebuild
/// loses nothing; Apply writes the feature and OK applies and closes.
/// Closing the window closes the model.
class FeatureFormWindow : public Gtk::Window {
 public:
  /// `FormControl` values (fv_desk_feature_form.h).
  enum Control {
    kText = 0, kMultiline, kInteger, kNumber, kCheckBox, kDate, kDateTime, kDateRange, kChoice,
    kPhone, kUrl, kEmail, kReadOnly
  };

  /// `host` must outlive the window.
  FeatureFormWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Rebuilds the rows from the form model.
  void Reload();
  /// Applies the form; false, with the errors shown, when a value is bad.
  bool Apply();
  /// Applies and closes on success.
  void Ok();
  void Revert();

  /// The main editor of control `i` (an Entry, CheckButton, DropDown,
  /// TextView or Label), or null.
  Gtk::Widget* editor(int i) const;
  /// The message shown under the rows after a failed apply; "" when hidden.
  std::string message() const;

 private:
  Gtk::Widget* MakeControl(int i);
  Gtk::Entry* MakeEntry(int i, bool end, const std::string& text, const std::string& placeholder,
                        int width);
  void ShowErrors();

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 8};
  Gtk::ScrolledWindow scroll_;
  Gtk::Grid* grid_ = nullptr;
  Gtk::Label message_;
  Gtk::Button revert_{"Revert"};
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button apply_{"Apply"};
  Gtk::Button ok_{"OK"};
  std::vector<Gtk::Widget*> editors_;
  std::vector<Gtk::Label*> errors_;
};

}  // namespace peregrine
