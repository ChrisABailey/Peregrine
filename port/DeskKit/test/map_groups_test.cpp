// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include "fv_desk_map_groups.h"

using fv::desk::MapGroup;
using fv::desk::MapGroups;
using fv::view::LadderKind;

namespace {

TEST(MapGroups, BuiltinIsTheShippedFile) {
  const MapGroups builtin = MapGroups::Builtin();
  MapGroups file;
  ASSERT_TRUE(file.LoadFile(FV_MAP_GROUPS_JSON).ok());
  ASSERT_EQ(builtin.All().size(), file.All().size());
  for (size_t i = 0; i < file.All().size(); ++i) {
    EXPECT_EQ(builtin.All()[i].id, file.All()[i].id);
    EXPECT_EQ(builtin.All()[i].formats, file.All()[i].formats);
  }
}

TEST(MapGroups, BuiltinOrderAndLadders) {
  const MapGroups g = MapGroups::Builtin();
  std::vector<std::string> ids;
  for (const MapGroup& m : g.All()) ids.push_back(m.id);
  EXPECT_EQ(ids, (std::vector<std::string>{"raster", "elevation", "osm", "enc", "dnc"}));
  EXPECT_EQ(g.Find("raster")->ladder, LadderKind::kSeries);
  EXPECT_EQ(g.Find("enc")->ladder, LadderKind::kUniform);
  EXPECT_DOUBLE_EQ(g.Find("osm")->uniform_factor, 2.0);
  // DNC is catalogued under the VPF format key.
  EXPECT_EQ(g.ForFormat("vpf")->id, "dnc");
  EXPECT_EQ(g.ForFormat("tiros")->id, "raster");
  EXPECT_EQ(g.ForFormat("nitf"), nullptr);
}

TEST(MapGroups, WithDataKeepsTableOrder) {
  const MapGroups g = MapGroups::Builtin();
  const auto with = g.WithData({"enc", "geotiff", "unknown"});
  ASSERT_EQ(with.size(), 2u);
  EXPECT_EQ(with[0]->id, "raster");
  EXPECT_EQ(with[1]->id, "enc");
  EXPECT_TRUE(g.WithData({}).empty());
}

TEST(MapGroups, OneWordAddsAFormat) {
  MapGroups g;
  ASSERT_TRUE(g.LoadJson(R"({"groups":[
      {"id":"raster","title":"Raster","formats":["cadrg","geopdf"],"ladder":"series"}]})")
                  .ok());
  EXPECT_EQ(g.ForFormat("geopdf")->id, "raster");
  EXPECT_EQ(g.Find("raster")->title, "Raster");
}

TEST(MapGroups, RejectsBadTablesAndKeepsTheOldOne) {
  MapGroups g = MapGroups::Builtin();
  const char* bad[] = {
      "not json",
      R"({"groups":{}})",
      R"({"groups":[{"title":"x","formats":["a"]}]})",
      R"({"groups":[{"id":"a","formats":[]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"ladder":"diagonal"}]})",
      R"({"groups":[{"id":"a","formats":["x"]},{"id":"b","formats":["x"]}]})",
      R"({"groups":[{"id":"a","formats":["x"]},{"id":"a","formats":["y"]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"step":1}]})",
  };
  for (const char* text : bad) {
    EXPECT_FALSE(g.LoadJson(text).ok()) << text;
    EXPECT_EQ(g.All().size(), 5u) << text;
  }
}

}  // namespace
