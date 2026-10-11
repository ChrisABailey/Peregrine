// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_desk.h — the desktop application model of one map window.
///
/// `Desk` composes the fv::app layer (type registry, overlay stack, session,
/// editors), the catalog and its map groups, the ViewKit map view, the
/// command registry and the menu model. Every user action is a command; the
/// dynamic ones (one per map group with data, per projection, per overlay
/// type, per open file overlay, per editor tool) are re-registered whenever
/// what they list changes, and the menus are rebuilt after. A native shell
/// implements `DeskShell` and renders `menus()`; it holds no state of its own.
/// Single-threaded: call from the UI thread.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fv_desk_attribute_table.h"
#include "fv_desk_catalog_build.h"
#include "fv_desk_commands.h"
#include "fv_desk_feature_form.h"
#include "fv_desk_layer_fields.h"
#include "fv_desk_layer_style.h"
#include "fv_desk_layers.h"
#include "fv_desk_map_groups.h"
#include "fv_desk_menu_model.h"
#include "fv_desk_options.h"
#include "fv_desk_profile.h"
#include "fv_desk_workspace.h"
#include "fv_view_map_view.h"
#include "fvkit/app/editor.h"
#include "fvkit/app/pick.h"
#include "fvkit/app/session.h"
#include "fvkit/app/shell.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/app/undo.h"
#include "fvkit/overlay/manager.h"

namespace fv {
class Catalog;
class Settings;
struct IElevationSource;
struct KeyEvent;
class RoutePlanner;

namespace desk {

/// The native shell: every `AppShell` question plus the desktop extras.
class DeskShell : public app::AppShell {
 public:
  /// The menu model was rebuilt; regenerate the menu bar from `Desk::menus()`.
  virtual void MenusChanged() = 0;
  /// Quit completed (every document closed or discarded); end the app.
  virtual void Quit() = 0;
  /// Show the Overlay ▸ Options dialog over `model`; on OK the shell calls
  /// `Desk::ApplyOverlayOptions(*model)`.
  virtual void ShowOverlayOptions(std::shared_ptr<OptionsModel> model) = 0;
  /// Show the Map ▸ Options dialog over `model`; on OK the shell calls
  /// `Desk::ApplyMapOptions(*model)`.
  virtual void ShowMapOptions(std::shared_ptr<OptionsModel> /*model*/) {}
  /// Show the Map Data Sources dialog; it works through `Desk::ScanRoots`,
  /// `AddScanRoot`, `RemoveScanRoot` and `GenerateCoverage`.
  virtual void ShowDataSources() {}
  /// Asks for a directory; "" when cancelled.
  virtual std::string ChooseDirectory(const std::string& /*title*/) { return std::string(); }
  /// An informational message for the user, such as a catalog build's result.
  virtual void ShowNotice(const std::string& /*text*/) {}
  /// Show (or bring forward) the route elevation profile window. It draws
  /// `Desk::route_profile()` and calls `Desk::CloseRouteProfile()` on close.
  virtual void ShowRouteProfile() {}
  /// Show (or bring forward) the layers panel. It reads `Desk::layers()` and
  /// changes the stack through `Desk::SetLayerVisible` and its siblings.
  virtual void ShowLayers() {}
  /// Show (or bring forward) the feature form over `Desk::feature_form()`;
  /// the shell applies it through `Desk::ApplyFeatureForm`.
  virtual void ShowFeatureForm() {}
  /// Show (or bring forward) the attribute table over `Desk::attribute_table()`.
  virtual void ShowAttributeTable() {}
  /// Show (or bring forward) the style dialog over `Desk::layer_style()`; the
  /// shell applies it through `Desk::ApplyLayerStyle`.
  virtual void ShowLayerStyle() {}
  /// Show (or bring forward) the Layer Fields dialog over
  /// `Desk::layer_fields()`; the shell applies it through `Desk::ApplyLayerFields`.
  virtual void ShowLayerFields() {}
  /// Show (or bring forward) the New Layer dialog over `Desk::new_layer()`;
  /// the shell creates the layer through `Desk::CreateNewLayer`.
  virtual void ShowNewLayer() {}
};

/// The status bar content (port/desktop-plan.md §1d), as text.
struct StatusBar {
  std::string scale;    ///< "1:50,000"
  std::string product;  ///< the series drawn, e.g. "cadrg GNC"; empty when none
  std::string message;  ///< e.g. the end of the raster ladder
  std::string feedback; ///< transient text from the map or an overlay, e.g. a hover hint
};

/// "1:50,000" — the denominator rounded to an integer, thousands separated.
std::string FormatScale(double scale_denom);

class Desk {
 public:
  /// Registers fvkit's built-in overlay types and Desk's own commands. More
  /// overlay types may be registered on `types()` before `Refresh()`.
  /// `shell` and `settings` must outlive the Desk.
  Desk(DeskShell& shell, Settings& settings, MapGroups groups = MapGroups::Builtin());
  ~Desk();
  Desk(const Desk&) = delete;
  Desk& operator=(const Desk&) = delete;

  // MARK: Parts
  app::OverlayTypeRegistry& types() { return types_; }
  OverlayManager& overlays() { return overlays_; }
  app::OverlaySession& session() { return *session_; }
  app::EditorManager& editors() { return *editors_; }
  CommandRegistry& commands() { return commands_; }
  const MenuModel& menus() const { return menus_; }
  view::MapView& map_view() { return *view_; }
  const MapGroups& groups() const { return groups_; }

  /// Re-registers the dynamic commands and rebuilds the menus. Needed only
  /// after registering overlay types; everything else refreshes itself.
  void Refresh();
  /// Registers the overlay types declared in each directory's manifests
  /// (fv_desk_overlay_manifest.h) and refreshes. Problems go to `warnings()`.
  /// Returns the ids registered.
  std::vector<app::TypeId> LoadOverlayManifests(const std::vector<std::string>& dirs);
  /// `commands().Execute(id)`.
  Status Execute(const std::string& id);

  // MARK: Catalog and map group
  /// Opens a catalog database. On failure the current catalog is kept.
  Status OpenCatalog(const std::string& path);
  /// Uses a catalog the caller opened. `path` is what a workspace records.
  void SetCatalog(std::shared_ptr<Catalog> catalog, const std::string& path = std::string());
  /// The catalog's contents changed (a scan, a removed source): re-derives the
  /// groups with data, falls back to the first available group if the
  /// current one lost its data, and rebuilds the menus.
  void CatalogChanged();
  const std::shared_ptr<Catalog>& catalog() const { return catalog_; }
  /// The first DTED data source in the catalog, or null. Shared by Contour
  /// Lines and the TA Mask; read it only where the overlay stack may be read.
  const std::shared_ptr<IElevationSource>& elevation_source() const { return elevation_; }
  const std::string& catalog_path() const { return catalog_path_; }
  /// The file name of the current overlay, or of the topmost file overlay
  /// when the current one is not a file; "unnamed.<ext>" for one never saved;
  /// empty when the stack holds no file overlay. For the window title.
  std::string DocumentName() const;
  /// Terrain height at `p` in feet from the catalog's DTED; false where there
  /// is none. UI thread only: it reads its own source, not `elevation_source()`.
  bool ElevationFeetAt(const GeoPoint& p, double* feet);

  /// Sets the status bar's feedback text; "" clears it. The next hover
  /// replaces it.
  void SetFeedback(std::string text) { feedback_ = std::move(text); }
  /// The hover hint from the base map under the pointer; overlay and editor
  /// hints take precedence over it.
  void SetMapHint(app::HintText hint);
  /// The floating tip for the pointer: the overlay hit's, else the base map's.
  const std::string& HoverToolTip() const { return tool_tip_; }

  /// The directories Generate Coverage scans, absolute. A catalog that has
  /// never saved a list offers the directories of its data sources.
  std::vector<std::string> ScanRoots() const;
  /// Adds a directory (made absolute) to the list and saves it in the
  /// catalog. A directory already covered by the list is refused; one that
  /// contains listed directories replaces them.
  Status AddScanRoot(const std::string& path);
  /// Removes a directory from the list and saves it. Coverage is unchanged
  /// until the next Generate Coverage.
  Status RemoveScanRoot(const std::string& path);
  /// Removes every data source and its coverage, then scans each listed
  /// directory in the background. Refused while a listed directory is
  /// unreachable, so an unmounted volume does not lose its coverage.
  Status GenerateCoverage();
  /// The running or just-finished build, or null.
  const CatalogBuild* catalog_build() const { return build_.get(); }
  bool Building() const { return build_ != nullptr; }
  void CancelCatalogBuild();
  /// Picks up a finished build: refreshes the catalog's groups, recentres if
  /// the catalog had no map data before, and shows the build's summary. Call
  /// from the UI thread; true when a build finished.
  bool PollCatalogBuild();
  /// Blocks until a running build finishes, then polls it; for tests.
  void WaitForCatalogBuild();

  /// The groups with coverage in the catalog, in table order.
  const std::vector<const MapGroup*>& AvailableGroups() const { return available_; }
  /// kNotFound for an unknown id; kUnsupported for a group without data.
  Status SelectGroup(const std::string& id);
  /// Null when no group has data.
  const MapGroup* CurrentGroup() const;
  /// Moves the camera to `center` at 1:`scale_denom` and chooses the product
  /// there afresh, as a group change does (the series ladder settles on the
  /// series nearest that scale).
  void GoTo(const GeoPoint& center, double scale_denom);

  // MARK: Editing input
  /// True while an overlay editor is active (Tools ▸ Route and the like).
  bool Editing() const;
  /// Offer a pointer event, in points from the top-left, to the overlay stack.
  /// True when an overlay took it; the shell pans only when none did. Only an
  /// active editor or a mouse capture receives anything.
  bool OverlayPointerDown(double x_pt, double y_pt);
  /// `shift` extends a selection (shift-click, shift-drag box).
  bool OverlayPointerDown(double x_pt, double y_pt, bool shift);
  /// `clicks` is 2 for the second press of a double-click; a double-click
  /// on a feature being edited opens it in the feature form.
  bool OverlayPointerDown(double x_pt, double y_pt, bool shift, int clicks);
  bool OverlayPointerDrag(double x_pt, double y_pt);
  bool OverlayPointerUp(double x_pt, double y_pt);
  /// Hit-tests the overlay stack under the cursor, in points; a change of
  /// hover sets the status bar feedback to the hit's hint (or clears it).
  void OverlayHover(double x_pt, double y_pt);
  void OverlayHoverExit();
  /// Offer a key to the overlay stack while editing; true when it was used.
  bool OverlayKeyDown(const KeyEvent& e);
  /// The road graph routes follow, from `routing.graph` and `routing.rules`;
  /// null when `routing.graph` is unset.
  const RoutePlanner* route_planner() const { return planner_.get(); }

  /// Gives overlays drawn in authored pixels the view's display scale. Call
  /// after the scale changes, where the overlay stack may be changed.
  void DisplayScaleChanged();

  // MARK: Undo
  /// The workspace's one history; every feature edit feeds it and Edit ▸
  /// Undo/Redo drive it. Cleared when a workspace is opened.
  app::UndoStack& undo() { return undo_; }
  const app::UndoStack& undo() const { return undo_; }

  // MARK: Layers panel
  /// The stack as panel rows. Read where the overlay stack may be read.
  const LayersModel& layers() const { return layers_; }
  /// Changes whenever a row would read differently: an overlay added, removed,
  /// moved, made current, renamed, dirtied, shown, hidden or faded.
  uint64_t LayersGeneration() const { return layers_generation_; }
  /// Each changes one row through `layers()` and repaints; errors are returned, not reported.
  Status SetLayerVisible(size_t row, bool visible);
  Status SetLayerOpacity(size_t row, int percent);
  Status MoveLayer(size_t from, size_t to);
  Status SelectLayer(size_t row);

  // MARK: Feature form and attribute table
  /// The editable feature layer behind `overlay`: a feature overlay's layer,
  /// or a point set through `PointsFeatureLayer`. Null for any other overlay.
  static std::shared_ptr<IEditableFeatureLayer> FeatureLayerOf(const std::shared_ptr<Overlay>& overlay);
  /// The feature selected in `overlay` (a point, for a point set); 0 = none.
  static int64_t SelectedFeatureOf(const Overlay& overlay);
  /// Selects feature `id` in `overlay`. False for an overlay with no features.
  static bool SelectFeatureIn(Overlay& overlay, int64_t id);
  /// Opens the form on feature `id` of `overlay` and asks the shell to show it.
  Status EditFeature(const Overlay* overlay, int64_t id);
  /// The form on the current overlay's selected feature. kNotFound without one.
  Status EditSelectedFeature();
  /// The table over the current overlay, shown by the shell. kNotFound when
  /// the current overlay has no features.
  Status OpenAttributeTable();
  /// Null when no form or table is open, or its overlay has closed.
  const std::shared_ptr<FeatureFormModel>& feature_form() const { return form_; }
  const std::shared_ptr<AttributeTableModel>& attribute_table() const { return table_; }
  /// Writes the form, dirties its overlay, reloads the table and repaints.
  Status ApplyFeatureForm();
  void CloseFeatureForm();
  void CloseAttributeTable();
  /// Selects the table row's feature on the map and repaints.
  Status SelectTableRow(size_t row);
  /// The table row of its overlay's selected feature; -1 for none.
  int SelectedTableRow() const;
  /// Opens the form on the table row's feature.
  Status EditTableRow(size_t row);
  /// The style dialog over the current feature overlay's layer style.
  /// kNotFound when the current overlay is not a feature overlay.
  Status OpenLayerStyle();
  /// The style dialog over the selected feature's own symbol.
  Status StyleSelectedFeature();
  /// Null when no style dialog is open, or its overlay has closed.
  const std::shared_ptr<LayerStyleModel>& layer_style() const { return style_; }
  /// Writes the draft style to its overlay, dirties it and repaints.
  Status ApplyLayerStyle();
  void CloseLayerStyle();
  /// The Layer Fields dialog over the current feature overlay. kNotFound when
  /// the current overlay is not a feature overlay.
  Status OpenLayerFields();
  /// Null when no Layer Fields dialog is open, or its overlay has closed.
  const std::shared_ptr<LayerFieldsModel>& layer_fields() const { return fields_; }
  /// What applying the draft would lose, as sentences; "" when nothing. The
  /// shell asks before applying when it is not empty.
  std::string LayerFieldsLoss() const;
  /// Changes the layer's fields, dirties its overlay, closes its form and
  /// style dialog (they name the old fields), reloads its table and repaints.
  Status ApplyLayerFields();
  void CloseLayerFields();
  /// A fresh New Layer dialog, shown by the shell.
  void OpenNewLayer();
  const std::shared_ptr<NewLayerModel>& new_layer() const { return new_layer_; }
  /// Writes the dialog's empty layer, opens it as the current overlay and
  /// closes the dialog. kInvalidArg when the file is open in the stack.
  Status CreateNewLayer();
  void CloseNewLayer();
  /// Changes whenever a feature dialog (form, table, style, fields, new
  /// layer) is opened, closed or reloaded.
  uint64_t FeatureViewsGeneration() const { return feature_views_gen_; }

  // MARK: Route elevation profile
  /// Profiles the route being edited, else the current overlay when it is a
  /// route, and asks the shell to show the window. kNotFound without a route
  /// of two or more waypoints (also reported through the shell).
  Status OpenRouteProfile();
  void CloseRouteProfile();
  bool RouteProfileOpen() const { return profile_open_; }
  /// Re-samples when the route's path, the elevation data or the profile
  /// settings (`analysis.units`, `analysis.profile_samples`) changed; true
  /// when the chart changed. A closed route leaves a message in the chart.
  bool UpdateRouteProfile();
  const ProfileChart& route_profile() const { return profile_; }
  /// "Terrain Profile — <route name>".
  const std::string& route_profile_title() const { return profile_title_; }

  // MARK: Workspace
  /// The window's state now. Writes the overlay configuration through
  /// `OverlaySession::SaveConfiguration("workspace")`.
  Workspace CaptureWorkspace();
  /// Replaces the window's state: closes every overlay (a cancel stops the
  /// restore), then restores catalog, group, camera, product, overlays and
  /// editor. Missing parts (a catalog that will not open, an overlay type
  /// that is gone) are skipped and listed in `warnings()`.
  app::FlowResult ApplyWorkspace(const Workspace& w);
  app::FlowResult SaveWorkspace(const std::string& path);
  app::FlowResult OpenWorkspace(const std::string& path);

  // MARK: Options
  /// A fresh options model over the registered types and current settings.
  std::shared_ptr<OptionsModel> OverlayOptions() const;
  /// Applies `model` to the settings, the user settings (when set) and every
  /// open overlay, then repaints. Rejections go to `warnings()`.
  Status ApplyOverlayOptions(OptionsModel& model);
  /// A fresh Map ▸ Options model over the map groups and current settings.
  std::shared_ptr<OptionsModel> MapOptions() const;
  /// Applies `model` like `ApplyOverlayOptions`, then bumps
  /// `MapStyleGeneration()` so the base map is drawn afresh.
  Status ApplyMapOptions(OptionsModel& model);
  /// Adds the options of a map group, replacing any source for the same group.
  void RegisterMapOptions(MapOptionsSource source);
  /// Pushes the settings' map options into the renderers and bumps
  /// `MapStyleGeneration()`. Call after loading or replacing the settings.
  void SettingsLoaded();
  /// Changes whenever map options change what a base map looks like.
  uint64_t MapStyleGeneration() const { return map_style_gen_; }
  /// Where applied options persist. Null (the default) keeps them in memory.
  void SetUserSettings(UserSettings* user) { user_settings_ = user; }
  UserSettings* user_settings() const { return user_settings_; }

  // MARK: Status
  StatusBar CurrentStatus() const;
  /// Warnings since the last ClearWarnings; each was also logged.
  const std::vector<std::string>& warnings() const { return warnings_; }
  void ClearWarnings() { warnings_.clear(); }

 private:
  class ShellTap;
  class StackHook;

  void RegisterStaticCommands();
  void RegisterCatalogCommands();
  /// Opens or creates the catalog file at `path`.
  Status UseCatalogFile(const std::string& path, bool create);
  /// Writes the scan list to the catalog.
  Status SaveScanRoots(const std::vector<std::string>& roots);
  void Recenter();
  /// Moves the camera by fractions of the surface: (0.5, 0) brings the point
  /// half a screen to the right into the centre.
  void PanScreens(double fx, double fy);
  void RegisterGroupCommands();
  void RegisterProjectionCommands();
  void RegisterOverlayTypeCommands();
  void RegisterInstanceCommands();
  /// Recomputes the feedback text and tool tip from the three hover hints.
  void ComposeHoverHint();
  /// Asks the edited overlay what a press at `e` would do.
  void UpdateEditHint(const MouseEvent& e);
  void SetOverlayHint(const app::HintText& hint);
  void RegisterEditorCommands();
  void RebuildMenus();
  /// The file overlay File ▸ Save/Close act on: the current overlay when it
  /// is a file overlay. Null otherwise.
  Overlay* FileTarget() const;
  std::vector<view::LadderProduct> ProductsAt(const GeoPoint& p) const;
  void ApplyGroupToView();
  /// Reopens `elevation_` from the catalog and hands it to every open
  /// elevation overlay.
  void ResolveElevation();
  /// Connects an overlay joining the stack to this window: the elevation
  /// source for Contour Lines and the TA Mask, the stack and road graph for a
  /// route. False when it needs DTED and the catalog has none.
  bool BindOverlay(Overlay& overlay);
  /// Reopens the route planner when the routing settings changed and hands
  /// it to every open route.
  void ResolveRoutePlanner();
  /// Closes a form, table, style or fields dialog on `overlay`, which is leaving the stack.
  void DropFeatureViews(const Overlay& overlay);
  /// Logs `s` and reports it through the shell.
  app::FlowResult Report(const Status& s);
  /// Logs a warning at the caller's location and keeps it for `warnings()`.
  void Warn(std::string w, const char* file = __builtin_FILE(), int line = __builtin_LINE());

  DeskShell& shell_;
  Settings& settings_;
  MapGroups groups_;
  std::unique_ptr<ShellTap> tap_;
  app::OverlayTypeRegistry types_;
  /// Declared before the stack so it outlives every overlay that feeds it.
  app::UndoStack undo_;
  OverlayManager overlays_;
  std::unique_ptr<app::OverlaySession> session_;
  std::unique_ptr<app::EditorManager> editors_;
  std::unique_ptr<app::PickSession> pick_;
  std::unique_ptr<StackHook> stack_hook_;
  LayersModel layers_{overlays_};
  uint64_t layers_generation_ = 0;
  std::shared_ptr<FeatureFormModel> form_;
  std::shared_ptr<AttributeTableModel> table_;
  std::shared_ptr<LayerStyleModel> style_;
  std::shared_ptr<LayerFieldsModel> fields_;
  std::shared_ptr<NewLayerModel> new_layer_;
  uint64_t feature_views_gen_ = 0;
  CommandRegistry commands_;
  MenuModel menus_;
  std::unique_ptr<view::MapView> view_;

  std::shared_ptr<Catalog> catalog_;
  std::string catalog_path_;
  std::vector<const MapGroup*> available_;
  std::string group_id_;
  std::optional<MenuSpec> editor_menu_;
  std::vector<std::string> warnings_;
  UserSettings* user_settings_ = nullptr;
  std::vector<MapOptionsSource> map_options_;
  /// Overlay-page sections the Desk acts on, keyed by type id.
  std::vector<MapOptionsSource> overlay_options_;
  uint64_t map_style_gen_ = 0;
  std::unique_ptr<CatalogBuild> build_;
  bool build_had_data_ = false;
  std::shared_ptr<IElevationSource> elevation_;
  std::unique_ptr<IElevationSource> pointer_elevation_;  ///< the UI thread's own cells
  std::string feedback_;
  // Hover feedback, by precedence: the edited overlay's action, the overlay
  // hit, the base map.
  std::string edit_hint_;
  app::HintText overlay_hint_;
  app::HintText map_hint_;
  std::string tool_tip_;
  std::unique_ptr<RoutePlanner> planner_;
  std::optional<std::string> planner_paths_;
  uint64_t elevation_gen_ = 0;  ///< bumped whenever `pointer_elevation_` is reopened
  bool profile_open_ = false;
  std::weak_ptr<Overlay> profile_route_;
  std::string profile_key_;  ///< what the chart was last sampled from
  std::string profile_title_;
  ProfileChart profile_;
};

}  // namespace desk
}  // namespace fv
