// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fv_route_editor.h"

#include <gtest/gtest.h>

#include <memory>

#include "fv_route_edit.h"
#include "fv_route_overlay.h"
#include "fvkit/overlay/manager.h"
#include "../../fvkit/app/test/fake_shell.h"

namespace {

using fv::RouteEditor;
using fv::RouteOverlay;

const fv::app::MenuNode* Find(const std::vector<fv::app::MenuNode>& tools, const std::string& prefix) {
  for (const auto& n : tools)
    if (n.label.compare(0, prefix.size(), prefix) == 0) return &n;
  return nullptr;
}

TEST(RouteEditor, ToolsFollowTheEditedRoute) {
  RouteOverlay route;
  RouteOverlay* edited = nullptr;
  RouteEditor editor([&] { return edited; });

  // Nothing being edited: every tool is greyed.
  for (const auto& n : editor.Tools())
    if (!n.is_separator()) EXPECT_FALSE(n.enabled) << n.label;

  edited = &route;
  auto tools = editor.Tools();
  EXPECT_TRUE(Find(tools, "Add Point")->enabled);
  EXPECT_FALSE(Find(tools, "Delete Point")->enabled);
  EXPECT_FALSE(Find(tools, "Follow Roads (R)")->enabled);

  Find(tools, "Add Point")->action();
  EXPECT_TRUE(route.edit().adding());
  EXPECT_TRUE(Find(editor.Tools(), "Add Point")->checked);

  route.edit().SetAdding(false);
  route.edit().AddAt(fv::GeoPoint{32.6, -80.0});
  route.edit().AddAt(fv::GeoPoint{32.7, -80.1});
  tools = editor.Tools();
  ASSERT_EQ(route.waypoints().size(), 2u);
  EXPECT_TRUE(Find(tools, "Cycle Leg Type")->enabled);
  EXPECT_TRUE(Find(tools, "Undo Route Edit")->enabled);

  // AddAt selects the new point, so Delete acts on it.
  ASSERT_FALSE(route.selected().empty());
  Find(tools, "Delete Point")->action();
  EXPECT_EQ(route.waypoints().size(), 1u);
  Find(editor.Tools(), "Undo Route Edit")->action();
  EXPECT_EQ(route.waypoints().size(), 2u);
}

TEST(RouteEditor, AnotherToolTurnsAddModeOff) {
  RouteOverlay route;
  RouteEditor editor([&] { return &route; });
  Find(editor.Tools(), "Add Point")->action();
  ASSERT_TRUE(route.edit().adding());
  Find(editor.Tools(), "Cycle Leg Type")->action();
  EXPECT_FALSE(route.edit().adding());
  Find(editor.Tools(), "Add Point")->action();
  Find(editor.Tools(), "Add Point")->action();
  EXPECT_FALSE(route.edit().adding());
}

TEST(RouteEditor, EveryToolHasAnIcon) {
  RouteOverlay route;
  RouteEditor editor([&] { return &route; });
  for (const auto& n : editor.Tools())
    if (!n.is_separator()) EXPECT_FALSE(n.icon.empty()) << n.label;
  EXPECT_EQ(Find(editor.Tools(), "Follow Roads by Bicycle")->icon, "follow_roads_bicycle");
}

TEST(RouteEditor, ElevationProfileNeedsAHandlerAndTwoWaypoints) {
  RouteOverlay route;
  int opened = 0;
  RouteEditor without([&] { return &route; });
  RouteEditor with([&] { return &route; }, [&] { ++opened; });
  route.edit().AddAt(fv::GeoPoint{32.6, -80.0});
  EXPECT_FALSE(Find(with.Tools(), "Elevation Profile")->enabled);
  route.edit().AddAt(fv::GeoPoint{32.7, -80.1});
  EXPECT_FALSE(Find(without.Tools(), "Elevation Profile")->enabled);
  const auto tools = with.Tools();
  const fv::app::MenuNode* profile = Find(tools, "Elevation Profile");
  ASSERT_TRUE(profile->enabled);
  EXPECT_EQ(profile->icon, "elevation_profile");
  profile->action();
  EXPECT_EQ(opened, 1);
}

TEST(RouteEditor, RegistersOnlyOverTheRouteType) {
  fv::app::OverlayTypeRegistry types;
  fv::OverlayManager overlays;
  fv::app::FakeShell shell;
  fv::app::EditorManager editors(types, overlays, shell);
  EXPECT_FALSE(fv::RegisterRouteEditor(types, editors).ok());
  ASSERT_TRUE(fv::RegisterRouteOverlayType(types).ok());
  ASSERT_TRUE(fv::RegisterRouteEditor(types, editors).ok());
  const auto* desc = types.Find(RouteOverlay::kTypeId);
  ASSERT_TRUE(desc && desc->editor_factory);
  EXPECT_NE(desc->editor_factory(), nullptr);
}

}  // namespace
