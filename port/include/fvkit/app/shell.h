// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/app/shell.h — the seam between the app layer and a native UI
// (fvkit-app-plan-COMPLETE.md §3f, rule R1: the core never opens a dialog).
//
// A1 landed the value types the rest of the app layer passes around --
// CursorId, HintText, MenuNode. A3 adds the other half: `FlowResult`, the
// outcome of any cancelable user flow, and `AppShell` itself -- the complete
// inventory of UI the app layer needs. Nothing else in fv::app touches a user.
//
// R1 IS THE WHOLE POINT. Every place FalconView calls AfxMessageBox, a
// CFileDialog or snptodlg becomes a call on this interface: the core states
// what it needs DECIDED and never decides how to ask. A shell answers with a
// modal dialog, a sheet or a scripted table, and the flow cannot tell which --
// which is exactly why `FakeShell` (port/fvkit/app/test/fake_shell.h) makes
// every FlowResult path a unit test instead of a manual click-through.
//
// NAMESPACE DEVIATION, deliberate and noted: contract D5 says "FvKit adds no
// nested namespace". The app layer is `fv::app` -- it is a new layer above
// L0-L5 with its own plan, that plan spells `fv::app` in every code block, and
// A6 binds it as the `pyfvw.app` submodule, which mirrors the C++ nesting.
// D1-D4 and D6 apply unchanged.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/app/type_registry.h"  // FileTypeDesc, TypeId
#include "fvkit/geo.h"                // PixelPoint, Status

namespace fv {
namespace app {

class OverlayEditor;  // fvkit/app/editor.h -- which includes THIS header

// What a click at the current point would feel like. The shell maps these to
// its own art; the core never names a platform cursor. Grows as editors need
// more -- the values are not an ABI (unlike fv::Key, whose peer is Win32).
enum class CursorId {
  kDefault,
  kCrosshair,
  kHand,
  kMove,
  kNo,
  kWait,
};

// ~ FalconView's HintText (getobjpr.h): the two strings a hover produces.
// `tool_tip` is the floating one, `status` the status-bar line; either may be
// empty, and both empty means "nothing to say about this point".
struct HintText {
  std::string tool_tip;
  std::string status;

  bool empty() const { return tool_tip.empty() && status.empty(); }
};

// One node of a menu tree: context menus, editor palettes, the Tools menu.
// Data, not widgets -- the shell renders it however it renders menus.
struct MenuNode {
  std::string label;   // empty => separator
  std::string icon;    // symbolic name; the shell maps it to art. May be empty.
  bool enabled = true;
  bool checked = false;
  std::function<void()> action;  // null => submenu-only (or separator) node
  std::vector<MenuNode> children;

  bool is_separator() const { return label.empty() && children.empty(); }
};

// The outcome of a cancelable flow (rule R5). It lives here, in the shell's
// vocabulary, because the thing that turns a flow into `kCanceled` is always a
// user answering an AppShell question -- and because editor.h, which includes
// this header, returns one too (A4).
//
// The three are not interchangeable and the difference is the user's:
//   kDone     — it happened.
//   kCanceled — the USER said no. Not an error, nothing to report, and the
//               caller must abort whatever it was doing on their behalf: a
//               cancel in one Save prompt aborts the whole CloseAll, and with
//               it the application exit.
//   kFailed   — it went wrong (a save that could not write, a type that is not
//               registered). The shell has already been told through
//               ReportError; the Status is on the session's last_error().
enum class FlowResult {
  kDone,
  kCanceled,
  kFailed,
};

const char* ToString(FlowResult r);

// Implemented by the native shell -- or by a test fake. Everything here is
// synchronous from the core's perspective: a flow calls AskSave and blocks on
// the answer. A shell whose dialogs are asynchronous owns the bridging, which
// is the same stance rule R6 takes on the event loop.
class AppShell {
 public:
  virtual ~AppShell() = default;

  // --- decisions (every former modal dialog) ---------------------------

  enum class SaveAnswer { kSave, kDiscard, kCancel };

  // "Save changes to <name>?" -- the name is the overlay's file spec if it has
  // one, else its display name, because that is what the user recognises.
  virtual SaveAnswer AskSave(const std::string& overlay_display_name) = 0;

  // Multi-select open. An empty vector is a cancel; there is no separate
  // cancel signal, because "chose no files" and "cancelled" are the same
  // outcome for every caller.
  virtual std::vector<std::string> ChooseFilesToOpen(const FileTypeDesc&) = 0;

  // Path plus which `save_filters` entry was chosen; an EMPTY path is cancel.
  // The index is the `format_index` that reaches Persistence::FileSaveAs.
  virtual std::pair<std::string, int> ChooseSaveSpec(
      const FileTypeDesc&, const std::string& suggested) = 0;

  // One row out of several -- the snap-to chooser (A5) and any other
  // "which of these did you mean". nullopt = cancel.
  virtual std::optional<int> ChooseFromList(
      const std::string& title, const std::vector<std::string>& rows) = 0;

  // Re-opening a file that is already open AND dirty: discard the edits and
  // re-read from disk? (~ FalconView's revert prompt, plan §1.2.)
  virtual bool ConfirmRevert(const std::string& file_spec) = 0;

  // --- presentation ----------------------------------------------------

  virtual void SetCursor(CursorId) = 0;
  virtual void ShowHint(const HintText&) = 0;
  virtual void ShowContextMenu(PixelPoint at, const MenuNode& root) = 0;
  // Redraw SCHEDULING stays shell-owned, and it is whole-view on purpose --
  // see the plan's §6 note on why region invalidation is omitted until a
  // profile asks for it.
  virtual void RequestInvalidate() = 0;
  // Null editor = edit mode left. A4 is what calls this.
  virtual void OnEditorChanged(const TypeId&, OverlayEditor*) = 0;
  // A failure the user should hear about. Never called for kCanceled.
  virtual void ReportError(const Status&) = 0;
};

}  // namespace app
}  // namespace fv
