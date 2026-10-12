// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "properties_window.h"

#include <algorithm>
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

std::vector<int> PropertiesWindow::HostTabs() const {
  std::vector<int> tabs;
  for (int i = 0; i < host_.LayerPropertiesTabCount(); ++i) tabs.push_back(host_.LayerPropertiesTabAt(i));
  return tabs;
}

void PropertiesWindow::Reload() {
  while (notebook_.get_n_pages() > 0) notebook_.remove_page(-1);
  symbology_ = labels_ = nullptr;
  fields_ = nullptr;
  tabs_ = HostTabs();
  auto report = [this](const std::string& error) { Report(error); };
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const std::string title = host_.LayerPropertiesTabTitle(static_cast<int>(i));
    Gtk::Widget* page = nullptr;
    switch (tabs_[i]) {
      case kSource: page = &source_page_; break;
      case kGeneral: page = &general_page_; break;
      case kSymbology:
        page = symbology_ = Gtk::make_managed<LayerStylePanel>(host_, LayerStylePanel::Part::kSymbology, report);
        break;
      case kLabels:
        page = labels_ = Gtk::make_managed<LayerStylePanel>(host_, LayerStylePanel::Part::kLabels, report);
        break;
      case kFields: page = fields_ = Gtk::make_managed<LayerFieldsPanel>(host_, report); break;
      default: continue;
    }
    if (page != &source_page_) {
      page->set_margin_top(12);
      page->set_margin_start(6);
      page->set_margin_end(6);
    }
    notebook_.append_page(*page, title);
  }
  notebook_.set_show_tabs(tabs_.size() > 1);
  buttons_->set_visible(tabs_.size() > 1);
  ReloadSource();
  ReloadGeneral();
  message_.set_visible(false);
  ShowTab(host_.LayerPropertiesTab());
}

bool PropertiesWindow::Refresh() {
  const bool had_layer = std::find(tabs_.begin(), tabs_.end(), kGeneral) != tabs_.end();
  if (had_layer && !host_.LayerGeneralOpen()) return false;
  if (HostTabs() == tabs_) return true;
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

void PropertiesWindow::Report(const std::string& error) {
  message_.set_text(error);
  message_.set_visible(!error.empty());
}

bool PropertiesWindow::Apply() {
  if (general_dirty_ && !CommitGeneral()) return false;
  if (fields_) fields_->CommitPending();
  if (message_.get_visible()) return false;
  const std::string loss = host_.LayerFieldsLoss();
  if (!loss.empty() && !(confirm_ && confirm_(loss))) return false;
  const std::string error = host_.ApplyLayerProperties();
  if (HostTabs() == tabs_) {
    ReloadSource();
    ReloadGeneral();
    for (LayerStylePanel* p : {symbology_, labels_})
      if (p) p->Reload();
    if (fields_) fields_->Reload();
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
  ReloadGeneral();
  for (LayerStylePanel* p : {symbology_, labels_})
    if (p) p->Reload();
  if (fields_) fields_->Reload();
  Report(error);
}

std::string PropertiesWindow::message() const {
  return message_.get_visible() ? std::string(message_.get_text()) : std::string();
}

}  // namespace peregrine
