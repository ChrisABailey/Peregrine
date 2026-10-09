// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "data_sources_window.h"

#include <utility>
#include <vector>

namespace peregrine {

DataSourcesWindow::DataSourcesWindow(fv::desk::DeskHost& host, Gtk::Window& parent,
                                     ErrorFn present_error)
    : host_(host), present_error_(std::move(present_error)) {
  set_title("Map Data Sources");
  set_transient_for(parent);
  set_default_size(600, 340);
  set_size_request(440, 240);
  set_hide_on_close(true);

  intro_.set_text(
      "Folders of map data. Each is searched for every format it holds "
      "(rpf, tiros3, dted, geotiff, enc, OSM and VPF databases). "
      "Generate Coverage removes the catalog's coverage and rescans every folder.");
  intro_.set_wrap(true);
  intro_.set_xalign(0);
  intro_.add_css_class("dim-label");

  list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
  list_.add_css_class("rich-list");
  list_.signal_selected_rows_changed().connect([this] { UpdateButtons(); });
  scroll_.set_child(list_);
  scroll_.set_vexpand(true);
  scroll_.set_has_frame(true);
  scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

  add_.signal_clicked().connect([this] { ChooseFolders(); });
  remove_.signal_clicked().connect([this] { RemoveSelected(); });
  close_.signal_clicked().connect([this] { close(); });
  generate_.signal_clicked().connect([this] { Generate(); });
  generate_.add_css_class("suggested-action");
  auto* spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  buttons_.append(add_);
  buttons_.append(remove_);
  buttons_.append(*spacer);
  buttons_.append(close_);
  buttons_.append(generate_);
  set_default_widget(generate_);

  box_.set_margin_top(16);
  box_.set_margin_bottom(16);
  box_.set_margin_start(20);
  box_.set_margin_end(20);
  box_.append(intro_);
  box_.append(scroll_);
  box_.append(buttons_);
  set_child(box_);

  auto keys = Gtk::EventControllerKey::create();
  keys->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval != GDK_KEY_Escape) return false;
        close();
        return true;
      },
      false);
  add_controller(keys);
  Reload();
}

void DataSourcesWindow::Reload() {
  const int selected = list_.get_selected_row() ? list_.get_selected_row()->get_index() : -1;
  while (Gtk::Widget* row = list_.get_first_child()) list_.remove(*row);
  const int count = host_.ScanRootCount();
  for (int i = 0; i < count; ++i) {
    const std::string path = host_.ScanRootAt(i);
    auto* label = Gtk::make_managed<Gtk::Label>(path);
    label->set_xalign(0);
    label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    label->set_tooltip_text(path);
    if (!host_.ScanRootReachable(i)) {
      label->set_text(path + "  (not found)");
      label->add_css_class("error");
    }
    list_.append(*label);
  }
  if (selected >= 0 && selected < count) list_.select_row(*list_.get_row_at_index(selected));
  UpdateButtons();
}

void DataSourcesWindow::UpdateButtons() {
  const bool idle = !host_.JobActive();
  remove_.set_sensitive(idle && list_.get_selected_row() != nullptr);
  generate_.set_sensitive(idle);
}

void DataSourcesWindow::ChooseFolders() {
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  dialog->set_title("Choose folders of map data");
  dialog->set_accept_label("Add");
  dialog->select_multiple_folders(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& r) {
    std::vector<std::string> paths;
    try {
      for (const auto& folder : dialog->select_multiple_folders_finish(r)) {
        if (folder && !folder->get_path().empty()) paths.push_back(folder->get_path());
      }
    } catch (const Glib::Error&) {
      return;  // dismissed
    }
    AddFolders(paths);
  });
}

void DataSourcesWindow::AddFolders(const std::vector<std::string>& paths) {
  for (const std::string& path : paths) {
    const std::string error = host_.AddScanRoot(path);
    if (!error.empty()) {
      if (present_error_) present_error_("Could not add the folder", error, *this);
      break;
    }
  }
  Reload();
}

void DataSourcesWindow::RemoveSelected() {
  const Gtk::ListBoxRow* row = list_.get_selected_row();
  if (!row) return;
  const std::string error = host_.RemoveScanRoot(host_.ScanRootAt(row->get_index()));
  if (!error.empty() && present_error_) present_error_("Could not remove the folder", error, *this);
  Reload();
}

void DataSourcesWindow::Generate() {
  const std::string error = host_.GenerateCoverage();
  if (!error.empty()) {
    if (present_error_) present_error_("Could not generate coverage", error, *this);
    return;
  }
  close();
}

}  // namespace peregrine
