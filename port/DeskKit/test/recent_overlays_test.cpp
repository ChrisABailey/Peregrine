// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "fv_desk_fake.h"
#include "fv_desk_user_settings.h"
#include "fvkit/overlay/feature_overlay.h"
#include "scratch.h"

namespace {

using fv::desk::Desk;
using fv::desk::FakeDesk;
using fv::desk::MenuItem;
using fv::desk::UserSettings;

/// A FakeDesk over a user-settings store, with copies of the rivers layer to open.
class RecentOverlaysTest : public ::testing::Test {
 protected:
  void SetUp() override { fd_.desk().SetUserSettings(&user_); }

  /// Copies the rivers layer to `name` in the scratch directory and returns its path.
  std::string Rivers(const std::string& name) {
    const std::string path = scratch_.Path(name);
    if (!std::filesystem::exists(path)) std::filesystem::copy_file(FV_FEATURE_RIVERS, path);
    return path;
  }
  void Open(const std::string& path) {
    fd_.shell().files_to_open = {path};
    ASSERT_TRUE(fd_.Run({"overlay.open"}).ok());
    ASSERT_TRUE(fd_.shell().errors.empty()) << fd_.shell().errors[0].message;
  }
  /// The labels of File ▸ Recent Overlay; empty when the submenu is absent.
  std::vector<std::string> RecentMenu() {
    std::vector<std::string> labels;
    for (const MenuItem& item : fd_.desk().menus().Find("file")->items) {
      if (item.label != "Recent Overlay") continue;
      for (const MenuItem& child : item.children) labels.push_back(child.label);
    }
    return labels;
  }
  Desk& desk() { return fd_.desk(); }

  fv::desk::test::Scratch scratch_;
  UserSettings user_;
  FakeDesk fd_{400, 400};
};

TEST_F(RecentOverlaysTest, OpenedFilesListMostRecentFirstUpToFive) {
  EXPECT_TRUE(RecentMenu().empty());
  for (const char* name : {"a.geojson", "b.geojson", "c.geojson", "d.geojson", "e.geojson", "f.geojson"})
    Open(Rivers(name));
  EXPECT_EQ(RecentMenu(),
            (std::vector<std::string>{"f.geojson", "e.geojson", "d.geojson", "c.geojson", "b.geojson"}));
  EXPECT_EQ(desk().recent_overlays()[0].type, fv::FeatureOverlay::kTypeId);

  // Opening a listed file again moves it to the top rather than listing it twice.
  ASSERT_TRUE(fd_.Run({"file.close"}).ok());
  ASSERT_TRUE(fd_.Run({"file.recent.3"}).ok());  // c.geojson
  EXPECT_EQ(RecentMenu(),
            (std::vector<std::string>{"c.geojson", "f.geojson", "e.geojson", "d.geojson", "b.geojson"}));
}

TEST_F(RecentOverlaysTest, TheListIsStoredAndReadBack) {
  Open(Rivers("a.geojson"));
  Open(Rivers("b.geojson"));
  EXPECT_EQ(user_.Get("recent.overlay.0.path"), Rivers("b.geojson"));
  EXPECT_EQ(user_.Get("recent.overlay.1.path"), Rivers("a.geojson"));

  FakeDesk next{400, 400};
  next.desk().SetUserSettings(&user_);
  ASSERT_EQ(next.desk().recent_overlays().size(), 2u);
  EXPECT_EQ(next.desk().recent_overlays()[0].path, Rivers("b.geojson"));
  ASSERT_TRUE(next.Run({"file.recent.1"}).ok());
  EXPECT_EQ(next.desk().DocumentName(), "a.geojson");
}

TEST_F(RecentOverlaysTest, AFileThatNoLongerOpensLeavesTheList) {
  Open(Rivers("a.geojson"));
  Open(Rivers("b.geojson"));
  ASSERT_TRUE(fd_.Run({"file.close"}).ok());
  std::filesystem::remove(Rivers("b.geojson"));
  ASSERT_TRUE(fd_.Run({"file.recent.0"}).ok());
  EXPECT_FALSE(fd_.shell().errors.empty());
  EXPECT_EQ(RecentMenu(), (std::vector<std::string>{"a.geojson"}));
  EXPECT_FALSE(user_.Has("recent.overlay.1.path"));
}

}  // namespace
