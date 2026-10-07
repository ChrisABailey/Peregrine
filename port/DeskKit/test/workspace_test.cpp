// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include <gtest/gtest.h>

#include "fv_desk_workspace.h"
#include "scratch.h"

using fv::ProjectionType;
using fv::desk::Workspace;
using fv::desk::test::Scratch;

namespace {

Workspace Sample() {
  Workspace w;
  w.catalog_path = "/data/catalog.sqlite";
  w.map_group = "enc";
  w.product_format = "enc";
  w.product_series_key = "Harbour";
  w.center = fv::GeoPoint{32.612345678901, -79.987654321098};
  w.scale_denom = 21987.123456789;
  w.rotation_deg = 17.25;
  w.projection = ProjectionType::kMercator;
  w.overlays = {{"count", "1"}, {"0.type", "fv.grid"}, {"0.file", ""}, {"0.visible", "true"}};
  w.active_editor = "fv.route";
  return w;
}

TEST(Workspace, JsonRoundTripIsExact) {
  const Workspace w = Sample();
  Workspace back;
  ASSERT_TRUE(back.FromJson(w.ToJson()).ok());
  EXPECT_EQ(back, w);
}

TEST(Workspace, FileRoundTrip) {
  Scratch dir;
  const std::string path = dir.Path("a.fvws");
  ASSERT_TRUE(Sample().Save(path).ok());
  Workspace back;
  ASSERT_TRUE(back.Load(path).ok());
  EXPECT_EQ(back, Sample());
  EXPECT_EQ(back.Load(dir.Path("missing.fvws")).code, fv::kNotFound);
}

TEST(Workspace, EveryProjectionHasAStableKey) {
  size_t n = 0;
  const ProjectionType* all = fv::desk::AllProjectionTypes(&n);
  ASSERT_EQ(n, 5u);
  for (size_t i = 0; i < n; ++i) {
    ProjectionType t;
    ASSERT_TRUE(fv::desk::ParseProjectionKey(fv::desk::ProjectionKey(all[i]), &t));
    EXPECT_EQ(t, all[i]);
  }
  EXPECT_STREQ(fv::desk::ProjectionKey(ProjectionType::kEqualArc), "equal_arc");
  EXPECT_FALSE(fv::desk::ParseProjectionKey("utm", nullptr));
}

TEST(Workspace, ARejectedFileChangesNothing) {
  Workspace w = Sample();
  const char* bad[] = {
      "[]",
      R"({"catalog":"x"})",
      R"({"version":1,"view":{"projection":"utm"}})",
      R"({"version":1,"view":{"scale_denom":0}})",
      R"({"version":1,"view":{"lat":"north"}})",
      R"({"version":1,"overlays":{"count":1}})",
  };
  for (const char* text : bad) {
    EXPECT_FALSE(w.FromJson(text).ok()) << text;
    EXPECT_EQ(w, Sample()) << text;
  }
  EXPECT_EQ(w.FromJson(R"({"version":2})").code, fv::kUnsupported);
}

TEST(Workspace, AbsentFieldsTakeDefaults) {
  Workspace w;
  ASSERT_TRUE(w.FromJson(R"({"version":1})").ok());
  EXPECT_EQ(w, Workspace());
}

}  // namespace
