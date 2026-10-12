// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_host.h — the one object a native shell drives.
///
/// `DeskHost` owns a `Desk`, its settings, a shell that queues what the core
/// tells the UI, and the render worker. The interface is plain values (doubles,
/// strings, a pixel pointer) so a Swift, GTK or WinUI shell calls it directly;
/// this header includes nothing but the standard library. Single-threaded
/// apart from the render worker: call every method from the UI thread.
///
/// A shell calls `Tick()` once per display refresh, redraws when it says so,
/// and paints the last frame through `Placement()` so a gesture moves the
/// picture before the next frame is ready.
///
/// The worker draws the base map, keeps it while the view is unchanged, and
/// draws the overlay stack over a copy while holding a stack lock. `Execute`
/// takes that lock, so a command never runs against an overlay being drawn.
/// A newer view stops the worker between map frames; a newer view or an
/// overlay change stops it between overlays, so the lock waits for at most
/// one overlay.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#if __has_include(<swift/bridging>)
#include <swift/bridging>
#else
#define SWIFT_SHARED_REFERENCE(_retain, _release)
#define SWIFT_RETURNS_RETAINED
#endif

namespace fv {
namespace desk {
class DeskHost;
}  // namespace desk
}  // namespace fv

/// Reference counting for Swift's import of `DeskHost` as a class.
void fv_desk_host_retain(fv::desk::DeskHost* host);
void fv_desk_host_release(fv::desk::DeskHost* host);

namespace fv {
namespace desk {

/// What changed since the previous `Tick()`.
struct HostTick {
  bool new_frame = false;        ///< a newer frame is available
  bool redraw = false;           ///< the widget should repaint
  bool status_changed = false;   ///< the status bar text or the hover tool tip changed
  bool menus_changed = false;    ///< rebuild the menu bar and toolbar
  bool catalog_changed = false;  ///< a different catalog file is open
  bool job_changed = false;      ///< a background job started, progressed or ended
  bool options_requested = false;  ///< show an options dialog; see TakeOptionsRequest
  bool sources_requested = false;  ///< show Map Data Sources; see TakeSourcesRequest
  bool profile_requested = false;  ///< show the route profile; see TakeRouteProfileRequest
  bool profile_changed = false;    ///< the route profile was re-sampled; lay it out again
  bool layers_requested = false;   ///< show the layers panel; see TakeLayersRequest
  bool layers_changed = false;     ///< the layer rows changed; reload the panel
  bool form_requested = false;     ///< show the feature form; see TakeFeatureFormRequest
  bool table_requested = false;    ///< show the attribute table; see TakeAttributeTableRequest
  bool style_requested = false;    ///< show the style dialog; see TakeLayerStyleRequest
  bool fields_requested = false;   ///< show Layer Fields; see TakeLayerFieldsRequest
  bool new_layer_requested = false;  ///< show New Layer; see TakeNewLayerRequest
  bool properties_requested = false;  ///< show Properties; see TakeLayerPropertiesRequest
  /// A feature dialog (form, table, style, fields, new layer) opened, closed
  /// or reloaded, or the table's selection moved; re-read them (a closed one
  /// reads as not open).
  bool features_changed = false;
};

/// Shortcut modifier bits (`fv::desk::Modifier`). Primary is Command on
/// macOS and Ctrl elsewhere; Control is the macOS Control key.
enum HostModifier : unsigned {
  kHostPrimary = 1u << 0,
  kHostShift = 1u << 1,
  kHostAlt = 1u << 2,
  kHostControl = 1u << 3,
};

enum HostMenuKind : int {
  kMenuTop = 0,        ///< a menu-bar title
  kMenuSubmenu = 1,    ///< a submenu title
  kMenuCommand = 2,    ///< an item that executes `id`
  kMenuSeparator = 3,
};

/// One entry of the menu bar or toolbar, flattened in preorder: the entries
/// of a menu or submenu follow it at `depth + 1`.
struct HostMenuEntry {
  HostMenuKind kind = kMenuSeparator;
  int depth = 0;
  std::string id;          ///< menu id ("file", "editor") or command id
  std::string label;
  std::string icon;        ///< symbolic name; the shell maps it to art
  std::string key;         ///< "S", "=", "PageUp"…; "" for no shortcut
  unsigned modifiers = 0;  ///< HostModifier bits
  bool checkable = false;  ///< shows a checked state (a toggle or radio item)
};

/// A question the core is asking the user (fvkit's AppShell and DeskShell).
enum HostRequestKind : int {
  kRequestNone = 0,
  kRequestAskSave = 1,          ///< save `title`? answer 0 save, 1 discard, 2 cancel
  kRequestChooseOpen = 2,       ///< files to open; items are filters
  kRequestChooseSave = 3,       ///< a file to save as; items are filters; index = filter
  kRequestChooseFromList = 4,   ///< one of the items; answer its index
  kRequestConfirmRevert = 5,    ///< revert `title`? answer 1 for yes
  kRequestChooseDirectory = 6,  ///< a directory; `title` is the prompt
};

struct HostRequest {
  HostRequestKind kind = kRequestNone;
  std::string title;
  std::string suggested_name;  ///< kRequestChooseSave
  std::string directory;       ///< where a file chooser starts; "" for the default
  bool multiple = false;       ///< kRequestChooseOpen may return several files
  int item_count = 0;          ///< filters or list rows
};

/// Called on the UI thread, inside the host call that raised the question.
/// The shell reads `PendingRequest()`, asks the user modally and answers with
/// `AnswerIndex` / `AnswerPath` before returning; no answer is a cancel.
using HostRequestFn = void (*)(void* context);

/// The two options dialogs (port/desktop-plan.md §1c).
enum HostOptionsKind : int {
  kOptionsMap = 0,      ///< Map ▸ Options
  kOptionsOverlay = 1,  ///< Overlay ▸ Options
};

/// A field's control; the values match `fv::app::PropertyType`.
enum HostFieldType : int {
  kFieldBool = 0,    ///< value "true" / "false"
  kFieldInt = 1,
  kFieldDouble = 2,
  kFieldString = 3,
  kFieldColor = 4,   ///< value "#RRGGBBAA"
  kFieldChoice = 5,  ///< value is the index into the choices
  kFieldPath = 6,    ///< a file or directory; value is the path
};

/// One control of an options page. Values travel as text in the settings
/// spelling (except a choice, which is its index).
struct HostOptionsField {
  std::string key;
  std::string label;
  std::string section;  ///< the group title; may be empty
  std::string help;     ///< tooltip; may be empty
  HostFieldType type = kFieldBool;
  std::string value;
  bool changed = false;  ///< differs from the applied value
  double min = 0;        ///< kFieldInt / kFieldDouble; min == max is unbounded
  double max = 0;
  int choice_count = 0;    ///< kFieldChoice; labels from OptionsChoiceLabel
  bool directory = false;  ///< kFieldPath names a directory
  std::string path_patterns;  ///< kFieldPath file filters, "*.gpx;*.json"; "" = any
};

/// Where the shown frame belongs under the live view, in points:
/// x' = a*x + c*y + tx, y' = b*x + d*y + ty. Invalid when there is nothing to
/// place (no frame, or no common ground).
struct FramePlacement {
  double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
  bool valid = false;
};

/// The route profile's plot box, in points from the window's top-left.
struct HostChartBox {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool valid = false;  ///< false: draw only `RouteProfileMessage()`, centred
};

class SWIFT_SHARED_REFERENCE(fv_desk_host_retain, fv_desk_host_release) DeskHost {
 public:
  /// A host with an empty catalog and a reference count of one. Registers
  /// fvkit's built-in raster formats.
  static DeskHost* Create() SWIFT_RETURNS_RETAINED;

  DeskHost(const DeskHost&) = delete;
  DeskHost& operator=(const DeskHost&) = delete;

  // MARK: Catalog and commands

  /// Opens a catalog database and centres the view on its data. Returns an
  /// empty string on success, else the error message.
  std::string OpenCatalog(const std::string& path);
  std::string CatalogPath() const;
  /// The file name the window title shows: the current overlay's, else the
  /// topmost file overlay's; "unnamed.<ext>" before a first save; empty when
  /// no file overlay is open.
  std::string DocumentName() const;
  /// Moves the camera and chooses the product there (`Desk::GoTo`).
  void GoTo(double lat, double lon, double scale_denom);
  double CenterLat() const;
  double CenterLon() const;
  double ScaleDenom() const;
  /// Executes a DeskKit command by id; returns the error message or "".
  std::string Execute(const std::string& command_id);
  /// True when an overlay type opens `path` by its extension.
  bool OpensAsOverlay(const std::string& path) const;
  /// Opens an overlay file by its extension (`OverlaySession::OpenFile`).
  /// Returns the error message or ""; the error is also queued for `TakeError`.
  std::string OpenOverlayFile(const std::string& path);
  /// The next error the core reported through the shell, or "" when none.
  std::string TakeError();
  /// The next informational message, or "" when none.
  std::string TakeNotice();
  /// True once the app.quit command has closed every document.
  bool QuitRequested() const;

  // MARK: Menus and toolbar

  /// Changes whenever the menu bar or toolbar does.
  uint64_t MenuGeneration() const;
  /// The menu bar: File, Map, Overlay and, while an editor is active, its menu.
  int MenuEntryCount() const;
  HostMenuEntry MenuEntryAt(int index) const;
  /// The toolbar: commands with icons, and separators, all at depth 0.
  int ToolbarEntryCount() const;
  HostMenuEntry ToolbarEntryAt(int index) const;
  /// False for an unknown command.
  bool IsEnabled(const std::string& command_id) const;
  bool IsChecked(const std::string& command_id) const;

  // MARK: Questions

  /// Without a handler every question is answered as a cancel.
  void SetRequestHandler(HostRequestFn fn, void* context);
  HostRequest PendingRequest() const;
  /// A filter's label ("Peregrine Workspace (*.fvws)") or a list row.
  std::string RequestItemLabel(int index) const;
  /// A filter's pattern ("*.fvws", "*.a;*.b"); "" for a list row.
  std::string RequestItemPattern(int index) const;
  void AnswerIndex(int index);
  /// Adds a chosen file or directory; call once per file.
  void AnswerPath(const std::string& path);

  // MARK: Options dialogs

  /// The dialog the core asked to show since the last call (a HostOptionsKind),
  /// or -1. Its model stays open until `CloseOptions`.
  int TakeOptionsRequest();
  /// False until the dialog of `kind` was requested, and after it closed.
  bool OptionsOpen(int kind) const;
  int OptionsPageCount(int kind) const;
  std::string OptionsPageTitle(int kind, int page) const;
  std::string OptionsPageIcon(int kind, int page) const;
  /// The page's fields in section order.
  int OptionsFieldCount(int kind, int page) const;
  HostOptionsField OptionsFieldAt(int kind, int page, int field) const;
  std::string OptionsChoiceLabel(int kind, int page, int field, int choice) const;
  /// Parses `value` as the field's type and offers it to the page; the
  /// accepted value may be clamped (re-read the field). Returns the error
  /// message, or "".
  std::string SetOptionValue(int kind, int page, const std::string& key,
                             const std::string& value);
  /// The page's fields back to their declared defaults (not yet applied).
  std::string ResetOptionsPage(int kind, int page);
  bool OptionsDirty(int kind) const;
  /// Writes the changes to the settings, the user settings file and the
  /// open overlays or the map renderer; the dialog stays open. Returns the
  /// error message, or "".
  std::string ApplyOptions(int kind);
  /// Every page back to its applied values.
  void RevertOptions(int kind);
  /// Discards the dialog's model (unapplied changes are lost).
  void CloseOptions(int kind);

  // MARK: Map Data Sources dialog

  /// True once after the core asked to show the Map Data Sources dialog.
  bool TakeSourcesRequest();
  /// The directories Generate Coverage scans, absolute (`Desk::ScanRoots`).
  int ScanRootCount() const;
  std::string ScanRootAt(int index) const;
  /// False when the directory does not exist now, such as an unmounted volume.
  bool ScanRootReachable(int index) const;
  /// Each returns the error message, or "".
  std::string AddScanRoot(const std::string& path);
  std::string RemoveScanRoot(const std::string& path);
  /// Removes all coverage and rescans every listed directory as a background job.
  std::string GenerateCoverage();

  // MARK: Layers panel

  /// True once after the core asked to show the layers panel (Overlay ▸ Layers…).
  bool TakeLayersRequest();
  /// Rows top of the stack first, as of the last `Tick` or layer change
  /// (`Desk::layers()`). Out-of-range rows read as empty.
  int LayerCount() const;
  std::string LayerName(int row) const;
  std::string LayerTypeName(int row) const;
  std::string LayerIcon(int row) const;
  bool LayerVisible(int row) const;
  bool LayerHasOpacity(int row) const;  ///< false: show no opacity control
  int LayerOpacity(int row) const;      ///< 0..100
  bool LayerCurrent(int row) const;
  bool LayerDirty(int row) const;
  /// Each returns the error message, or "", and repaints.
  std::string SetLayerVisible(int row, bool visible);
  std::string SetLayerOpacity(int row, int percent);
  std::string MoveLayer(int from, int to);
  std::string SelectLayer(int row);

  // MARK: Layer manager (Sources view)

  /// Sources topmost first, as of the same snapshot as the layer rows
  /// (`Desk::sources()`). Out-of-range sources read as empty.
  int SourceCount() const;
  std::string SourceName(int source) const;
  std::string SourcePath(int source) const;  ///< "" for an overlay without a file
  std::string SourceTypeName(int source) const;
  std::string SourceIcon(int source) const;
  /// How many layers are shown: 0 none, 1 some, 2 all.
  int SourceShown(int source) const;
  bool SourceCurrent(int source) const;
  bool SourceDirty(int source) const;
  int SourceLayerCount(int source) const;
  /// The `i`th layer of the source (top first) as a layer row for `LayerName`
  /// and its siblings; -1 out of range.
  int SourceLayerRow(int source, int i) const;
  /// Each returns an error message, "" on success.
  std::string SetSourceVisible(int source, bool visible);
  std::string MoveSource(int from, int to);
  std::string SelectSource(int source);
  /// The right-click menus (`Desk::LayerMenu`, `Desk::SourceMenu`), flattened
  /// at depth 0. They act on the current layer and its source, so the shell
  /// selects the row first, then runs the chosen item through `Execute`.
  int LayerMenuEntryCount() const;
  HostMenuEntry LayerMenuEntryAt(int index) const;
  int SourceMenuEntryCount() const;
  HostMenuEntry SourceMenuEntryAt(int index) const;

  // MARK: Properties sheet

  /// True once after the core asked to show the Properties sheet.
  bool TakeLayerPropertiesRequest();
  std::string LayerPropertiesTitle() const;
  /// Rows in order; a row whose section differs from the one before starts a new section.
  int LayerPropertyCount() const;
  std::string LayerPropertySection(int i) const;
  std::string LayerPropertyKey(int i) const;
  std::string LayerPropertyValue(int i) const;
  /// Declares that the shell's Properties dialog shows a layer's General,
  /// Symbology, Labels and Fields tabs (`DeskShell::HostsPropertiesTabs`);
  /// Layer Style… and Layer Fields… then open Properties at their tab. Off
  /// until called.
  void SetPropertiesTabs(bool tabbed);
  /// The tabs Properties shows, in order, each a `fv::desk::PropertiesTab`:
  /// 0 Source (the rows above), 1 General, 2 Symbology, 3 Labels, 4 Fields.
  /// The style tabs read the layer style accessors, the Fields tab the layer
  /// fields accessors. Out of range reads as -1 and "".
  int LayerPropertiesTabCount() const;
  int LayerPropertiesTabAt(int i) const;
  std::string LayerPropertiesTabTitle(int i) const;
  /// The tab to open at, a `fv::desk::PropertiesTab`.
  int LayerPropertiesTab() const;
  bool LayerPropertiesDirty() const;
  /// Applies every tab with changes and repaints; ask about
  /// `LayerFieldsLoss()` first. Returns the error message, or "".
  std::string ApplyLayerProperties();
  std::string RevertLayerProperties();
  /// The dialog closed; its tabs' unapplied changes are dropped.
  void CloseLayerProperties();
  /// The General tab: false for a source or a shell without tabs.
  bool LayerGeneralOpen() const;
  std::string LayerGeneralName() const;
  std::string LayerGeneralDescription() const;
  std::string LayerGeneralCredits() const;
  /// The scale range as "1:50,000"; "" for no limit. The minimum is the scale
  /// zoomed furthest out at which the layer still draws, the maximum the
  /// scale zoomed furthest in.
  std::string LayerGeneralMinScale() const;
  std::string LayerGeneralMaxScale() const;
  /// Replaces the General draft, the scales as typed ("1:50,000", "50000",
  /// "" for no limit). Returns the error message, or "".
  std::string SetLayerGeneral(const std::string& name, const std::string& description, const std::string& credits,
                              const std::string& min_scale, const std::string& max_scale);

  // MARK: Feature form

  /// True once after the core asked to show the feature form (Edit Feature…,
  /// Edit Point…, a table row).
  bool TakeFeatureFormRequest();
  bool FeatureFormOpen() const;
  std::string FeatureFormTitle() const;
  /// Controls in form order. Out-of-range indexes read as empty.
  int FormFieldCount() const;
  /// A `fv::desk::FormControl` value: 0 text, 1 multi-line, 2 integer,
  /// 3 number, 4 check box, 5 date, 6 date and time, 7 date range, 8 choice,
  /// 9 phone, 10 URL, 11 email, 12 read-only.
  int FormFieldControl(int i) const;
  std::string FormFieldTitle(int i) const;
  std::string FormFieldDescription(int i) const;
  std::string FormFieldUnit(int i) const;
  bool FormFieldRequired(int i) const;
  int FormFieldChoiceCount(int i) const;
  std::string FormFieldChoice(int i, int choice) const;
  std::string FormFieldText(int i) const;
  std::string FormFieldEndText(int i) const;  ///< a date range's end
  /// The last `ValidateFeatureForm` or apply's complaint about this control; "".
  std::string FormFieldError(int i) const;
  /// tel:, mailto: or a web URL for the control's text; "" when it has no action.
  std::string FormFieldActionUrl(int i) const;
  /// Each returns the error message, or "".
  std::string SetFormFieldText(int i, const std::string& text);
  std::string SetFormFieldEndText(int i, const std::string& text);
  bool FeatureFormDirty() const;
  std::string ValidateFeatureForm();
  /// Writes the form to the layer and repaints; the form stays open.
  std::string ApplyFeatureForm();
  std::string RevertFeatureForm();
  void CloseFeatureForm();

  // MARK: Attribute table

  /// True once after the core asked to show the attribute table.
  bool TakeAttributeTableRequest();
  bool AttributeTableOpen() const;
  std::string AttributeTableTitle() const;
  int TableColumnCount() const;
  std::string TableColumnTitle(int column) const;
  bool TableColumnNumeric(int column) const;  ///< align right
  int TableRowCount() const;
  std::string TableCell(int row, int column) const;
  /// `column` -1 sorts by feature id.
  void SortTable(int column, bool ascending);
  int TableSortColumn() const;
  bool TableSortAscending() const;
  void SetTableFilter(const std::string& text);
  /// Re-reads the layer, for edits made outside the form (drags, undo).
  std::string ReloadAttributeTable();
  /// Selects the row's feature on the map. Returns the error message, or "".
  std::string SelectTableRow(int row);
  /// The row of the selected feature; -1 for none.
  int SelectedTableRow() const;
  /// Opens the feature form on the row's feature.
  std::string EditTableRow(int row);
  void CloseAttributeTable();

  // MARK: Layer style

  /// True once after the core asked to show the style dialog (Layer Style…,
  /// Style This Feature…).
  bool TakeLayerStyleRequest();
  bool LayerStyleOpen() const;
  std::string LayerStyleTitle() const;
  /// True when the dialog styles one feature: one entry, no renderer controls.
  bool LayerStyleIsFeature() const;
  bool LayerStyleDirty() const;
  /// 0 single, 1 categorised, 2 graduated.
  int StyleRenderer() const;
  /// Each setter returns the error message, or "".
  std::string SetStyleRenderer(int renderer);
  /// The fields the current renderer can classify by.
  int StyleFieldCount() const;
  std::string StyleFieldName(int i) const;
  std::string StyleFieldTitle(int i) const;
  std::string StyleField() const;
  std::string SetStyleField(const std::string& name);
  /// Graduated options: class count, mode (0 equal interval, 1 quantile),
  /// vary (0 colour ramp, 1 size).
  int StyleClassCount() const;
  int StyleClassMode() const;
  int StyleClassVary() const;
  /// Sets the graduated options (ignored for categorised) and reclassifies.
  std::string ClassifyStyle(int classes, int mode, int vary);
  /// Legend rows: the classes, then "Other"; or the one symbol.
  int StyleEntryCount() const;
  bool StyleEntryIsOther(int i) const;
  std::string StyleEntryLabel(int i) const;
  /// A category value or class lower bound; "" for other rows.
  std::string StyleEntryValue(int i) const;
  std::string SetStyleEntryLabel(int i, const std::string& label);
  std::string SetStyleEntryValue(int i, const std::string& value);
  std::string AddStyleEntry();
  std::string RemoveStyleEntry(int i);
  /// Copies entry `i`'s swatch, RGBA8 on opaque white, rows of `width * 4`
  /// bytes, into `out`. False when nothing is open or it does not fit.
  bool CopyStyleSwatch(int i, int width, int height, double scale, uint8_t* out, size_t capacity) const;
  /// The symbol properties the layer's geometry shows, in dialog order;
  /// `k` below indexes this list.
  int StylePropertyCount() const;
  /// 0 colour, 1 number (pixels), 2 choice.
  int StylePropertyKind(int k) const;
  std::string StylePropertyTitle(int k) const;
  /// "Point", "Line" or "Area".
  std::string StylePropertyGroup(int k) const;
  int StylePropertyChoiceCount(int k) const;
  std::string StylePropertyChoice(int k, int choice) const;
  /// Entry `i`'s property `k`; a colour is 0xRRGGBBAA.
  uint32_t StyleColor(int i, int k) const;
  double StyleNumber(int i, int k) const;
  int StyleChoice(int i, int k) const;
  std::string SetStyleColor(int i, int k, uint32_t rgba);
  std::string SetStyleNumber(int i, int k, double value);
  std::string SetStyleChoice(int i, int k, int choice);
  /// Whether the styled feature has its own symbol; false for a layer style.
  bool StyleHasOverride() const;
  std::string ClearStyleOverride();
  /// Fields a label can show (any but blobs).
  int StyleLabelFieldCount() const;
  std::string StyleLabelFieldName(int i) const;
  std::string StyleLabelFieldTitle(int i) const;
  /// "" = no labels.
  std::string StyleLabelField() const;
  double StyleLabelSize() const;
  std::string SetStyleLabel(const std::string& field, double size_px);
  /// Writes the draft to the overlay and repaints; the dialog stays open.
  std::string ApplyLayerStyle();
  std::string RevertLayerStyle();
  void CloseLayerStyle();

  // MARK: Layer fields

  /// True once after the core asked to show the Layer Fields dialog.
  bool TakeLayerFieldsRequest();
  bool LayerFieldsOpen() const;
  std::string LayerFieldsTitle() const;
  bool LayerFieldsDirty() const;
  /// The storage types and roles the editor's pop-ups offer.
  int FieldTypeCount() const;
  std::string FieldTypeTitle(int k) const;
  int FieldRoleCount() const;
  std::string FieldRoleTitle(int k) const;
  int LayerFieldCount() const;
  std::string LayerFieldName(int i) const;
  /// The field's name when the dialog opened; "" for a new field.
  std::string LayerFieldOriginalName(int i) const;
  /// Indexes into the type and role pop-ups; -1 for a type the editor does
  /// not offer (binary).
  int LayerFieldType(int i) const;
  int LayerFieldRole(int i) const;
  std::string LayerFieldTitle(int i) const;
  std::string LayerFieldDescription(int i) const;
  std::string LayerFieldUnit(int i) const;
  bool LayerFieldRequired(int i) const;
  /// The default, minimum and maximum as typed text; "" for none.
  std::string LayerFieldDefault(int i) const;
  std::string LayerFieldMin(int i) const;
  std::string LayerFieldMax(int i) const;
  /// One choice per line, "value" or "value = Label".
  std::string LayerFieldChoices(int i) const;
  /// The index of the new field.
  int AddLayerField();
  /// Each setter returns the error message, or "".
  std::string RemoveLayerField(int i);
  std::string MoveLayerField(int from, int to);
  /// Replaces every property of field `i` at once, from the editor's controls.
  std::string SetLayerField(int i, const std::string& name, int type, int role, const std::string& title,
                            const std::string& description, const std::string& unit, bool required,
                            const std::string& default_text, const std::string& min_text,
                            const std::string& max_text, const std::string& choices);
  /// What applying would lose, as sentences; "" when nothing. Ask before
  /// `ApplyLayerFields` when it is not empty.
  std::string LayerFieldsLoss() const;
  std::string ApplyLayerFields();
  std::string RevertLayerFields();
  void CloseLayerFields();

  // MARK: New layer

  /// True once after the core asked to show the New Layer dialog.
  bool TakeNewLayerRequest();
  bool NewLayerOpen() const;
  int NewLayerTemplateCount() const;
  std::string NewLayerTemplateTitle(int i) const;
  int NewLayerTemplate() const;
  std::string SetNewLayerTemplate(int i);
  int NewLayerGeometryCount() const;
  std::string NewLayerGeometryTitle(int k) const;
  int NewLayerGeometry() const;
  std::string SetNewLayerGeometry(int k);
  std::string NewLayerName() const;
  void SetNewLayerName(const std::string& name);
  /// 0 GeoPackage, 1 GeoJSON.
  int NewLayerFormat() const;
  std::string SetNewLayerFormat(int format);
  std::string NewLayerPath() const;
  void SetNewLayerPath(const std::string& path);
  /// The template's fields, one "Title — Type" per line.
  std::string NewLayerFieldSummary() const;
  /// Writes the layer and opens it; the dialog closes on success.
  std::string CreateNewLayer();
  void CloseNewLayer();

  // MARK: Route elevation profile

  /// True once after the core asked to show the route profile window.
  bool TakeRouteProfileRequest();
  bool RouteProfileOpen() const;
  /// The window closed; the core stops re-sampling.
  void CloseRouteProfile();
  std::string RouteProfileTitle() const;
  /// The summary line: distance, min, max, gain, loss, sample count.
  std::string RouteProfileStats() const;
  /// Lays the chart out for a window of that size, in points; the accessors
  /// below read that layout. Call again after `profile_changed` or a resize.
  void LayoutRouteProfile(double width_pt, double height_pt);
  HostChartBox RouteProfileBox() const;
  /// Shown instead of the plot when there is nothing to draw.
  std::string RouteProfileMessage() const;
  /// Axis 0 is distance (x positions), 1 is elevation (y positions).
  int RouteProfileTickCount(int axis) const;
  double RouteProfileTickPos(int axis, int index) const;
  std::string RouteProfileTickLabel(int axis, int index) const;
  /// The terrain in runs of known ground; a gap in the data ends a run.
  int RouteProfileRunCount() const;
  int RouteProfileRunLength(int run) const;
  double RouteProfileRunX(int run, int index) const;
  double RouteProfileRunY(int run, int index) const;
  /// x of each interior waypoint.
  int RouteProfileVertexCount() const;
  double RouteProfileVertexX(int index) const;
  /// Distance, elevation and position of the sample nearest `x_pt` in the
  /// last layout; "" outside the plot.
  std::string RouteProfileReadout(double x_pt) const;

  // MARK: Settings

  /// Loads peregrine.ini (`fv::Settings::LoadDefault`) and the user settings
  /// file at `user_settings_path` ("" for `DefaultUserSettingsPath()`), lays
  /// the user settings over it, and applies the map options. Applied options
  /// are saved to that file, unless it exists and would not read. Returns the
  /// first error message, or "".
  std::string LoadSettings(const std::string& user_settings_path);

  // MARK: Application log

  /// Starts the log file in `directory` ("" for the platform's log directory)
  /// at the `log.level` setting, and writes a first line with `app_version`,
  /// the OS, the settings files and the catalog. Call after LoadSettings; a
  /// second call does nothing. Returns the error message, or "".
  std::string StartLog(const std::string& directory, const std::string& app_version);
  /// The file the log is writing now, or "" before StartLog.
  std::string LogFilePath() const;

  // MARK: Background jobs

  /// True while a catalog build runs.
  bool JobActive() const;
  /// 0…1, by data sources finished.
  double JobFraction() const;
  /// What the job is doing now, for a progress line.
  std::string JobText() const;
  /// Stops the job after the data source in progress.
  void CancelJob();

  // MARK: Surface and input (points, top-left origin)

  void Resize(double width_pt, double height_pt, double display_scale,
              double mm_per_point);
  void Hover(double x, double y);
  void HoverExit();
  /// The floating tip for what is under the pointer: an overlay's hit, else
  /// the base map's named feature; empty for none.
  std::string HoverToolTip() const;
  /// While an overlay editor is active the press goes to the overlay stack
  /// first; the drag and release follow it there when an overlay took it,
  /// and pan the map otherwise.
  void PointerDown(double x, double y);
  /// `shift` extends a feature selection (shift-click, shift-drag box).
  void PointerDown(double x, double y, bool shift);
  /// `clicks` is 2 for the second press of a double-click.
  void PointerDown(double x, double y, bool shift, int clicks);
  /// A press that pans the map whatever overlay is being edited (the space
  /// bar held); the drag and release follow it.
  void PanPointerDown(double x, double y);
  void PointerDrag(double x, double y);
  void PointerUp(double x, double y);
  /// A key press for the overlay being edited. `key` is an fv::Key value
  /// (Win32 virtual-key code; letters uppercase), `text` the character
  /// typed or 0. False when nothing used it.
  bool KeyDown(int key, uint32_t text, bool shift, bool ctrl, bool alt, bool meta);
  /// `precise` (trackpad) pans by (dx, dy); a wheel steps the ladder once per
  /// notch of `dy`, positive zooming in.
  void Scroll(double x, double y, double dx, double dy, bool precise);
  void MagnifyBegin(double x, double y);
  /// `factor` > 1 zooms in, relative to the previous event of the pinch.
  void Magnify(double x, double y, double factor);
  void MagnifyEnd(double x, double y);
  /// One ladder step about the cursor (Page Up is +1).
  void Step(int direction);

  // MARK: Frames

  /// Requests a frame when the view changed and picks up a finished one.
  HostTick Tick();
  bool HasFrame() const;
  int FrameWidth() const;   ///< pixels
  int FrameHeight() const;  ///< pixels
  /// RGBA8, premultiplied, rows of `FrameWidth() * 4` bytes. Valid until the
  /// next `Tick()`; null when there is no frame.
  const uint8_t* FramePixels() const;
  /// Copies the frame's pixels into `out`, which holds `capacity` bytes.
  /// False when there is no frame or it does not fit. The Swift shell's
  /// accessor: a pointer into a reference type is not imported.
  bool CopyFrame(uint8_t* out, size_t capacity) const;
  /// The display scale the frame was rendered at (pixels per point).
  double FrameDisplayScale() const;
  FramePlacement Placement() const;
  /// Blocks until the render worker is idle; for tests and screenshots.
  void WaitForRender();
  /// Frames delivered since creation; an interrupted render is not counted.
  uint64_t FramesRendered() const;

  // MARK: Status bar

  std::string StatusScale() const;     ///< "1:50,000"
  std::string StatusProduct() const;   ///< "cadrg GNC", or "" when none
  std::string StatusMessage() const;   ///< e.g. the end of the raster ladder
  std::string StatusPosition() const;  ///< the position under the cursor, or ""
  /// The terrain height under the cursor, "1,234 ft", or "" without DTED there.
  std::string StatusElevation() const;
  /// The left-hand slot: map and overlay feedback, then the map's own message.
  std::string StatusFeedback() const;

 private:
  friend void ::fv_desk_host_retain(DeskHost*);
  friend void ::fv_desk_host_release(DeskHost*);
  struct Impl;

  DeskHost();
  ~DeskHost();

  std::atomic<int> refs_{1};
  std::unique_ptr<Impl> impl_;
};

}  // namespace desk
}  // namespace fv
