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

#include "fv_desk_catalog_build.h"
#include "fv_desk_commands.h"
#include "fv_desk_map_groups.h"
#include "fv_desk_menu_model.h"
#include "fv_desk_options.h"
#include "fv_desk_workspace.h"
#include "fv_view_map_view.h"
#include "fvkit/app/editor.h"
#include "fvkit/app/session.h"
#include "fvkit/app/shell.h"
#include "fvkit/app/type_registry.h"
#include "fvkit/overlay/manager.h"

namespace fv {
class Catalog;
class Settings;

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
};

/// The status bar content (port/desktop-plan.md §1d), as text.
struct StatusBar {
  std::string scale;    ///< "1:50,000"
  std::string product;  ///< the series drawn, e.g. "cadrg GNC"; empty when none
  std::string message;  ///< e.g. the end of the raster ladder
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
  const std::string& catalog_path() const { return catalog_path_; }

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
  void RegisterGroupCommands();
  void RegisterProjectionCommands();
  void RegisterOverlayTypeCommands();
  void RegisterInstanceCommands();
  void RegisterEditorCommands();
  void RebuildMenus();
  /// The file overlay File ▸ Save/Close act on: the current overlay when it
  /// is a file overlay. Null otherwise.
  Overlay* FileTarget() const;
  std::vector<view::LadderProduct> ProductsAt(const GeoPoint& p) const;
  void ApplyGroupToView();
  /// Logs `s` and reports it through the shell.
  app::FlowResult Report(const Status& s);
  /// Logs a warning at the caller's location and keeps it for `warnings()`.
  void Warn(std::string w, const char* file = __builtin_FILE(), int line = __builtin_LINE());

  DeskShell& shell_;
  Settings& settings_;
  MapGroups groups_;
  std::unique_ptr<ShellTap> tap_;
  app::OverlayTypeRegistry types_;
  OverlayManager overlays_;
  std::unique_ptr<app::OverlaySession> session_;
  std::unique_ptr<app::EditorManager> editors_;
  std::unique_ptr<StackHook> stack_hook_;
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
  uint64_t map_style_gen_ = 0;
  std::unique_ptr<CatalogBuild> build_;
  bool build_had_data_ = false;
};

}  // namespace desk
}  // namespace fv
