// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// map_window.h — one map window, the counterpart of MapWindowController.swift.
#pragma once

#include <gtkmm.h>

#include <memory>
#include <string>
#include <vector>

#include "attribute_table_window.h"
#include "data_sources_window.h"
#include "dialogs.h"
#include "feature_form_window.h"
#include "fv_desk_host.h"
#include "layer_fields_window.h"
#include "properties_window.h"
#include "layer_style_window.h"
#include "layers_window.h"
#include "main_menu.h"
#include "map_canvas.h"
#include "new_layer_window.h"
#include "options_window.h"
#include "profile_window.h"

namespace peregrine {

/// Releases the reference `DeskHost::Create` returned.
struct DeskHostRelease {
  void operator()(fv::desk::DeskHost* host) const { fv_desk_host_release(host); }
};

/// The map widget over a status bar (feedback from the map and overlays on
/// the left, scale and series in the centre, position and terrain height
/// under the cursor on the right), under the menu bar and with the toolbar in
/// the header bar, both generated from DeskKit's model, and the dialogs the
/// model calls for.
class MapWindow : public Gtk::ApplicationWindow {
 public:
  MapWindow();
  ~MapWindow() override;

  fv::desk::DeskHost& host() { return *host_; }
  MapCanvas& canvas() { return canvas_; }

  /// Opens a catalog; on failure shows the error and keeps the current one.
  bool OpenCatalog(const std::string& path);
  /// Moves the camera; a `scale` of 0 keeps the current one.
  void GoTo(double lat, double lon, double scale);
  void RefreshStatus();
  /// Executes a DeskKit command; shows its error, and closes the window once
  /// the quit command has closed every document.
  void Execute(const std::string& command_id);
  /// Regenerates the menu bar, toolbar and shortcuts when the model changed.
  void UpdateMenus();
  /// Help ▸ Show Log: opens today's log file in the default viewer.
  void ShowLog();
  /// Brings up the Map Data Sources window, reusing it when it is showing.
  void ShowDataSources();
  /// The Map Data Sources window, or null when it is not open.
  DataSourcesWindow* data_sources_window() { return data_sources_.get(); }
  /// The panels and dialogs the core opened, or null before the first time
  /// each is shown; a closed one is hidden, not destroyed.
  LayersWindow* layers_window() { return layers_.get(); }
  FeatureFormWindow* feature_form_window() { return feature_form_.get(); }
  AttributeTableWindow* attribute_table_window() { return attribute_table_.get(); }
  LayerStyleWindow* layer_style_window() { return layer_style_.get(); }
  LayerFieldsWindow* layer_fields_window() { return layer_fields_.get(); }
  PropertiesWindow* properties_window() { return properties_.get(); }
  NewLayerWindow* new_layer_window() { return new_layer_.get(); }
  ProfileWindow* profile_window() { return profile_.get(); }
  /// The options window of `kind` (a `fv::desk::HostOptionsKind`), or null.
  OptionsWindow* options_window(int kind) { return kind == 0 ? map_options_.get() : overlay_options_.get(); }
  /// The progress dialog of a running job, or null.
  JobDialog* job_dialog() { return job_dialog_.get(); }
  /// Renders the current view and writes the window's content (map and
  /// status bar) to `path` as a PNG, then prints the status line to stderr.
  /// Returns the error message, or "".
  std::string WriteSnapshot(const std::string& path);

 private:
  void Handle(const fv::desk::HostTick& tick);
  void CatalogDidChange();
  void UpdateTitle();
  void UpdateJob();
  void AnswerRequest();
  bool OnCloseRequest();
  void PresentError(const std::string& message, const std::string& detail);
  void PresentError(const std::string& message, const std::string& detail, Gtk::Window& parent);
  void PresentNotice(const std::string& notice);
  void RebuildToolbar();
  void FeaturesDidChange();
  /// Shows `window`, creating it with `make` the first time and reloading it after.
  template <typename W, typename Make>
  void ShowPanel(std::unique_ptr<W>& window, Make make);

  // Declared first so the host outlives the widgets that call it.
  std::unique_ptr<fv::desk::DeskHost, DeskHostRelease> host_;
  MapCanvas canvas_;
  CommandMenus menus_;
  Gtk::HeaderBar header_;
  Gtk::Box toolbar_{Gtk::Orientation::HORIZONTAL, 2};
  std::vector<ToolbarItem> toolbar_items_;
  std::vector<std::string> accelerated_actions_;
  Gtk::Box content_{Gtk::Orientation::VERTICAL};
  Gtk::PopoverMenuBar menu_bar_;
  Gtk::Box status_bar_{Gtk::Orientation::HORIZONTAL, 16};
  Gtk::Box status_left_{Gtk::Orientation::HORIZONTAL, 16};
  Gtk::Box status_centre_{Gtk::Orientation::HORIZONTAL, 16};
  Gtk::Box status_right_{Gtk::Orientation::HORIZONTAL, 16};
  Gtk::Label feedback_label_;
  Gtk::Label scale_label_;
  Gtk::Label product_label_;
  Gtk::Label position_label_;
  Gtk::Label elevation_label_;
  /// The title when no overlay file is open: the app and the catalog.
  std::string catalog_title_ = "Peregrine";
  std::string tooltip_;
  std::unique_ptr<JobDialog> job_dialog_;
  std::unique_ptr<DataSourcesWindow> data_sources_;
  std::unique_ptr<LayersWindow> layers_;
  std::unique_ptr<FeatureFormWindow> feature_form_;
  std::unique_ptr<AttributeTableWindow> attribute_table_;
  std::unique_ptr<LayerStyleWindow> layer_style_;
  std::unique_ptr<LayerFieldsWindow> layer_fields_;
  std::unique_ptr<PropertiesWindow> properties_;
  std::unique_ptr<NewLayerWindow> new_layer_;
  std::unique_ptr<ProfileWindow> profile_;
  std::unique_ptr<OptionsWindow> map_options_;
  std::unique_ptr<OptionsWindow> overlay_options_;
};

}  // namespace peregrine
