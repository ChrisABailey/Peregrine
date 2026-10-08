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
  bool status_changed = false;   ///< the status bar text changed
  bool menus_changed = false;    ///< rebuild the menu bar and toolbar
  bool catalog_changed = false;  ///< a different catalog file is open
  bool job_changed = false;      ///< a background job started, progressed or ended
  bool options_requested = false;  ///< show an options dialog; see TakeOptionsRequest
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
  /// Moves the camera and chooses the product there (`Desk::GoTo`).
  void GoTo(double lat, double lon, double scale_denom);
  double CenterLat() const;
  double CenterLon() const;
  double ScaleDenom() const;
  /// Executes a DeskKit command by id; returns the error message or "".
  std::string Execute(const std::string& command_id);
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

  // MARK: Settings

  /// Loads peregrine.ini (`fv::Settings::LoadDefault`) and the user settings
  /// file at `user_settings_path` ("" for `DefaultUserSettingsPath()`), lays
  /// the user settings over it, and applies the map options. Applied options
  /// are saved to that file, unless it exists and would not read. Returns the
  /// first error message, or "".
  std::string LoadSettings(const std::string& user_settings_path);

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
  void PointerDown(double x, double y);
  void PointerDrag(double x, double y);
  void PointerUp(double x, double y);
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
