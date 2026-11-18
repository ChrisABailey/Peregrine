// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_moving_map_editor.h"

#include <memory>
#include <string>
#include <utility>

#include "fv_desk_desk.h"
#include "fv_desk_moving_map.h"
#include "fvkit/overlay/moving_map_overlay.h"
#include "fvkit/settings.h"

namespace fv {
namespace desk {

std::vector<app::MenuNode> MovingMapEditor::Tools() const {
  Desk* desk = &desk_;
  const DeskMovingMap* mm = desk_.EditedMovingMap();
  const Settings& settings = desk_.settings();
  auto node = [](const char* label, const char* icon, bool enabled, bool checked,
                 std::function<void()> action) {
    app::MenuNode n;
    n.label = label;
    n.icon = icon;
    n.enabled = enabled;
    n.checked = checked;
    n.action = std::move(action);
    return n;
  };
  // A window setting, read again when run: the tree outlives the call.
  auto flag = [desk](const char* key, bool fallback) {
    return [desk, key, fallback] {
      desk->SetMovingMapFlag(key, !desk->settings().GetBool(key, fallback));
    };
  };
  // Course up only acts while the map follows, which is centring on every fix.
  const bool following = settings.GetBool("movingmap.continuous", false);
  std::vector<app::MenuNode> tools;
  tools.push_back(node("Course Up", "course_up", following,
                       following && settings.GetBool("movingmap.auto_rotate", false),
                       flag("movingmap.auto_rotate", false)));
  tools.push_back(node("Centre on Every Fix", "continuous", true,
                       settings.GetBool("movingmap.continuous", false),
                       flag("movingmap.continuous", false)));
  tools.push_back(node("Trail Dots", "trail_dots", true,
                       settings.GetBool("movingmap.show_trail_dots", true),
                       flag("movingmap.show_trail_dots", true)));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Snap to Road", "snap_to_road", mm != nullptr,
                       mm != nullptr && mm->feed_config().snap_to_road,
                       [desk] { desk->ToggleMovingMapSnap(); }));
  tools.push_back(node("Listen", "listen", mm != nullptr && !mm->replay(),
                       mm != nullptr && !mm->replay() && mm->feed_config().listening,
                       [desk] { desk->ToggleMovingMapListening(); }));
  tools.push_back(node("Feed…", "feed", mm != nullptr && !mm->replay(), false,
                       [desk] { desk->OpenMovingMapFeed(); }));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Ownship…", "ownship", true, false,
                       [desk] { (void)desk->Execute("view.ownship"); }));
  tools.push_back(app::MenuNode{});
  tools.push_back(node("Trail Line", "", true, settings.GetBool("movingmap.show_trail_line", false),
                       flag("movingmap.show_trail_line", false)));
  return tools;
}

Status RegisterMovingMapEditor(app::OverlayTypeRegistry& registry, Desk& desk) {
  Desk* d = &desk;
  if (!registry.SetEditorFactory(MovingMapOverlay::kTypeId,
                                 [d] { return std::make_unique<MovingMapEditor>(*d); }))
    return Status::Error(kNotFound, "fv.movingmap is not registered");
  return Status::Ok();
}

}  // namespace desk
}  // namespace fv
