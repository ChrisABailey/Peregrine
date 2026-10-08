// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Map ▸ Options: the elevation colour breaks read as PythonView reads them,
// and a page applied through the Desk reaches the settings, the user settings
// and the DTED renderer.

#include "fv_desk_map_options.h"

#include <gtest/gtest.h>

#include "fv_desk_fake.h"
#include "fv_desk_user_settings.h"
#include "fvkit/formats/dted_shaded.h"
#include "fvkit/settings.h"

using fv::app::PropertyValue;
using fv::desk::FormatElevationBands;
using fv::desk::ParseElevationBands;

namespace {

/// Restores FalconView's default breakpoints at scope exit.
struct BandsGuard {
  ~BandsGuard() { fv::SetDtedShadedElevationBands({}); }
};

TEST(ElevationBands, ParseLikePythonView) {
  EXPECT_EQ(ParseElevationBands("2500, 5000,7500"), (std::vector<int>{2500, 5000, 7500}));
  EXPECT_EQ(ParseElevationBands(""), std::vector<int>{});
  // Garbage is skipped; ';' separates too; halves round to even.
  EXPECT_EQ(ParseElevationBands("x, 100; 50.5, 200.5,, 0x10"), (std::vector<int>{50, 100, 200}));
  // Sorted first, then the lowest five kept.
  EXPECT_EQ(ParseElevationBands("6,5,4,3,2,1"), (std::vector<int>{1, 2, 3, 4, 5}));
  EXPECT_EQ(ParseElevationBands(" 300 , 1e3"), (std::vector<int>{300, 1000}));
  EXPECT_EQ(FormatElevationBands({2500, 5000}), "2500,5000");
  EXPECT_EQ(FormatElevationBands({}), "");
}

TEST(MapOptions, MapOptionsShowsAnElevationPage) {
  fv::desk::FakeDesk fake;
  ASSERT_TRUE(fake.desk().commands().IsEnabled("map.options"));
  ASSERT_TRUE(fake.desk().Execute("map.options").ok());
  ASSERT_NE(fake.shell().map_options_shown, nullptr);
  const auto& pages = fake.shell().map_options_shown->pages();
  ASSERT_EQ(pages.size(), 1u);
  EXPECT_EQ(pages[0]->id(), "elevation");
  EXPECT_EQ(pages[0]->title(), "Elevation");
  ASSERT_EQ(pages[0]->sections().size(), 1u);
  EXPECT_EQ(pages[0]->sections()[0].keys, std::vector<std::string>{"elevation_bands_ft"});
}

TEST(MapOptions, TheFieldShowsTheBreaksThatWillBeDrawn) {
  fv::desk::FakeDesk fake;
  fake.settings().Set("dted.elevation_bands_ft", "300, 100");
  auto model = fake.desk().MapOptions();
  fv::desk::OptionsPage* page = model->Page("elevation");
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(page->Field("elevation_bands_ft")->value.s, "100,300");
  ASSERT_TRUE(page->Set("elevation_bands_ft", PropertyValue::String("9; 7, x")).ok());
  EXPECT_EQ(page->Field("elevation_bands_ft")->value.s, "7,9");
  EXPECT_EQ(page->Set("elevation_bands_ft", PropertyValue::Int(3)).code, fv::kInvalidArg);
}

TEST(MapOptions, ApplyReachesSettingsUserSettingsAndTheRenderer) {
  BandsGuard guard;
  fv::desk::FakeDesk fake;
  fv::desk::UserSettings user;
  fake.desk().SetUserSettings(&user);
  auto model = fake.desk().MapOptions();
  ASSERT_TRUE(model->Page("elevation")
                  ->Set("elevation_bands_ft", PropertyValue::String("5000,2500"))
                  .ok());
  const uint64_t gen = fake.desk().MapStyleGeneration();
  const int invalidates = fake.shell().invalidate_calls;
  ASSERT_TRUE(fake.desk().ApplyMapOptions(*model).ok());
  EXPECT_FALSE(model->dirty());
  EXPECT_EQ(fake.settings().GetString("dted.elevation_bands_ft"), "2500,5000");
  EXPECT_EQ(user.Get("dted.elevation_bands_ft"), "2500,5000");
  EXPECT_EQ(fv::DtedShadedElevationBands(), (std::vector<int>{2500, 5000}));
  EXPECT_GT(fake.desk().MapStyleGeneration(), gen);
  EXPECT_GT(fake.shell().invalidate_calls, invalidates);

  // Nothing changed: no new base map.
  const uint64_t after = fake.desk().MapStyleGeneration();
  ASSERT_TRUE(fake.desk().ApplyMapOptions(*model).ok());
  EXPECT_EQ(fake.desk().MapStyleGeneration(), after);
}

TEST(MapOptions, SettingsLoadedPushesTheBreaksIntoTheRenderer) {
  BandsGuard guard;
  fv::desk::FakeDesk fake;
  fake.settings().Set("dted.elevation_bands_ft", "200,100");
  const uint64_t gen = fake.desk().MapStyleGeneration();
  fake.desk().SettingsLoaded();
  EXPECT_EQ(fv::DtedShadedElevationBands(), (std::vector<int>{100, 200}));
  EXPECT_GT(fake.desk().MapStyleGeneration(), gen);
}

TEST(MapOptions, ARegisteredSourceReplacesTheBuiltinForItsGroup) {
  fv::desk::FakeDesk fake;
  fv::desk::MapOptionsSource none;
  none.group_id = "elevation";
  fake.desk().RegisterMapOptions(none);
  EXPECT_TRUE(fake.desk().MapOptions()->pages().empty());
}

}  // namespace
