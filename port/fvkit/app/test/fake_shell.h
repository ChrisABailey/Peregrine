// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// FakeShell — a scripted AppShell (fvkit-app-plan.md §3f).
//
// This is what rule R1 buys. Because the core never opens a dialog and instead
// STATES what it needs decided, every branch a user could take through a flow
// is reachable by setting a field here: `next_save_answer = kCancel` and the
// close aborts, and the CloseAll around it aborts with it. On Windows the
// equivalent test needed a message pump and a robot clicking buttons.
//
// A test header, not a shipped one: it lives beside the tests that use it and
// A4/A5 pick it up from here.

#ifndef FVKIT_APP_TEST_FAKE_SHELL_H_
#define FVKIT_APP_TEST_FAKE_SHELL_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/app/shell.h"

namespace fv {
namespace app {

class FakeShell : public AppShell {
 public:
  // --- what the user will answer -----------------------------------------

  // Every AskSave gets this answer unless `save_answers` has one queued.
  SaveAnswer next_save_answer = SaveAnswer::kDiscard;
  // Queued answers, consumed front-first; used where one flow prompts twice
  // ("save this one, cancel the next").
  std::vector<SaveAnswer> save_answers;

  std::vector<std::string> files_to_open;   // empty => the user cancelled
  std::string save_spec;                    // empty => the user cancelled
  int save_format = 0;
  std::optional<int> list_choice;           // nullopt => cancelled
  bool revert_answer = false;

  // --- what the core asked ------------------------------------------------

  std::vector<std::string> asked_save;      // display names, in order
  std::vector<std::string> asked_revert;
  int open_chooser_calls = 0;
  int save_chooser_calls = 0;
  std::vector<std::string> save_suggestions;
  std::vector<Status> errors;
  std::vector<std::string> hints;
  CursorId cursor = CursorId::kDefault;
  int invalidate_calls = 0;
  // A5. The COUNTS are the interesting part for hover: the pick session
  // notifies only on a change, so "one hint for three moves along the same
  // road" is the assertion, and a test that only looked at the last value
  // would pass on a session that notified every time.
  std::vector<HintText> hint_calls;
  std::vector<CursorId> cursor_calls;
  int list_calls = 0;
  std::string last_list_title;
  std::vector<std::string> last_list_rows;
  std::vector<MenuNode> context_menus;
  std::vector<PixelPoint> context_menu_points;
  // The filters the core offered the open chooser -- the union built by
  // OpenFileOverlays with no type hint is worth asserting on.
  std::vector<std::pair<std::string, std::string>> last_open_filters;
  // Every settled mode, in order (A4). An empty TypeId with a null editor is
  // "edit mode left"; the count matters as much as the values, because one
  // SetMode must produce exactly one notification.
  std::vector<std::pair<TypeId, OverlayEditor*>> editor_changes;

  // --- AppShell -----------------------------------------------------------

  SaveAnswer AskSave(const std::string& name) override {
    asked_save.push_back(name);
    if (!save_answers.empty()) {
      const SaveAnswer a = save_answers.front();
      save_answers.erase(save_answers.begin());
      return a;
    }
    return next_save_answer;
  }

  std::vector<std::string> ChooseFilesToOpen(const FileTypeDesc& d) override {
    ++open_chooser_calls;
    last_open_filters = d.open_filters;
    return files_to_open;
  }

  std::pair<std::string, int> ChooseSaveSpec(
      const FileTypeDesc&, const std::string& suggested) override {
    ++save_chooser_calls;
    save_suggestions.push_back(suggested);
    return {save_spec, save_format};
  }

  std::optional<int> ChooseFromList(const std::string& title,
                                    const std::vector<std::string>& rows) override {
    ++list_calls;
    last_list_title = title;
    last_list_rows = rows;
    return list_choice;
  }

  bool ConfirmRevert(const std::string& spec) override {
    asked_revert.push_back(spec);
    return revert_answer;
  }

  void SetCursor(CursorId c) override {
    cursor = c;
    cursor_calls.push_back(c);
  }
  void ShowHint(const HintText& h) override {
    hints.push_back(h.status);
    hint_calls.push_back(h);
  }
  void ShowContextMenu(PixelPoint at, const MenuNode& root) override {
    context_menus.push_back(root);
    context_menu_points.push_back(at);
  }
  void RequestInvalidate() override { ++invalidate_calls; }
  void OnEditorChanged(const TypeId& id, OverlayEditor* editor) override {
    editor_changes.emplace_back(id, editor);
  }
  void ReportError(const Status& s) override { errors.push_back(s); }
};

}  // namespace app
}  // namespace fv

#endif  // FVKIT_APP_TEST_FAKE_SHELL_H_
