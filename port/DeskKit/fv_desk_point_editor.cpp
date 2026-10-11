// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_point_editor.h"

#include <memory>
#include <utility>

#include "fvkit/overlay/point_edit.h"
#include "fvkit/overlay/point_overlay.h"

namespace fv {
namespace desk {

PointEditor::PointEditor(std::function<PointOverlay*()> edited, std::function<void()> edit_point)
    : edited_(std::move(edited)), edit_point_(std::move(edit_point)) {}

Status PointEditor::Activate() {
  active_ = true;
  return Status::Ok();
}

Status PointEditor::Deactivate() {
  active_ = false;
  return Status::Ok();
}

std::vector<app::MenuNode> PointEditor::Tools() const {
  PointOverlay* o = edited_ ? edited_() : nullptr;
  const bool selected = o != nullptr && o->selected() != 0;
  // Actions look the overlay up again when run: the tree outlives the call.
  // Every tool but Add Point turns add mode off.
  auto on_points = [this](std::function<void(PointOverlay&)> fn, bool keeps_adding = false) {
    return [this, fn, keeps_adding] {
      PointOverlay* points = edited_ ? edited_() : nullptr;
      if (points == nullptr) return;
      if (!keeps_adding) points->edit().SetAdding(false);
      fn(*points);
    };
  };
  auto node = [](const char* label, const char* icon, bool enabled,
                 std::function<void()> action, bool checked = false) {
    app::MenuNode n;
    n.label = label;
    n.icon = icon;
    n.enabled = enabled;
    n.checked = checked;
    n.action = std::move(action);
    return n;
  };
  std::vector<app::MenuNode> tools;
  tools.push_back(node("Add Point (A)", "add_point", o != nullptr,
                       on_points([](PointOverlay& p) { p.edit().SetAdding(!p.edit().adding()); },
                                 true),
                       o != nullptr && o->edit().adding()));
  if (edit_point_) {
    std::function<void()> edit = edit_point_;
    tools.push_back(node("Edit Point…", "edit_point", selected,
                         on_points([edit](PointOverlay&) { edit(); })));
  }
  tools.push_back(node("Delete Point (D)", "delete_point", selected,
                       on_points([](PointOverlay& p) {
                         if (p.selected() != 0) p.edit().Delete(p.selected());
                       })));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Labels", "labels", o != nullptr,
                       on_points([](PointOverlay& p) { p.SetShowLabels(!p.show_labels()); }),
                       o != nullptr && o->show_labels()));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Undo Point Edit (U)", "undo", o != nullptr && o->edit().CanUndo(),
                       on_points([](PointOverlay& p) { p.edit().Undo(); })));
  tools.push_back(node("Redo Point Edit (Shift+U)", "redo", o != nullptr && o->edit().CanRedo(),
                       on_points([](PointOverlay& p) { p.edit().Redo(); })));
  return tools;
}

Status RegisterPointEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                           std::function<void()> edit_point) {
  app::EditorManager* em = &editors;
  const bool set = registry.SetEditorFactory(PointOverlay::kTypeId, [em, edit_point] {
    return std::make_unique<PointEditor>(
        [em] { return dynamic_cast<PointOverlay*>(em->edited()); }, edit_point);
  });
  if (!set) return Status::Error(kNotFound, "fv.points is not registered");
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
