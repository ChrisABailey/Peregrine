// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/app/undo.h — one undo history shared by every editor in a workspace
/// (port/desktop-plan.md §2b). Editors push entries; Edit ▸ Undo and Redo
/// drive the stack without knowing which editor made an entry.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace app {

/// One user action as a pair of functions that put the document back and
/// forward. `owner` identifies the document, so its entries can be dropped
/// when it closes or is reloaded.
struct UndoEntry {
  std::string label;  ///< "Move Vertex", shown as "Undo Move Vertex"
  const void* owner = nullptr;
  std::function<Status()> undo;
  std::function<Status()> redo;
};

class UndoStack {
 public:
  /// The oldest entries are dropped past `depth`.
  explicit UndoStack(size_t depth = 200) : depth_(depth > 0 ? depth : 1) {}

  UndoStack(const UndoStack&) = delete;
  UndoStack& operator=(const UndoStack&) = delete;

  /// Records an action already done; the redo history is discarded.
  void Push(UndoEntry entry);

  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  /// The label of the entry `Undo`/`Redo` would run; empty when there is none.
  std::string UndoLabel() const { return undo_.empty() ? std::string() : undo_.back().label; }
  std::string RedoLabel() const { return redo_.empty() ? std::string() : redo_.back().label; }

  /// Runs the newest entry's `undo` and moves it to the redo history. An
  /// entry whose function fails is discarded, with the rest of the history
  /// of its owner, since later entries assume the state it failed to restore.
  Status Undo();
  Status Redo();

  /// Drops every entry of `owner`, in both histories.
  void RemoveOwner(const void* owner);
  void Clear();

  size_t undo_count() const { return undo_.size(); }
  size_t redo_count() const { return redo_.size(); }

  /// Changes on every push, undo, redo and removal.
  uint64_t generation() const { return generation_; }
  /// Called after every change, on the thread that made it.
  void SetOnChange(std::function<void()> fn) { on_change_ = std::move(fn); }

 private:
  void Changed();

  size_t depth_;
  std::vector<UndoEntry> undo_;
  std::vector<UndoEntry> redo_;
  uint64_t generation_ = 0;
  std::function<void()> on_change_;
};

}  // namespace app
}  // namespace fv
