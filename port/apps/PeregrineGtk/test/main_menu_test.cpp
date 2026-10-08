// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// main_menu_test.cpp — the GTK menu bar, actions and shortcuts generated
/// from a real DeskHost. Needs GIO but no display.

#include "main_menu.h"
#include "map_canvas.h"

#include <gtest/gtest.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fv_desk_catalog_build.h"

namespace fs = std::filesystem;

namespace peregrine {
namespace {

struct HostRelease {
  void operator()(fv::desk::DeskHost* h) const { fv_desk_host_release(h); }
};
using HostPtr = std::unique_ptr<fv::desk::DeskHost, HostRelease>;

/// A host plus the menus over it; every activation runs the command.
class MainMenuTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Gio::init();
    host_.reset(fv::desk::DeskHost::Create());
    menus_ = std::make_unique<CommandMenus>(*host_, [this](const std::string& id) {
      executed_.push_back(id);
      host_->Execute(id);
    });
    ASSERT_TRUE(menus_->Rebuild());
  }

  /// Command ids of the host's menu bar, in order.
  std::vector<std::string> MenuCommands() const {
    std::vector<std::string> ids;
    for (int i = 0; i < host_->MenuEntryCount(); ++i) {
      const fv::desk::HostMenuEntry e = host_->MenuEntryAt(i);
      if (e.kind == fv::desk::kMenuCommand) ids.push_back(e.id);
    }
    return ids;
  }

  Glib::RefPtr<Gio::SimpleAction> Action(const std::string& command) const {
    const std::string detailed = menus_->ActionFor(command);
    const std::string name = detailed.substr(detailed.find('.') + 1);
    return std::dynamic_pointer_cast<Gio::SimpleAction>(menus_->actions()->lookup_action(name));
  }

  HostPtr host_;
  std::unique_ptr<CommandMenus> menus_;
  std::vector<std::string> executed_;
};

/// Every "action" attribute in `model` and its submenus and sections.
void CollectActions(const Glib::RefPtr<Gio::MenuModel>& model, std::multiset<std::string>* out) {
  for (int i = 0; i < model->get_n_items(); ++i) {
    Glib::VariantBase value =
        model->get_item_attribute(i, Gio::MenuModel::Attribute::ACTION, Glib::VARIANT_TYPE_STRING);
    if (value) out->insert(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get());
    for (const auto link : {Gio::MenuModel::Link::SUBMENU, Gio::MenuModel::Link::SECTION}) {
      if (auto child = model->get_item_link(i, link)) CollectActions(child, out);
    }
  }
}

TEST(Accelerator, MapsDeskKitShortcutsToGtkSpelling) {
  using fv::desk::kHostAlt;
  using fv::desk::kHostPrimary;
  using fv::desk::kHostShift;
  EXPECT_EQ(AcceleratorFor("S", kHostPrimary), "<Primary>s");
  EXPECT_EQ(AcceleratorFor("S", kHostPrimary | kHostShift), "<Primary><Shift>s");
  EXPECT_EQ(AcceleratorFor("O", kHostPrimary | kHostAlt), "<Primary><Alt>o");
  EXPECT_EQ(AcceleratorFor("=", kHostPrimary), "<Primary>equal");
  EXPECT_EQ(AcceleratorFor("-", kHostPrimary), "<Primary>minus");
  EXPECT_EQ(AcceleratorFor("PageUp", 0), "Page_Up");
  EXPECT_EQ(AcceleratorFor("Space", kHostShift), "<Shift>space");
  EXPECT_EQ(AcceleratorFor("F5", 0), "F5");
  EXPECT_EQ(AcceleratorFor("", kHostPrimary), "");
  EXPECT_EQ(AcceleratorFor("F99", 0), "");
  EXPECT_EQ(AcceleratorFor("Bogus", kHostPrimary), "");
}

TEST(Accelerator, EveryMappedSpellingParses) {
  for (const char* key : {"S", "=", "-", "0", "PageDown", "Delete", "Escape", "F12"}) {
    const std::string accel = AcceleratorFor(key, fv::desk::kHostPrimary);
    guint keyval = 0;
    GdkModifierType mods{};
    EXPECT_TRUE(gtk_accelerator_parse(accel.c_str(), &keyval, &mods)) << key << " -> " << accel;
    EXPECT_NE(keyval, 0u) << accel;
    EXPECT_TRUE(mods & GDK_CONTROL_MASK) << accel;
  }
}

TEST(ActionName, ReplacesCharactersGActionNamesCannotHold) {
  EXPECT_EQ(ActionNameFor("file.save_as"), "file.save-as");
  EXPECT_EQ(ActionNameFor("map.group.raster"), "map.group.raster");
  EXPECT_TRUE(g_action_name_is_valid(ActionNameFor("overlay.new.fv:points 2").c_str()));
}

/// Composes `PlacementTransform(p)` back into an affine matrix.
void ExpectPlacementRoundTrips(const fv::desk::FramePlacement& p) {
  GskTransform* t = PlacementTransform(p);
  EXPECT_GE(gsk_transform_get_category(t), GSK_TRANSFORM_CATEGORY_2D);
  float xx, yx, xy, yy, dx, dy;
  gsk_transform_to_2d(t, &xx, &yx, &xy, &yy, &dx, &dy);
  gsk_transform_unref(t);
  EXPECT_NEAR(xx, p.a, 1e-5);
  EXPECT_NEAR(yx, p.b, 1e-5);
  EXPECT_NEAR(xy, p.c, 1e-5);
  EXPECT_NEAR(yy, p.d, 1e-5);
  EXPECT_NEAR(dx, p.tx, 1e-4);
  EXPECT_NEAR(dy, p.ty, 1e-4);
}

TEST(PlacementTransform, IsA2dTransformEqualToThePlacement) {
  fv::desk::FramePlacement p;
  p.valid = true;
  ExpectPlacementRoundTrips(p);  // identity
  p.a = 0.5; p.d = 0.5; p.tx = 120; p.ty = -40;
  ExpectPlacementRoundTrips(p);  // a pinch preview
  const double r = 0.4;
  p.a = 1.3 * std::cos(r); p.b = 1.3 * std::sin(r);
  p.c = -1.3 * std::sin(r); p.d = 1.3 * std::cos(r);
  ExpectPlacementRoundTrips(p);  // rotated and scaled
  p.c += 0.2;
  ExpectPlacementRoundTrips(p);  // sheared
}

TEST_F(MainMenuTest, EveryMenuCommandHasAnActionAndOneMenuItem) {
  const std::vector<std::string> commands = MenuCommands();
  ASSERT_FALSE(commands.empty());
  std::multiset<std::string> in_menu;
  CollectActions(menus_->menu_bar(), &in_menu);
  for (const std::string& id : commands) {
    ASSERT_FALSE(menus_->ActionFor(id).empty()) << id;
    EXPECT_TRUE(Action(id)) << id;
    EXPECT_EQ(in_menu.count(menus_->ActionFor(id)), 1u) << id;
  }
  EXPECT_EQ(in_menu.size(), commands.size());
}

TEST_F(MainMenuTest, TopLevelMenusFollowTheModel) {
  std::vector<std::string> titles;
  for (int i = 0; i < host_->MenuEntryCount(); ++i) {
    const fv::desk::HostMenuEntry e = host_->MenuEntryAt(i);
    if (e.kind == fv::desk::kMenuTop) titles.push_back(e.label);
  }
  const auto bar = menus_->menu_bar();
  ASSERT_EQ(bar->get_n_items(), static_cast<int>(titles.size()));
  for (int i = 0; i < bar->get_n_items(); ++i) {
    Glib::VariantBase label =
        bar->get_item_attribute(i, Gio::MenuModel::Attribute::LABEL, Glib::VARIANT_TYPE_STRING);
    ASSERT_TRUE(label);
    EXPECT_EQ(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(label).get().raw(),
              titles[i]);
  }
}

TEST_F(MainMenuTest, ToolbarCommandsHaveActions) {
  ASSERT_FALSE(menus_->toolbar().empty());
  EXPECT_FALSE(menus_->toolbar().front().command.empty());
  EXPECT_FALSE(menus_->toolbar().back().command.empty());
  for (const ToolbarItem& item : menus_->toolbar()) {
    if (item.command.empty()) continue;
    EXPECT_EQ(item.action, menus_->ActionFor(item.command));
    EXPECT_TRUE(Action(item.command)) << item.command;
  }
}

TEST_F(MainMenuTest, ShortcutsComeFromTheCommands) {
  std::set<std::string> accels;
  bool save = false, quit = false;
  for (const auto& [action, accel] : menus_->accelerators()) {
    EXPECT_TRUE(accels.insert(accel).second) << accel << " is bound twice";
    if (action == menus_->ActionFor("file.save")) save = accel == "<Primary>s";
    if (action == menus_->ActionFor("app.quit")) quit = accel == "<Primary>q";
  }
  EXPECT_TRUE(save);
  EXPECT_TRUE(quit);
}

TEST_F(MainMenuTest, ActionsFollowEnabledState) {
  for (const std::string& id : MenuCommands())
    EXPECT_EQ(Action(id)->get_enabled(), host_->IsEnabled(id)) << id;
  // Nothing is open, so there is nothing to save.
  EXPECT_FALSE(Action("file.save")->get_enabled());
}

TEST_F(MainMenuTest, ActivatingARadioItemExecutesItAndMovesTheCheck) {
  std::string target, previous;
  for (const std::string& id : MenuCommands()) {
    if (id.rfind("map.projection.", 0) != 0) continue;
    if (host_->IsChecked(id)) previous = id;
    else if (target.empty()) target = id;
  }
  ASSERT_FALSE(target.empty());
  ASSERT_FALSE(previous.empty());

  Action(target)->activate();
  ASSERT_EQ(executed_, std::vector<std::string>{target});
  EXPECT_TRUE(host_->IsChecked(target));
  menus_->Refresh();
  bool checked = false;
  Action(target)->get_state(checked);
  EXPECT_TRUE(checked);
  Action(previous)->get_state(checked);
  EXPECT_FALSE(checked);
}

TEST_F(MainMenuTest, RebuildIsANoOpUntilTheModelChanges) {
  EXPECT_FALSE(menus_->Rebuild());
}

TEST_F(MainMenuTest, MenusRebuildWhenACatalogWithDataOpens) {
  const char* data = std::getenv("FVW_TESTDATA_DIR");
  if (data == nullptr || !fs::exists(fs::path(data) / "dted"))
    GTEST_SKIP() << "no DTED in the test data";
  const fs::path dir = fs::temp_directory_path() / ("peregrine-gtk-menu-" + std::to_string(::getpid()));
  fs::create_directories(dir);
  const std::string catalog = (dir / "catalog.sqlite").string();
  {
    fv::desk::CatalogBuild build(catalog, fv::desk::PlanScan(data));
    build.Wait();
    ASSERT_FALSE(build.Results().empty()) << build.Summary();
  }

  auto groups = [this] {
    int n = 0;
    for (const std::string& id : MenuCommands()) n += id.rfind("map.group.", 0) == 0;
    return n;
  };
  const int before = groups();
  ASSERT_EQ(host_->OpenCatalog(catalog), "");
  ASSERT_TRUE(menus_->Rebuild());
  EXPECT_GT(groups(), before);
  for (const std::string& id : MenuCommands()) EXPECT_TRUE(Action(id)) << id;
  host_.reset();
  fs::remove_all(dir);
}

}  // namespace
}  // namespace peregrine
