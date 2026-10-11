// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_desk_feature_editor.h"

#include <memory>
#include <utility>

#include "fvkit/overlay/feature_edit.h"
#include "fvkit/overlay/feature_overlay.h"

namespace fv {
namespace desk {

FeatureEditor::FeatureEditor(std::function<FeatureOverlay*()> edited, std::function<void()> edit_feature)
    : edited_(std::move(edited)), edit_feature_(std::move(edit_feature)) {}

FeatureEditor::FeatureEditor(std::function<FeatureOverlay*()> edited, std::vector<FeatureLayerTool> layer_tools)
    : edited_(std::move(edited)), layer_tools_(std::move(layer_tools)) {}

Status FeatureEditor::Activate() {
  active_ = true;
  return Status::Ok();
}

Status FeatureEditor::Deactivate() {
  active_ = false;
  return Status::Ok();
}

std::vector<app::MenuNode> FeatureEditor::Tools() const {
  FeatureOverlay* o = edited_ ? edited_() : nullptr;
  const FeatureEditSession* edit = o != nullptr ? &o->edit() : nullptr;
  // Actions look the overlay up again when run: the tree outlives the call.
  auto on_edit = [this](std::function<void(FeatureOverlay&)> fn) {
    return [this, fn] {
      FeatureOverlay* f = edited_ ? edited_() : nullptr;
      if (f != nullptr) fn(*f);
    };
  };
  auto node = [](const char* label, const char* icon, bool enabled, std::function<void()> action,
                 bool checked = false) {
    app::MenuNode n;
    n.label = label;
    n.icon = icon;
    n.enabled = enabled;
    n.checked = checked;
    n.action = std::move(action);
    return n;
  };
  auto tool = [&](const char* label, const char* icon, FeatureTool t) {
    return node(label, icon, edit != nullptr && edit->ToolAvailable(t),
                on_edit([t](FeatureOverlay& f) { f.edit().SetTool(t); }),
                edit != nullptr && edit->tool() == t);
  };

  const bool selected = o != nullptr && !o->selection().empty();
  const VertexRef vertex = edit != nullptr ? edit->active_vertex() : VertexRef{};
  std::vector<app::MenuNode> tools;
  tools.push_back(tool("Select", "select", FeatureTool::kSelect));
  tools.push_back(tool("Add Point", "add_point", FeatureTool::kAddPoint));
  tools.push_back(tool("Draw Line", "draw_line", FeatureTool::kDrawLine));
  tools.push_back(tool("Draw Area", "draw_area", FeatureTool::kDrawArea));
  tools.push_back(tool("Add Hole", "add_hole", FeatureTool::kAddHole));
  tools.push_back(node("Finish Shape (Return)", "finish", edit != nullptr && edit->sketching(),
                       on_edit([](FeatureOverlay& f) { f.edit().FinishSketch(); })));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Delete Vertex", "delete_vertex", vertex.valid(), on_edit([](FeatureOverlay& f) {
                         const VertexRef v = f.edit().active_vertex();
                         if (v.valid()) f.edit().DeleteVertex(v);
                       })));
  tools.push_back(node("Delete Hole", "delete_hole", vertex.valid() && vertex.path > 0,
                       on_edit([](FeatureOverlay& f) { f.edit().RemoveHole(f.edit().active_vertex()); })));
  tools.push_back(node("Delete Feature", "delete_feature", selected,
                       on_edit([](FeatureOverlay& f) { f.edit().DeleteSelected(); })));
  if (edit_feature_) {
    std::function<void()> open = edit_feature_;
    tools.push_back(node("Edit Feature…", "edit_point", selected, on_edit([open](FeatureOverlay&) { open(); })));
  }
  if (!layer_tools_.empty()) {
    tools.push_back(app::MenuNode{});
    for (const FeatureLayerTool& t : layer_tools_)
      tools.push_back(node(t.label.c_str(), t.icon.c_str(), o != nullptr && (!t.enabled || t.enabled()), t.run));
  }
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Undo", "undo", edit != nullptr && edit->CanUndo(),
                       on_edit([](FeatureOverlay& f) { f.edit().Undo(); })));
  tools.push_back(node("Redo", "redo", edit != nullptr && edit->CanRedo(),
                       on_edit([](FeatureOverlay& f) { f.edit().Redo(); })));
  return tools;
}

Status RegisterFeatureEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                             std::function<void()> edit_feature) {
  app::EditorManager* em = &editors;
  const bool set = registry.SetEditorFactory(FeatureOverlay::kTypeId, [em, edit_feature] {
    return std::make_unique<FeatureEditor>([em] { return dynamic_cast<FeatureOverlay*>(em->edited()); },
                                           edit_feature);
  });
  if (!set) return Status::Error(kNotFound, "fv.features is not registered");
  return Status::Ok();
}

Status RegisterFeatureEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                             std::vector<FeatureLayerTool> layer_tools) {
  app::EditorManager* em = &editors;
  const bool set = registry.SetEditorFactory(FeatureOverlay::kTypeId, [em, layer_tools] {
    return std::make_unique<FeatureEditor>([em] { return dynamic_cast<FeatureOverlay*>(em->edited()); },
                                           layer_tools);
  });
  if (!set) return Status::Error(kNotFound, "fv.features is not registered");
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
