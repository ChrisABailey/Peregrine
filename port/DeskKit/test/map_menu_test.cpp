// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The map's right-click menu through FakeDesk: Get Info on a feature, a
/// point and a route, and Edit Point… / Edit Route… entering their editors.
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "fv_desk_fake.h"
#include "fv_route_overlay.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/point_overlay.h"
#include "fvkit/raster.h"

namespace {

using fv::app::MenuNode;
using fv::desk::FakeDesk;

const std::string kGetInfo = "Get Info…";

/// The top-level item labelled `label`, or null.
const MenuNode* Item(const MenuNode& menu, const std::string& label) {
  for (const MenuNode& child : menu.children)
    if (child.label == label) return &child;
  return nullptr;
}

/// The value of the info row `key` in `section`; "" when there is none.
std::string InfoValue(const fv::app::ItemInfoSheet& sheet, const std::string& section,
                      const std::string& key) {
  for (const fv::app::InfoRow& row : sheet.rows)
    if (row.section == section && row.key == key) return row.value;
  return std::string();
}

class MapMenuTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& mv = fd_.desk().map_view();
    mv.SetViewport(mv.View().WithCamera(fv::GeoPoint{32.6, -80.0}, 50000, 0));
  }

  void Render() {
    fv::PixelBuffer out;
    ASSERT_TRUE(fd_.Render(&out).ok());
  }

  void Click(double x, double y) {
    fd_.desk().OverlayPointerDown(x, y);
    fd_.desk().OverlayPointerUp(x, y);
  }

  /// A point overlay, not in edit mode, holding one point drawn at (x, y).
  std::shared_ptr<fv::PointOverlay> PointsWithOneAt(double x, double y) {
    auto& desk = fd_.desk();
    EXPECT_TRUE(fd_.Run({std::string("overlay.new.") + fv::PointOverlay::kTypeId}).ok());
    auto points = std::dynamic_pointer_cast<fv::PointOverlay>(desk.overlays().current_ptr());
    EXPECT_NE(points, nullptr);
    if (!points) return nullptr;
    desk.editors().SetMode(fv::app::TypeId());
    const auto& v = desk.map_view().View();
    fv::GeoPoint g;
    EXPECT_TRUE(v.Projection().SurfaceToGeo(x * v.DisplayScale(), y * v.DisplayScale(), &g).ok());
    fv::MapPoint mp;
    mp.name = "Fort Moultrie";
    mp.category = "fort";
    mp.position = g;
    points->AddPoint(mp);
    return points;
  }

  FakeDesk fd_{400, 300};
};

TEST_F(MapMenuTest, NothingUnderThePointGivesAnEmptyMenu) {
  Render();
  EXPECT_TRUE(fd_.desk().MapContextMenu(200, 150).children.empty());
}

TEST_F(MapMenuTest, GetInfoOnAFeatureShowsItsAttributes) {
  auto& desk = fd_.desk();
  ASSERT_TRUE(fd_.Run({std::string("overlay.new.") + fv::FeatureOverlay::kTypeId}).ok());
  ASSERT_NE(desk.new_layer(), nullptr);
  desk.new_layer()->SetTarget(fv::desk::NewLayerTarget::kUnsaved);
  ASSERT_TRUE(desk.new_layer()->SetGeometry(fv::GeometryType::kGeometry).ok());
  ASSERT_TRUE(desk.CreateNewLayer().ok());
  auto* overlay = dynamic_cast<fv::FeatureOverlay*>(desk.overlays().current());
  ASSERT_NE(overlay, nullptr);
  Render();
  for (const std::string& id : desk.commands().IdsWithPrefix("editor.tool."))
    if (desk.commands().Find(id)->label == "Add Point") ASSERT_TRUE(fd_.Run({id}).ok());
  Click(200, 150);
  const int64_t id = overlay->selected();
  ASSERT_NE(id, 0);
  desk.editors().SetMode(fv::app::TypeId());
  Render();

  const MenuNode menu = desk.MapContextMenu(200, 150);
  const MenuNode* info = Item(menu, kGetInfo);
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(Item(menu, "Edit Point…"), nullptr);
  info->action();
  EXPECT_EQ(fd_.shell().item_info_shown, 1);
  fv::Feature f;
  ASSERT_TRUE(overlay->layer()->Get(id, &f).ok());
  EXPECT_EQ(desk.item_info().title, overlay->FeatureTitle(f));
  EXPECT_EQ(InfoValue(desk.item_info(), "Feature", "Layer"), overlay->Name());
  EXPECT_EQ(InfoValue(desk.item_info(), "Feature", "Geometry"), "Point");
}

TEST_F(MapMenuTest, EditPointPutsThePointsInTheirEditorWithThePointSelected) {
  auto& desk = fd_.desk();
  auto points = PointsWithOneAt(200, 150);
  ASSERT_NE(points, nullptr);
  // A second overlay on top, so the edit has to raise the points.
  ASSERT_TRUE(fd_.Run({std::string("overlay.new.") + fv::PointOverlay::kTypeId}).ok());
  desk.editors().SetMode(fv::app::TypeId());
  ASSERT_NE(desk.overlays().TopOfWorking(), points);
  Render();

  const MenuNode menu = desk.MapContextMenu(200, 150);
  const MenuNode* info = Item(menu, kGetInfo);
  ASSERT_NE(info, nullptr);
  info->action();
  EXPECT_EQ(desk.item_info().title, "Fort Moultrie");
  EXPECT_EQ(InfoValue(desk.item_info(), "Point", "Category"), "fort");
  EXPECT_FALSE(InfoValue(desk.item_info(), "Point", "Position").empty());

  const MenuNode* edit = Item(menu, "Edit Point…");
  ASSERT_NE(edit, nullptr);
  edit->action();
  EXPECT_EQ(fd_.shell().editor_mode, fv::PointOverlay::kTypeId);
  EXPECT_EQ(desk.editors().edited(), points.get());
  EXPECT_EQ(desk.overlays().TopOfWorking(), points);
  EXPECT_EQ(points->selected(), points->points()[0].id);
  EXPECT_TRUE(fd_.shell().errors.empty());
}

TEST_F(MapMenuTest, GetInfoAndEditRouteFromTheRouteLine) {
  auto& desk = fd_.desk();
  ASSERT_TRUE(fd_.Run({std::string("editor.mode.") + fv::RouteOverlay::kTypeId}).ok());
  auto route = std::dynamic_pointer_cast<fv::RouteOverlay>(
      desk.overlays().FirstOfType(fv::RouteOverlay::kTypeId));
  ASSERT_NE(route, nullptr);
  Render();
  fv::KeyEvent a;
  a.key = 'A';
  ASSERT_TRUE(desk.OverlayKeyDown(a));
  Click(100, 150);
  Click(300, 150);
  ASSERT_EQ(route->waypoints().size(), 2u);
  fv::KeyEvent escape;
  escape.key = fv::Key::kEscape;
  desk.OverlayKeyDown(escape);
  desk.editors().SetMode(fv::app::TypeId());
  // A point at the first waypoint and over the route.
  auto points = PointsWithOneAt(100, 150);
  ASSERT_NE(points, nullptr);
  Render();

  // Mid-leg: only the route answers.
  const MenuNode menu = desk.MapContextMenu(200, 150);
  const MenuNode* info = Item(menu, kGetInfo);
  ASSERT_NE(info, nullptr);
  info->action();
  EXPECT_EQ(desk.item_info().title, route->Name());
  EXPECT_EQ(InfoValue(desk.item_info(), "Route", "Waypoints"), "2");
  const std::string leg = route->waypoints()[0].label + " → " + route->waypoints()[1].label;
  EXPECT_NE(InfoValue(desk.item_info(), "Legs", leg).find(" km"), std::string::npos);

  // On the shared spot both answer, each Get Info naming its item.
  const MenuNode both = desk.MapContextMenu(100, 150);
  EXPECT_NE(Item(both, "Get Info: Fort Moultrie…"), nullptr);
  EXPECT_NE(Item(both, "Get Info: " + route->Name() + "…"), nullptr);
  EXPECT_EQ(Item(both, kGetInfo), nullptr);

  const MenuNode* edit = Item(both, "Edit Route…");
  ASSERT_NE(edit, nullptr);
  edit->action();
  EXPECT_EQ(fd_.shell().editor_mode, fv::RouteOverlay::kTypeId);
  EXPECT_EQ(desk.editors().edited(), route.get());
  EXPECT_EQ(desk.overlays().TopOfWorking(), route);
  EXPECT_EQ(route->selected(), route->waypoints()[0].label);
  EXPECT_TRUE(fd_.shell().errors.empty());
}

}  // namespace
