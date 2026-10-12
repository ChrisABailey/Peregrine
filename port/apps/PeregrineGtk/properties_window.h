// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// properties_window.h — the Properties dialog of a layer or a source.
#pragma once

#include <gtkmm.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fv_desk_host.h"
#include "layer_fields_panel.h"
#include "layer_style_window.h"

namespace peregrine {

/// One page per tab `DeskHost` lists: Source (a heading per section and one
/// selectable line per property), General (name, description, credits, scale
/// range), Symbology and Labels (the style panel) and Fields (the fields
/// panel). Apply writes every tab, OK applies and closes, Revert drops the
/// edits; a source's sheet has the Source page alone and no buttons. Closing
/// the window closes the tabs' models.
class PropertiesWindow : public Gtk::Window {
 public:
  /// Asked before an apply that loses field values, with the loss as
  /// sentences; true to go ahead. The default asks with a modal dialog.
  using ConfirmFn = std::function<bool(const std::string& loss)>;

  /// `host` must outlive the window.
  PropertiesWindow(fv::desk::DeskHost& host, Gtk::Window& parent);

  /// Rebuilds every page from the host and shows the tab it asks for.
  void Reload();
  /// Rebuilds the pages when the host's tabs changed (a style tab taken by
  /// Style This Feature…), keeping the page shown. False when the layer the
  /// dialog was open on has gone.
  bool Refresh();
  /// Shows the page of `tab`, a `fv::desk::PropertiesTab`; false without one.
  bool ShowTab(int tab);
  /// The shown page's `fv::desk::PropertiesTab`, or -1.
  int current_tab() const;
  /// Applies every tab, asking first when field values would be lost; false
  /// when it was refused, cancelled or failed with the message shown.
  bool Apply();
  void Ok();
  void Revert();
  void set_confirm(ConfirmFn confirm) { confirm_ = std::move(confirm); }

  Gtk::Notebook& notebook() { return notebook_; }
  /// The Source page's rows.
  Gtk::Grid& grid() { return grid_; }
  Gtk::Entry& name_entry() { return name_; }
  Gtk::TextView& description_view() { return description_; }
  Gtk::Entry& credits_entry() { return credits_; }
  Gtk::Entry& min_scale_entry() { return min_scale_; }
  Gtk::Entry& max_scale_entry() { return max_scale_; }
  /// Null while the tab is not shown.
  LayerStylePanel* symbology() { return symbology_; }
  LayerStylePanel* labels() { return labels_; }
  LayerFieldsPanel* fields() { return fields_; }
  /// The message under the pages; "" when hidden.
  std::string message() const;

 private:
  void BuildGeneral();
  void ReloadSource();
  void ReloadGeneral();
  /// Writes the General page to the draft; false, with the message shown, when refused.
  bool CommitGeneral();
  void Report(const std::string& error);
  std::vector<int> HostTabs() const;

  fv::desk::DeskHost& host_;
  Gtk::Box box_{Gtk::Orientation::VERTICAL, 10};
  Gtk::Notebook notebook_;
  Gtk::ScrolledWindow source_page_;
  Gtk::Grid grid_;
  Gtk::Grid general_page_;
  Gtk::Entry name_;
  Gtk::ScrolledWindow description_scroll_;
  Gtk::TextView description_;
  Gtk::Entry credits_;
  Gtk::Entry min_scale_;
  Gtk::Entry max_scale_;
  Gtk::Button min_current_{"Current"};
  Gtk::Button max_current_{"Current"};
  LayerStylePanel* symbology_ = nullptr;
  LayerStylePanel* labels_ = nullptr;
  LayerFieldsPanel* fields_ = nullptr;
  /// The tabs the pages were built for.
  std::vector<int> tabs_;
  Gtk::Label message_;
  Gtk::Box* buttons_ = nullptr;
  Gtk::Button revert_{"Revert"};
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button apply_{"Apply"};
  Gtk::Button ok_{"OK"};
  ConfirmFn confirm_;
  /// Set while the General page is filled from the model, so its signals are not edits.
  bool loading_ = false;
  /// A General control changed since the page was last written to the draft.
  bool general_dirty_ = false;
};

}  // namespace peregrine
