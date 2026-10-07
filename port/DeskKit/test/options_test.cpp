// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include <memory>

#include "fv_desk_fake.h"
#include "fv_desk_options.h"
#include "fv_desk_user_settings.h"
#include "fvkit/overlay/manager.h"
#include "fvkit/settings.h"
#include "range_rings.h"

using fv::app::PropertyValue;
using fv::desk::OptionsModel;
using fv::desk::OptionsPage;
using fv::desk::test::RangeRings;

namespace {

fv::app::OverlayTypeDesc RingsType() {
  fv::app::OverlayTypeDesc d;
  d.id = "test.rings";
  d.display_name = "Range Rings";
  d.factory = [] { return std::make_shared<RangeRings>(); };
  return d;
}

TEST(OptionsPage, SectionsFollowGroupsInFirstAppearanceOrder) {
  fv::Settings settings;
  const auto page = OptionsPage::ForType(RingsType(), settings);
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(page->sections().size(), 2u);
  EXPECT_EQ(page->sections()[0].title, "Rings");
  EXPECT_EQ(page->sections()[0].keys,
            (std::vector<std::string>{"ring_count", "units", "export_dir"}));
  EXPECT_EQ(page->sections()[1].title, "Style");
  EXPECT_FALSE(page->dirty());
}

TEST(OptionsPage, NoPageForAHiddenTypeOrOneWithoutProperties) {
  fv::Settings settings;
  fv::app::OverlayTypeDesc hidden = RingsType();
  hidden.display_name.clear();
  EXPECT_EQ(OptionsPage::ForType(hidden, settings), nullptr);
  fv::app::OverlayTypeDesc plain;
  plain.id = "test.plain";
  plain.display_name = "Plain";
  plain.factory = [] { return std::make_shared<fv::Overlay>("plain"); };
  EXPECT_EQ(OptionsPage::ForType(plain, settings), nullptr);
}

TEST(OptionsPage, OpensOnTheSettingsANewOverlayWouldRead) {
  fv::Settings settings;
  settings.Set("rings.ring_count", "6");
  settings.Set("rings.units", "km");
  const auto page = OptionsPage::ForType(RingsType(), settings);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->Field("ring_count")->value.i, 6);
  EXPECT_EQ(page->Field("units")->value.i, 1);
  EXPECT_FALSE(page->dirty());
}

TEST(OptionsPage, TheOverlayValidatesAndItsClampShows) {
  fv::Settings settings;
  auto page = OptionsPage::ForType(RingsType(), settings);
  EXPECT_EQ(page->Set("ring_count", PropertyValue::Bool(true)).code, fv::kInvalidArg);
  EXPECT_EQ(page->Set("units", PropertyValue::Choice(7)).code, fv::kInvalidArg);
  EXPECT_EQ(page->Set("nope", PropertyValue::Int(1)).code, fv::kNotFound);
  EXPECT_FALSE(page->dirty());
  ASSERT_TRUE(page->Set("ring_count", PropertyValue::Int(40)).ok());
  EXPECT_EQ(page->Field("ring_count")->value.i, 10);
  EXPECT_TRUE(page->dirty());
}

TEST(OptionsPage, PendingSettingsSpellAChoiceByNameAndRevertClearsThem) {
  fv::Settings settings;
  auto page = OptionsPage::ForType(RingsType(), settings);
  ASSERT_TRUE(page->Set("units", PropertyValue::Choice(2)).ok());
  ASSERT_TRUE(page->Set("export_dir", PropertyValue::Path("/tmp/rings out")).ok());
  ASSERT_TRUE(page->Set("line_color", PropertyValue::Color(fv::FvColor{0, 0, 255, 255})).ok());
  const auto pending = page->PendingSettings();
  ASSERT_EQ(pending.size(), 3u);
  EXPECT_EQ(pending[0], (std::pair<std::string, std::string>{"rings.units", "mi"}));
  EXPECT_EQ(pending[1], (std::pair<std::string, std::string>{"rings.line_color", "#0000FFFF"}));
  EXPECT_EQ(pending[2], (std::pair<std::string, std::string>{"rings.export_dir", "/tmp/rings out"}));
  page->Revert();
  EXPECT_FALSE(page->dirty());
  EXPECT_EQ(page->Field("units")->value.i, 0);
}

TEST(OptionsPage, ResetToDefaultsIsPendingUntilApplied) {
  fv::Settings settings;
  settings.Set("rings.ring_count", "8");
  auto page = OptionsPage::ForType(RingsType(), settings);
  ASSERT_TRUE(page->ResetToDefaults().ok());
  EXPECT_EQ(page->Field("ring_count")->value.i, 3);
  ASSERT_EQ(page->PendingSettings().size(), 1u);
  // A default is written explicitly so it outranks peregrine.ini.
  EXPECT_EQ(page->PendingSettings()[0].second, "3");
}

TEST(OptionsModel, ApplyWritesSettingsUserSettingsAndOpenOverlays) {
  fv::app::OverlayTypeRegistry types;
  ASSERT_TRUE(types.Register(RingsType()).ok());
  fv::OverlayManager overlays;
  auto open = std::make_shared<RangeRings>();
  open->set_type_id("test.rings");
  overlays.Add(open);
  fv::Settings settings;
  fv::desk::UserSettings user;

  OptionsModel model(types, settings);
  ASSERT_EQ(model.pages().size(), 1u);
  ASSERT_TRUE(model.Page("test.rings")->Set("ring_count", PropertyValue::Int(5)).ok());
  ASSERT_TRUE(model.dirty());
  std::vector<std::string> warnings;
  ASSERT_TRUE(model.Apply(settings, overlays, &user, &warnings).ok());

  EXPECT_EQ(settings.GetString("rings.ring_count"), "5");
  EXPECT_EQ(user.Get("rings.ring_count"), "5");
  EXPECT_FALSE(user.Has("rings.units"));  // only what changed
  EXPECT_EQ(open->GetInt("ring_count", -1), 5);
  EXPECT_FALSE(model.dirty());
  EXPECT_TRUE(warnings.empty());

  // A fresh model reads the applied value back.
  OptionsModel again(types, settings);
  EXPECT_EQ(again.Page("test.rings")->Field("ring_count")->value.i, 5);
}

TEST(OptionsModel, BuiltinGridHasAGeneratedPage) {
  fv::desk::FakeDesk fake;
  const auto model = fake.desk().OverlayOptions();
  OptionsPage* grid = model->Page(fv::app::kGridTypeId);
  ASSERT_NE(grid, nullptr);
  EXPECT_FALSE(grid->fields().empty());
  EXPECT_FALSE(grid->sections().empty());
}

TEST(OptionsModel, DeskApplyRepaintsAndPersistsThroughUserSettings) {
  fv::desk::FakeDesk fake;
  fv::desk::UserSettings user;
  fake.desk().SetUserSettings(&user);
  ASSERT_TRUE(fake.desk().types().Register(RingsType()).ok());
  const auto model = fake.desk().OverlayOptions();
  ASSERT_TRUE(model->Page("test.rings")->Set("units", PropertyValue::Choice(1)).ok());
  const int before = fake.shell().invalidate_calls;
  ASSERT_TRUE(fake.desk().ApplyOverlayOptions(*model).ok());
  EXPECT_GT(fake.shell().invalidate_calls, before);
  EXPECT_EQ(user.Get("rings.units"), "km");
  EXPECT_EQ(fake.settings().GetString("rings.units"), "km");
}

}  // namespace
