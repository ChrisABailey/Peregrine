// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "properties_window.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>

#include "dialogs.h"
#include "fv_desk_layer_properties.h"

namespace peregrine {
namespace {

using fv::desk::PropertiesTab;

constexpr int kSource = static_cast<int>(PropertiesTab::kSource);
constexpr int kGeneral = static_cast<int>(PropertiesTab::kGeneral);
constexpr int kSymbology = static_cast<int>(PropertiesTab::kSymbology);
constexpr int kLabels = static_cast<int>(PropertiesTab::kLabels);
constexpr int kFields = static_cast<int>(PropertiesTab::kFields);
constexpr int kDisplay = static_cast<int>(PropertiesTab::kDisplay);
constexpr int kSelection = static_cast<int>(PropertiesTab::kSelection);
constexpr int kDefinitionQuery = static_cast<int>(PropertiesTab::kDefinitionQuery);

/// A wrapped, dimmed line of help under a control.
Gtk::Label* HelpLabel(const std::string& text) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->set_xalign(0);
  label->set_wrap(true);
  label->set_max_width_chars(70);
  label->add_css_class("dim-label");
  return label;
}

/// Calls `commit` when `w` loses the keyboard focus.
void OnFocusLeave(Gtk::Widget& w, std::function<void()> commit) {
  auto focus = Gtk::EventControllerFocus::create();
  focus->signal_leave().connect(std::move(commit));
  w.add_controller(focus);
}

/// A right-aligned row title for a form grid.
Gtk::Label* RowTitle(const std::string& text) {
  auto* label = Gtk::make_managed<Gtk::Label>(text);
  label->set_xalign(1);
  label->set_valign(Gtk::Align::START);
  label->set_margin_top(6);
  return label;
}

}  // namespace

PropertiesWindow::PropertiesWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_transient_for(parent);
  set_default_size(640, 520);
  set_hide_on_close(true);
  confirm_ = [this](const std::string& loss) {
    ChoiceDialog ask(*this, "Change the layer’s fields?", loss, {"Cancel", "Change Fields"}, 1, 0, 1);
    return ask.Run() == 1;
  };

  grid_.set_row_spacing(4);
  grid_.set_column_spacing(12);
  grid_.set_margin_top(12);
  grid_.set_margin_bottom(12);
  grid_.set_margin_start(16);
  grid_.set_margin_end(16);
  source_page_.set_child(grid_);
  source_page_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  source_page_.set_vexpand(true);
  BuildGeneral();
  BuildDisplay();
  BuildSelection();

  notebook_.set_vexpand(true);
  notebook_.set_show_border(false);
  message_.set_xalign(0);
  message_.set_wrap(true);
  message_.add_css_class("error");
  message_.set_visible(false);
  revert_.signal_clicked().connect([this] { Revert(); });
  cancel_.signal_clicked().connect([this] { close(); });
  apply_.signal_clicked().connect([this] { Apply(); });
  ok_.signal_clicked().connect([this] { Ok(); });
  ok_.add_css_class("suggested-action");
  buttons_ = ButtonRow({&revert_}, {&cancel_, &apply_, &ok_});

  SetDialogMargins(box_);
  box_.append(notebook_);
  box_.append(message_);
  box_.append(*buttons_);
  set_child(box_);

  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseLayerProperties();
        return false;
      },
      false);
  // A selectable label given the initial focus selects its whole text.
  signal_map().connect([this] { Glib::signal_idle().connect_once(sigc::mem_fun(*this, &Gtk::Window::unset_focus)); });
  Reload();
}

/// The General page: each control writes the draft when it is left or Enter
/// is pressed; Current fills a scale with the map's.
void PropertiesWindow::BuildGeneral() {
  general_page_.set_row_spacing(6);
  general_page_.set_column_spacing(8);
  general_page_.set_margin_top(12);
  general_page_.set_margin_bottom(12);
  general_page_.set_margin_start(12);
  general_page_.set_margin_end(12);

  auto commit_on_leave = [this](Gtk::Widget& w) {
    auto focus = Gtk::EventControllerFocus::create();
    focus->signal_leave().connect([this] {
      if (general_dirty_) CommitGeneral();
    });
    w.add_controller(focus);
  };
  for (Gtk::Entry* e : {&name_, &credits_, &min_scale_, &max_scale_}) {
    e->set_hexpand(true);
    e->signal_changed().connect([this] {
      if (!loading_) general_dirty_ = true;
    });
    e->signal_activate().connect([this] { CommitGeneral(); });
    commit_on_leave(*e);
  }
  description_.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  description_.get_buffer()->signal_changed().connect([this] {
    if (!loading_) general_dirty_ = true;
  });
  commit_on_leave(description_);
  description_scroll_.set_child(description_);
  description_scroll_.set_has_frame(true);
  description_scroll_.set_size_request(-1, 90);
  description_scroll_.set_hexpand(true);
  min_scale_.set_placeholder_text("No limit");
  max_scale_.set_placeholder_text("No limit");
  for (auto [button, entry] : {std::pair{&min_current_, &min_scale_}, std::pair{&max_current_, &max_scale_}}) {
    Gtk::Entry* target = entry;
    button->signal_clicked().connect([this, target] {
      target->set_text(host_.StatusScale());
      CommitGeneral();
    });
  }

  int row = 0;
  general_page_.attach(*RowTitle("Name"), 0, row);
  general_page_.attach(name_, 1, row++, 2, 1);
  general_page_.attach(*RowTitle("Description"), 0, row);
  general_page_.attach(description_scroll_, 1, row++, 2, 1);
  general_page_.attach(*RowTitle("Credits"), 0, row);
  general_page_.attach(credits_, 1, row++, 2, 1);
  auto* scale_heading = Gtk::make_managed<Gtk::Label>("Scale range");
  scale_heading->set_xalign(0);
  scale_heading->add_css_class("heading");
  scale_heading->set_margin_top(10);
  general_page_.attach(*scale_heading, 0, row++, 3, 1);
  auto* scale_help = Gtk::make_managed<Gtk::Label>(
      "The layer is not drawn zoomed out beyond the minimum scale or zoomed in beyond the maximum.");
  scale_help->set_xalign(0);
  scale_help->set_wrap(true);
  scale_help->add_css_class("dim-label");
  general_page_.attach(*scale_help, 0, row++, 3, 1);
  general_page_.attach(*RowTitle("Minimum (out)"), 0, row);
  general_page_.attach(min_scale_, 1, row);
  general_page_.attach(min_current_, 2, row++);
  general_page_.attach(*RowTitle("Maximum (in)"), 0, row);
  general_page_.attach(max_scale_, 1, row);
  general_page_.attach(max_current_, 2, row++);
}

/// The Display page: opacity, and the fields a hover and a link show. Each
/// control writes the draft as it changes.
void PropertiesWindow::BuildDisplay() {
  display_page_.set_margin_top(12);
  display_page_.set_margin_bottom(12);
  display_page_.set_margin_start(12);
  display_page_.set_margin_end(12);
  opacity_.set_range(0, 100);
  opacity_.set_increments(1, 10);
  opacity_.set_digits(0);
  opacity_.set_draw_value(false);
  opacity_.set_size_request(220, -1);
  opacity_.signal_value_changed().connect([this] {
    opacity_label_.set_text(std::to_string(static_cast<int>(opacity_.get_value())) + "%");
    if (!loading_) CommitDisplay();
  });
  opacity_label_.set_width_chars(5);
  opacity_label_.set_xalign(0);
  opacity_row_.append(*Gtk::make_managed<Gtk::Label>("Opacity"));
  opacity_row_.append(opacity_);
  opacity_row_.append(opacity_label_);
  for (Gtk::DropDown* d : {&map_tip_, &hyperlink_}) {
    d->set_halign(Gtk::Align::START);
    d->property_selected().signal_changed().connect([this] {
      if (!loading_) CommitDisplay();
    });
  }
  display_fields_.set_row_spacing(6);
  display_fields_.set_column_spacing(8);
  display_fields_.attach(*RowTitle("Map tip"), 0, 0);
  display_fields_.attach(map_tip_, 1, 0);
  display_fields_.attach(*HelpLabel("The field shown when the pointer rests on a feature."), 1, 1);
  display_fields_.attach(*RowTitle("Hyperlink"), 0, 2);
  display_fields_.attach(hyperlink_, 1, 2);
  display_fields_.attach(*HelpLabel("The field holding each feature’s web address or file."), 1, 3);
  display_page_.append(opacity_row_);
  display_page_.append(display_fields_);
}

/// The Selection page: the default halo or a colour and width of the layer's own.
void PropertiesWindow::BuildSelection() {
  selection_page_.set_margin_top(12);
  selection_page_.set_margin_bottom(12);
  selection_page_.set_margin_start(12);
  selection_page_.set_margin_end(12);
  own_selection_.set_group(default_selection_);
  for (Gtk::CheckButton* b : {&default_selection_, &own_selection_})
    b->signal_toggled().connect([this, b] {
      if (!loading_ && b->get_active()) CommitSelection();
    });
  selection_color_ = Gtk::make_managed<Gtk::ColorDialogButton>(Gtk::ColorDialog::create());
  selection_color_->property_rgba().signal_changed().connect([this] {
    if (!loading_) CommitSelection();
  });
  selection_width_.set_width_chars(6);
  selection_width_.set_max_width_chars(6);
  selection_width_.signal_activate().connect([this] { CommitSelection(); });
  OnFocusLeave(selection_width_, [this] {
    if (host_.LayerSelectionOpen() && own_selection_.get_active()) CommitSelection();
  });
  auto* own_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  own_row->append(own_selection_);
  own_row->append(*selection_color_);
  own_row->append(*Gtk::make_managed<Gtk::Label>("Width"));
  own_row->append(selection_width_);
  own_row->append(*Gtk::make_managed<Gtk::Label>("px"));
  auto* help =
      HelpLabel("Selected features are drawn over a halo of this colour, wider than the feature by the width.");
  help->set_margin_bottom(10);
  selection_count_.add_css_class("dim-label");
  select_by_attributes_.signal_clicked().connect(
      [this] { Report(host_.Execute("layers.select_by_attributes")); });
  auto* count_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  count_row->append(selection_count_);
  count_row->append(select_by_attributes_);
  selection_page_.append(default_selection_);
  selection_page_.append(*own_row);
  selection_page_.append(*help);
  selection_page_.append(*count_row);
}

std::vector<int> PropertiesWindow::HostTabs() const {
  std::vector<int> tabs;
  for (int i = 0; i < host_.LayerPropertiesTabCount(); ++i) tabs.push_back(host_.LayerPropertiesTabAt(i));
  return tabs;
}

void PropertiesWindow::Reload() {
  while (notebook_.get_n_pages() > 0) notebook_.remove_page(-1);
  symbology_ = labels_ = nullptr;
  fields_ = nullptr;
  definition_query_ = nullptr;
  host_tabs_ = HostTabs();
  tabs_.clear();
  auto report = [this](const std::string& error) { Report(error); };
  for (size_t i = 0; i < host_tabs_.size(); ++i) {
    const std::string title = host_.LayerPropertiesTabTitle(static_cast<int>(i));
    Gtk::Widget* page = nullptr;
    switch (host_tabs_[i]) {
      case kSource: page = &source_page_; break;
      case kGeneral: page = &general_page_; break;
      case kSymbology:
        page = symbology_ = Gtk::make_managed<LayerStylePanel>(host_, LayerStylePanel::Part::kSymbology, report);
        break;
      case kLabels:
        page = labels_ = Gtk::make_managed<LayerStylePanel>(host_, LayerStylePanel::Part::kLabels, report);
        break;
      case kFields: page = fields_ = Gtk::make_managed<LayerFieldsPanel>(host_, report); break;
      case kDisplay: page = &display_page_; break;
      case kSelection: page = &selection_page_; break;
      case kDefinitionQuery:
        page = definition_query_ =
            Gtk::make_managed<QueryBuilderPanel>(host_, fv::desk::DeskHost::kQueryDefinition, report);
        break;
      default: continue;
    }
    if (page != &source_page_) {
      page->set_margin_top(12);
      page->set_margin_start(6);
      page->set_margin_end(6);
    }
    notebook_.append_page(*page, title);
    tabs_.push_back(host_tabs_[i]);
  }
  notebook_.set_show_tabs(tabs_.size() > 1);
  buttons_->set_visible(tabs_.size() > 1);
  ReloadSource();
  ReloadGeneral();
  ReloadDisplay();
  ReloadSelection();
  message_.set_visible(false);
  ShowTab(host_.LayerPropertiesTab());
}

bool PropertiesWindow::Refresh() {
  const bool had_layer = std::find(tabs_.begin(), tabs_.end(), kGeneral) != tabs_.end();
  if (had_layer && !host_.LayerGeneralOpen()) return false;
  if (HostTabs() == host_tabs_) return true;
  const int shown = current_tab();
  Reload();
  ShowTab(shown);
  return true;
}

bool PropertiesWindow::ShowTab(int tab) {
  const auto at = std::find(tabs_.begin(), tabs_.end(), tab);
  if (at == tabs_.end()) return false;
  notebook_.set_current_page(static_cast<int>(at - tabs_.begin()));
  return true;
}

int PropertiesWindow::current_tab() const {
  const int page = notebook_.get_current_page();
  return page >= 0 && page < static_cast<int>(tabs_.size()) ? tabs_[page] : -1;
}

void PropertiesWindow::ReloadSource() {
  set_title(host_.LayerPropertiesTitle());
  while (Gtk::Widget* child = grid_.get_first_child()) grid_.remove(*child);
  std::string section;
  int line = 0;
  for (int i = 0; i < host_.LayerPropertyCount(); ++i) {
    if (i == 0 || host_.LayerPropertySection(i) != section) {
      section = host_.LayerPropertySection(i);
      auto* heading = Gtk::make_managed<Gtk::Label>(section);
      heading->set_xalign(0);
      heading->add_css_class("heading");
      if (line > 0) heading->set_margin_top(10);
      grid_.attach(*heading, 0, line++, 2, 1);
    }
    auto* key = Gtk::make_managed<Gtk::Label>(host_.LayerPropertyKey(i));
    key->set_xalign(1);
    key->set_valign(Gtk::Align::START);
    key->add_css_class("dim-label");
    auto* value = Gtk::make_managed<Gtk::Label>(host_.LayerPropertyValue(i));
    value->set_xalign(0);
    value->set_hexpand(true);
    value->set_wrap(true);
    value->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    value->set_selectable(true);
    grid_.attach(*key, 0, line);
    grid_.attach(*value, 1, line++);
  }
}

void PropertiesWindow::ReloadGeneral() {
  loading_ = true;
  name_.set_text(host_.LayerGeneralName());
  description_.get_buffer()->set_text(host_.LayerGeneralDescription());
  credits_.set_text(host_.LayerGeneralCredits());
  min_scale_.set_text(host_.LayerGeneralMinScale());
  max_scale_.set_text(host_.LayerGeneralMaxScale());
  loading_ = false;
  general_dirty_ = false;
}

bool PropertiesWindow::CommitGeneral() {
  general_dirty_ = false;
  if (!host_.LayerGeneralOpen()) return true;
  const std::string error = host_.SetLayerGeneral(name_.get_text(), description_.get_buffer()->get_text(),
                                                  credits_.get_text(), min_scale_.get_text(), max_scale_.get_text());
  Report(error);
  return error.empty();
}

void PropertiesWindow::ReloadDisplay() {
  if (!host_.LayerDisplayOpen()) return;
  loading_ = true;
  opacity_row_.set_visible(host_.LayerDisplayHasOpacity());
  opacity_.set_value(host_.LayerDisplayOpacity());
  opacity_label_.set_text(std::to_string(host_.LayerDisplayOpacity()) + "%");
  const int count = host_.LayerDisplayFieldCount();
  display_fields_.set_visible(count > 0);
  display_field_names_.assign(1, std::string());
  std::vector<std::string> titles;
  for (int i = 0; i < count; ++i) {
    display_field_names_.push_back(host_.LayerDisplayFieldName(i));
    titles.push_back(host_.LayerDisplayFieldTitle(i));
  }
  auto fill = [this, &titles](Gtk::DropDown& d, const std::string& none, const std::string& selected) {
    std::vector<std::string> items{none};
    items.insert(items.end(), titles.begin(), titles.end());
    const auto at = std::find(display_field_names_.begin(), display_field_names_.end(), selected);
    SetChoices(d, items, at == display_field_names_.end() ? 0 : static_cast<int>(at - display_field_names_.begin()));
  };
  fill(map_tip_, "The feature’s title", host_.LayerDisplayMapTipField());
  fill(hyperlink_, "None", host_.LayerDisplayHyperlinkField());
  loading_ = false;
}

void PropertiesWindow::CommitDisplay() {
  if (!host_.LayerDisplayOpen()) return;
  auto name = [this](const Gtk::DropDown& d) {
    const int i = SelectedIndex(d);
    return i > 0 && i < static_cast<int>(display_field_names_.size()) ? display_field_names_[i] : std::string();
  };
  Report(host_.SetLayerDisplay(static_cast<int>(opacity_.get_value()), name(map_tip_), name(hyperlink_)));
}

void PropertiesWindow::ReloadSelection() {
  if (!host_.LayerSelectionOpen()) return;
  loading_ = true;
  const bool use_default = host_.LayerSelectionUsesDefault();
  (use_default ? default_selection_ : own_selection_).set_active(true);
  selection_color_->set_rgba(ToRgba(host_.LayerSelectionColor()));
  selection_color_->set_sensitive(!use_default);
  char width[32];
  std::snprintf(width, sizeof width, "%g", host_.LayerSelectionWidth());
  selection_width_.set_text(width);
  selection_width_.set_sensitive(!use_default);
  const int n = host_.LayerSelectionCount();
  selection_count_.set_text(n == 1 ? "1 feature is selected." : std::to_string(n) + " features are selected.");
  loading_ = false;
}

bool PropertiesWindow::CommitSelection() {
  if (!host_.LayerSelectionOpen()) return true;
  const bool use_default = default_selection_.get_active();
  selection_color_->set_sensitive(!use_default);
  selection_width_.set_sensitive(!use_default);
  double width = -1;
  try {
    size_t used = 0;
    const std::string text = selection_width_.get_text();
    width = std::stod(text, &used);
    if (text.find_first_not_of(" \t", used) != std::string::npos) width = -1;
  } catch (const std::exception&) {
    width = -1;
  }
  const std::string error = host_.SetLayerSelection(use_default, FromRgba(selection_color_->get_rgba()), width);
  Report(error);
  return error.empty();
}

void PropertiesWindow::ReloadPages() {
  ReloadGeneral();
  ReloadDisplay();
  ReloadSelection();
  for (LayerStylePanel* p : {symbology_, labels_})
    if (p) p->Reload();
  if (fields_) fields_->Reload();
  if (definition_query_) definition_query_->Reload();
}

void PropertiesWindow::Report(const std::string& error) {
  message_.set_text(error);
  message_.set_visible(!error.empty());
}

bool PropertiesWindow::Apply() {
  if (general_dirty_ && !CommitGeneral()) return false;
  if (host_.LayerSelectionOpen() && own_selection_.get_active() && !CommitSelection()) return false;
  if (fields_) fields_->CommitPending();
  if (definition_query_) definition_query_->CommitPending();
  if (message_.get_visible()) return false;
  const std::string loss = host_.LayerFieldsLoss();
  if (!loss.empty() && !(confirm_ && confirm_(loss))) return false;
  const std::string error = host_.ApplyLayerProperties();
  if (HostTabs() == host_tabs_) {
    ReloadSource();
    ReloadPages();
  } else {
    const int shown = current_tab();
    Reload();
    ShowTab(shown);
  }
  Report(error);
  return error.empty();
}

void PropertiesWindow::Ok() {
  if (Apply()) close();
}

void PropertiesWindow::Revert() {
  const std::string error = host_.RevertLayerProperties();
  ReloadPages();
  Report(error);
}

std::string PropertiesWindow::message() const {
  return message_.get_visible() ? std::string(message_.get_text()) : std::string();
}

}  // namespace peregrine
