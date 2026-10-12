// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "new_layer_window.h"

#include <filesystem>
#include <vector>

#include "dialogs.h"

namespace peregrine {

NewLayerWindow::NewLayerWindow(fv::desk::DeskHost& host, Gtk::Window& parent) : host_(host) {
  set_title("New Feature Layer");
  set_transient_for(parent);
  set_default_size(460, -1);
  set_resizable(false);
  set_hide_on_close(true);

  std::vector<std::string> templates, geometries;
  for (int i = 0; i < host_.NewLayerTemplateCount(); ++i) templates.push_back(host_.NewLayerTemplateTitle(i));
  for (int k = 0; k < host_.NewLayerGeometryCount(); ++k) geometries.push_back(host_.NewLayerGeometryTitle(k));
  SetChoices(template_, templates, 0);
  SetChoices(geometry_, geometries, 0);
  SetChoices(format_, {"GeoPackage", "GeoJSON"}, 0);
  template_.property_selected().signal_changed().connect([this] {
    if (!loading_) Report(host_.SetNewLayerTemplate(SelectedIndex(template_)));
  });
  geometry_.property_selected().signal_changed().connect([this] {
    if (!loading_) Report(host_.SetNewLayerGeometry(SelectedIndex(geometry_)));
  });
  format_.property_selected().signal_changed().connect([this] {
    if (!loading_) Report(host_.SetNewLayerFormat(SelectedIndex(format_)));
  });
  name_.signal_changed().connect([this] {
    if (!loading_) host_.SetNewLayerName(name_.get_text());
  });
  name_.set_activates_default(true);
  path_.set_xalign(0);
  path_.set_hexpand(true);
  path_.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
  choose_.signal_clicked().connect([this] { ChoosePath(); });
  auto* file_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  file_row->append(path_);
  file_row->append(choose_);
  fields_.set_xalign(0);
  fields_.set_wrap(true);
  fields_.add_css_class("dim-label");

  auto* grid = Gtk::make_managed<Gtk::Grid>();
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  int row = 0;
  for (auto [title, control] : std::vector<std::pair<const char*, Gtk::Widget*>>{
           {"Start from", &template_}, {"Geometry", &geometry_}, {"Name", &name_},
           {"Format", &format_}, {"File", file_row}, {"Fields", &fields_}}) {
    auto* label = Gtk::make_managed<Gtk::Label>(title);
    label->set_xalign(1);
    label->set_valign(Gtk::Align::START);
    label->set_margin_top(6);
    control->set_hexpand(true);
    control->set_size_request(300, -1);
    grid->attach(*label, 0, row);
    grid->attach(*control, 1, row++);
  }

  message_.set_xalign(0);
  message_.set_wrap(true);
  message_.add_css_class("error");
  message_.set_visible(false);
  cancel_.signal_clicked().connect([this] { close(); });
  create_.signal_clicked().connect([this] { Create(); });
  create_.add_css_class("suggested-action");
  set_default_widget(create_);

  SetDialogMargins(box_);
  box_.append(*grid);
  box_.append(message_);
  box_.append(*ButtonRow({}, {&cancel_, &create_}));
  set_child(box_);
  CloseOnEscape(*this);
  signal_close_request().connect(
      [this] {
        host_.CloseNewLayer();
        return false;
      },
      false);
  Reload();
}

void NewLayerWindow::Reload() {
  loading_ = true;
  template_.set_selected(static_cast<guint>(std::max(host_.NewLayerTemplate(), 0)));
  geometry_.set_selected(static_cast<guint>(std::max(host_.NewLayerGeometry(), 0)));
  if (name_.get_text().raw() != host_.NewLayerName()) name_.set_text(host_.NewLayerName());
  format_.set_selected(static_cast<guint>(std::max(host_.NewLayerFormat(), 0)));
  const std::string path = host_.NewLayerPath();
  path_.set_text(path.empty() ? "Not chosen" : path);
  path_.set_tooltip_text(path);
  if (path.empty()) path_.add_css_class("dim-label");
  else path_.remove_css_class("dim-label");
  const std::string fields = host_.NewLayerFieldSummary();
  fields_.set_text(fields.empty() ? "No fields" : fields);
  loading_ = false;
}

/// Shows `error` (or clears the message) and re-reads the model.
void NewLayerWindow::Report(const std::string& error) {
  message_.set_text(error);
  message_.set_visible(!error.empty());
  Reload();
}

void NewLayerWindow::ChoosePath() {
  const bool geojson = host_.NewLayerFormat() == 1;
  const std::string ext = geojson ? "geojson" : "gpkg";
  auto dialog = Gtk::FileDialog::create();
  dialog->set_modal(true);
  dialog->set_title("New Feature Layer");
  auto filter = Gtk::FileFilter::create();
  filter->set_name(geojson ? "GeoJSON (*.geojson)" : "GeoPackage (*.gpkg)");
  filter->add_pattern("*." + ext);
  dialog->set_default_filter(filter);
  const std::filesystem::path current = host_.NewLayerPath();
  if (current.empty()) {
    dialog->set_initial_name(host_.NewLayerName() + "." + ext);
  } else {
    dialog->set_initial_folder(Gio::File::create_for_path(current.parent_path().string()));
    dialog->set_initial_name(current.filename().string());
  }
  dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
    try {
      auto file = dialog->save_finish(result);
      if (!file || file->get_path().empty()) return;
      host_.SetNewLayerPath(file->get_path());
      Report(std::string());
    } catch (const Glib::Error&) {
      // dismissed
    }
  });
}

bool NewLayerWindow::Create() {
  host_.SetNewLayerName(name_.get_text());
  const std::string error = host_.CreateNewLayer();
  if (!error.empty()) {
    Report(error);
    return false;
  }
  close();
  return true;
}

std::string NewLayerWindow::message() const {
  return message_.get_visible() ? std::string(message_.get_text()) : std::string();
}

}  // namespace peregrine
