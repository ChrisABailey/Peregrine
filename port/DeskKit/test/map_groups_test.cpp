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
      R"({"groups":[{"id":"a","formats":["x"],"scales":{}}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"scale":1000}]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"series":"s","prefix":"s","scale":1000}]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"series":"s","scale":0}]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"series":"s"}]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"format":"y","series":"s","scale":1}]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"format":"x"}]}]})",
      R"({"groups":[{"id":"a","formats":["x"],"scales":[{"format":"x","prefix":"","scale":1}]}]})",
  };
  for (const char* text : bad) {
    EXPECT_FALSE(g.LoadJson(text).ok()) << text;
    EXPECT_EQ(g.All().size(), 5u) << text;
  }
}

TEST(MapGroups, NominalScalesForScaleLessSeries) {
  const MapGroups g = MapGroups::Builtin();
  const MapGroup& elev = *g.Find("elevation");
  EXPECT_DOUBLE_EQ(elev.NominalScaleOf("dted-shaded", "DTED0"), 2e6);
  EXPECT_DOUBLE_EQ(elev.NominalScaleOf("dted-shaded", "DTED1"), 500e3);
  EXPECT_DOUBLE_EQ(elev.NominalScaleOf("dted-shaded", "DTED2"), 150e3);
  EXPECT_DOUBLE_EQ(elev.NominalScaleOf("dted-shaded", "DTED3"), 50e3);
  // The elevation-query format draws nothing, so it never joins the ladder.
  EXPECT_EQ(elev.NominalScaleOf("dted", "DTED1"), 0);
  EXPECT_EQ(elev.NominalScaleOf("dted-shaded", "DTED12"), 0);

  // DNC library names carry their type; case is not significant.
  const MapGroup& dnc = *g.Find("dnc");
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "gen17a"), 1e6);
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "COA17C"), 300e3);
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "a1707300"), 50e3);
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "h1707290"), 15e3);
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "WVS012M"), 12e6);
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "wvs040m"), 40e6);
  EXPECT_DOUBLE_EQ(dnc.NominalScaleOf("vpf", "wvs120m"), 120e6);
  EXPECT_EQ(dnc.NominalScaleOf("vpf", "browse"), 0);
  EXPECT_EQ(g.Find("raster")->NominalScaleOf("cadrg", "GNC"), 0);

  // An OSM series is a file stem, so one entry covers the whole format.
  const MapGroup& osm = *g.Find("osm");
  EXPECT_DOUBLE_EQ(osm.NominalScaleOf("osm", "kiawah"), 50e3);
  EXPECT_DOUBLE_EQ(osm.NominalScaleOf("osm", "us-south"), 50e3);
}

TEST(MapGroups, AFormatOnlyScaleMatchesEverySeriesOfIt) {
  MapGroups g;
  ASSERT_TRUE(g.LoadJson(R"({"groups":[{"id":"a","formats":["x","y"],"scales":[
      {"format":"x","series":"big","scale":1000},
      {"format":"x","scale":2000}]}]})")
                  .ok());
  const MapGroup& a = *g.Find("a");
  EXPECT_DOUBLE_EQ(a.NominalScaleOf("x", "big"), 1000);
  EXPECT_DOUBLE_EQ(a.NominalScaleOf("x", "anything"), 2000);
  EXPECT_EQ(a.NominalScaleOf("y", "anything"), 0);
}

TEST(MapGroups, FirstMatchingScaleWins) {
  MapGroups g;
  ASSERT_TRUE(g.LoadJson(R"({"groups":[{"id":"a","formats":["x","y"],"scales":[
      {"series":"hx","scale":1000},
      {"format":"y","prefix":"h","scale":2000},
      {"prefix":"h","scale":3000}]}]})")
                  .ok());
  const MapGroup& a = *g.Find("a");
  EXPECT_DOUBLE_EQ(a.NominalScaleOf("y", "HX"), 1000);
  EXPECT_DOUBLE_EQ(a.NominalScaleOf("y", "hy"), 2000);
  EXPECT_DOUBLE_EQ(a.NominalScaleOf("x", "hy"), 3000);
  EXPECT_EQ(a.NominalScaleOf("x", "z"), 0);
}

}  // namespace
