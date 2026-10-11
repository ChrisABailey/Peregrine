// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/app/undo.h"

#include <algorithm>

namespace fv {
namespace app {

void UndoStack::Push(UndoEntry entry) {
  undo_.push_back(std::move(entry));
  if (undo_.size() > depth_) undo_.erase(undo_.begin());
  redo_.clear();
  Changed();
}

Status UndoStack::Undo() {
  if (undo_.empty()) return Status::Error(kNotFound, "nothing to undo");
  UndoEntry e = std::move(undo_.back());
  undo_.pop_back();
  const Status s = e.undo ? e.undo() : Status::Ok();
  if (!s.ok()) {
    RemoveOwner(e.owner);
    return s;
  }
  redo_.push_back(std::move(e));
  Changed();
  return Status::Ok();
}

Status UndoStack::Redo() {
  if (redo_.empty()) return Status::Error(kNotFound, "nothing to redo");
  UndoEntry e = std::move(redo_.back());
  redo_.pop_back();
  const Status s = e.redo ? e.redo() : Status::Ok();
  if (!s.ok()) {
    RemoveOwner(e.owner);
    return s;
  }
  undo_.push_back(std::move(e));
  Changed();
  return Status::Ok();
}

void UndoStack::RemoveOwner(const void* owner) {
  const auto of = [owner](const UndoEntry& e) { return e.owner == owner; };
  undo_.erase(std::remove_if(undo_.begin(), undo_.end(), of), undo_.end());
  redo_.erase(std::remove_if(redo_.begin(), redo_.end(), of), redo_.end());
  Changed();
}

void UndoStack::Clear() {
  undo_.clear();
  redo_.clear();
  Changed();
}

void UndoStack::Changed() {
  ++generation_;
  if (on_change_) on_change_();
}

}  // namespace app
}  // namespace fv
