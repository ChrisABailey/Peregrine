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
#include "query_builder_panel.h"

namespace peregrine {

/// One page per tab `DeskHost` lists: Source (a heading per section and one
/// selectable line per property), General (name, description, credits, scale
/// range), Display (opacity, map tip and hyperlink fields), Symbology and
/// Labels (the style panel), Fields (the fields panel), Selection (the
/// selection colour) and Definition Query (the query builder). Apply writes every tab, OK applies and closes, Revert drops the
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
  QueryBuilderPanel* definition_query() { return definition_query_; }
  Gtk::Scale& opacity_scale() { return opacity_; }
  /// "80%" beside the opacity slider.
  std::string opacity_text() const { return opacity_label_.get_text(); }
  Gtk::DropDown& map_tip_drop_down() { return map_tip_; }
  Gtk::DropDown& hyperlink_drop_down() { return hyperlink_; }
  Gtk::CheckButton& default_selection_button() { return default_selection_; }
  Gtk::CheckButton& own_selection_button() { return own_selection_; }
  Gtk::ColorDialogButton& selection_color_button() { return *selection_color_; }
  Gtk::Entry& selection_width_entry() { return selection_width_; }
  /// "1 feature is selected."
  std::string selection_count_text() const { return selection_count_.get_text(); }
  /// The message under the pages; "" when hidden.
  std::string message() const;

 private:
  void BuildGeneral();
  void BuildDisplay();
  void BuildSelection();
  void ReloadSource();
  void ReloadGeneral();
  void ReloadDisplay();
  void ReloadSelection();
  /// Writes the Display page to the draft.
  void CommitDisplay();
  /// Writes the Selection page to the draft; false, with the message shown, when refused.
  bool CommitSelection();
  /// Re-reads every page's controls from the drafts, keeping the pages.
  void ReloadPages();
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
  QueryBuilderPanel* definition_query_ = nullptr;
  Gtk::Box display_page_{Gtk::Orientation::VERTICAL, 16};
  Gtk::Box opacity_row_{Gtk::Orientation::HORIZONTAL, 8};
  Gtk::Scale opacity_;
  Gtk::Label opacity_label_;
  Gtk::Grid display_fields_;
  Gtk::DropDown map_tip_;
  Gtk::DropDown hyperlink_;
  /// The field names behind the map tip and hyperlink items; item 0 is "".
  std::vector<std::string> display_field_names_;
  Gtk::Box selection_page_{Gtk::Orientation::VERTICAL, 10};
  Gtk::CheckButton default_selection_{"Use the default selection colour"};
  Gtk::CheckButton own_selection_{"Use this colour:"};
  Gtk::ColorDialogButton* selection_color_ = nullptr;
  Gtk::Entry selection_width_;
  Gtk::Label selection_count_;
  Gtk::Button select_by_attributes_{"Select by Attributes…"};  /// The tabs the host offered when the pages were built.
  std::vector<int> host_tabs_;
  /// The tab of each page, in page order; a tab without a page here is left out.
  std::vector<int> tabs_;
  Gtk::Label message_;
  Gtk::Box* buttons_ = nullptr;
  Gtk::Button revert_{"Revert"};
  Gtk::Button cancel_{"Cancel"};
  Gtk::Button apply_{"Apply"};
  Gtk::Button ok_{"OK"};
  ConfirmFn confirm_;
  /// Set while a page is filled from the model, so its signals are not edits.
  bool loading_ = false;
  /// A General control changed since the page was last written to the draft.
  bool general_dirty_ = false;
};

}  // namespace peregrine
