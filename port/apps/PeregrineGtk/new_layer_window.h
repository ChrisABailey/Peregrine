// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// new_layer_window.h — the New Feature Layer dialog, the counterpart of
/// NewLayerWindow.swift.
#pragma once

#include <gtkmm.h>

#include <string>

#include "fv_desk_host.h"

namespace peregrine {

/// Template, geometry, name, format and file over `DeskHost`'s new-layer
/// model. Create writes the empty layer and opens it; the fields are changed
/// afterwards in Layer Fields. Closing the window closes the model.
class NewLayerWindow : public Gtk::Window {
 public:
  /// `host` must outlive the window.
  NewLayerWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  void Reload();
  /// Creates the layer and closes; false, with the message shown, on failure.
  bool Create();

  Gtk::DropDown& template_drop_down() { return template_; }
  Gtk::DropDown& geometry_drop_down() { return geometry_; }
  Gtk::DropDown& format_drop_down() { return format_; }
  Gtk::Entry& name_entry() { return name_; }
  std::string fields_text() const { return fields_.get_text(); }
  /// The message under the controls; "" when hidden.
  std::string message() const;

 private:
  void Report(const std::string& error);
  void ChoosePath();

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 12};
  Gtk::DropDown template_;
  Gtk::DropDown geometry_;
  Gtk::Entry name_;
  Gtk::DropDown format_;
  Gtk::Label path_;
  Gtk::Button choose_{"Choose…"};
  Gtk::Label fields_;
  Gtk::Label message_;
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button create_{"Create"};
  /// Set while controls are filled from the model, so their signals are not edits.
  bool loading_ = false;
};

}  // namespace peregrine
