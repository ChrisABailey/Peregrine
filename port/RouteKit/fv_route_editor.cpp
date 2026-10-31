// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_route_editor.h"

#include <memory>
#include <utility>

#include "fv_route_edit.h"
#include "fv_route_overlay.h"

namespace fv {

RouteEditor::RouteEditor(std::function<RouteOverlay*()> edited, std::function<void()> open_profile)
    : edited_(std::move(edited)), open_profile_(std::move(open_profile)) {}

Status RouteEditor::Activate() {
  active_ = true;
  return Status::Ok();
}

Status RouteEditor::Deactivate() {
  active_ = false;
  return Status::Ok();
}

std::vector<app::MenuNode> RouteEditor::Tools() const {
  RouteOverlay* r = edited_ ? edited_() : nullptr;
  const bool routable = r != nullptr && r->waypoints().size() >= 2;
  // Actions look the route up again when run: the tree outlives the call.
  // Every tool but Add Point turns add mode off.
  auto on_route = [this](std::function<void(RouteOverlay&)> fn, bool keeps_adding = false) {
    return [this, fn, keeps_adding] {
      RouteOverlay* route = edited_ ? edited_() : nullptr;
      if (route == nullptr) return;
      if (!keeps_adding) route->edit().SetAdding(false);
      fn(*route);
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
  tools.push_back(node("Add Point (A)", "add_point", r != nullptr,
                       on_route([](RouteOverlay& o) { o.edit().SetAdding(!o.edit().adding()); }, true),
                       r != nullptr && r->edit().adding()));
  tools.push_back(node("Delete Point (D)", "delete_point", r != nullptr && !r->selected().empty(),
                       on_route([](RouteOverlay& o) {
                         if (!o.selected().empty()) o.edit().Delete(o.selected());
                       })));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Cycle Leg Type (G)", "leg_type", routable,
                       on_route([](RouteOverlay& o) { o.edit().CycleLegKind(); })));
  tools.push_back(node("Follow Roads (R)", "follow_roads", routable,
                       on_route([](RouteOverlay& o) { o.edit().FollowRoads(); })));
  tools.push_back(node("Follow Roads by Bicycle (B)", "follow_roads_bicycle", routable,
                       on_route([](RouteOverlay& o) { o.edit().FollowRoadsByBicycle(); })));
  // Offered on a graph with elevation; kept while set so it can be cleared.
  const bool hills = r != nullptr && r->avoid_hills();
  tools.push_back(node("Avoid Hills (H)", "avoid_hills",
                       r != nullptr && (hills || r->MayHaveElevation()),
                       on_route([](RouteOverlay& o) { o.edit().ToggleAvoidHills(); }), hills));
  tools.push_back(node("Straight Legs", "straight_legs", r != nullptr && r->has_plan(),
                       on_route([](RouteOverlay& o) { o.ClearRoads(); })));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Undo Route Edit (U)", "undo", r != nullptr && r->edit().CanUndo(),
                       on_route([](RouteOverlay& o) { o.edit().Undo(); })));
  tools.push_back(node("Redo Route Edit (Shift+U)", "redo", r != nullptr && r->edit().CanRedo(),
                       on_route([](RouteOverlay& o) { o.edit().Redo(); })));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Elevation Profile", "elevation_profile", routable && open_profile_,
                       on_route([this](RouteOverlay&) { open_profile_(); })));
  return tools;
}

Status RegisterRouteEditor(app::OverlayTypeRegistry& registry, app::EditorManager& editors,
                           std::function<void()> open_profile) {
  app::EditorManager* em = &editors;
  const bool set = registry.SetEditorFactory(RouteOverlay::kTypeId, [em, open_profile] {
    return std::make_unique<RouteEditor>(
        [em] { return dynamic_cast<RouteOverlay*>(em->edited()); }, open_profile);
  });
  if (!set) return Status::Error(kNotFound, "fv.route is not registered");
  return Status::Ok();
}

}  // namespace fv
