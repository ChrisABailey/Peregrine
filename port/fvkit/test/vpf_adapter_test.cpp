// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// VPF catalog adapter (phase V2): the fv_vpf reader enumerated into catalog
// coverage rows. Real-data over TestData/vpf/dnc17 (skipped if absent);
// locator round-trip always runs.

#include "fvkit/formats/vpf.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"

namespace fs = std::filesystem;

namespace {

std::string Dnc17Root() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  std::string p = std::string(d) + "/vpf/dnc17";
  return fs::exists(p) ? p : std::string();
}

TEST(VpfLocator, RoundTrip) {
  std::string s = fv::MakeVpfLocator("/data/dnc17", "h1707330", "hjem3000");
  std::string root, lib, tile;
  ASSERT_TRUE(fv::ParseVpfLocator(s, &root, &lib, &tile));
  EXPECT_EQ(root, "/data/dnc17");
  EXPECT_EQ(lib, "h1707330");
  EXPECT_EQ(tile, "hjem3000");
  // untiled: empty tile field still parses
  std::string u = fv::MakeVpfLocator("/data/dnc17", "browse", "");
  ASSERT_TRUE(fv::ParseVpfLocator(u, &root, &lib, &tile));
  EXPECT_EQ(lib, "browse");
  EXPECT_TRUE(tile.empty());
  EXPECT_FALSE(fv::ParseVpfLocator("no-separators", nullptr, nullptr, nullptr));
}

TEST(VpfEnumerate, HarborTilesAndBounds) {
  std::string root = Dnc17Root();
  if (root.empty()) GTEST_SKIP();

  fv::VpfFrameEnumerator e;
  ASSERT_TRUE(e.Begin(root).ok());
  std::vector<fv::FrameInfo> frames;
  fv::FrameInfo f;
  while (e.Next(&f)) frames.push_back(f);

  // 11 libraries; the 5 harbor libs are 10 tiles each -> plenty of rows
  ASSERT_GT(frames.size(), 40u);
  std::set<std::string> libs;
  for (const auto& fr : frames) {
    libs.insert(fr.series_key);
    EXPECT_LT(fr.bounds.ll.lat, fr.bounds.ur.lat) << fr.path;
    EXPECT_LT(fr.bounds.ll.lon, fr.bounds.ur.lon) << fr.path;
    std::string rt, lib, tile;
    EXPECT_TRUE(fv::ParseVpfLocator(fr.path, &rt, &lib, &tile)) << fr.path;
    EXPECT_EQ(lib, fr.series_key);
  }
  EXPECT_GE(libs.size(), 5u);
  EXPECT_EQ(libs.count("h1707330"), 1u);

  // the pinned first harbor tile (hjem3000: 41.0-41.25 N, 70.5-70.25 W)
  bool found = false;
  for (const auto& fr : frames) {
    std::string rt, lib, tile;
    fv::ParseVpfLocator(fr.path, &rt, &lib, &tile);
    if (lib == "h1707330" && tile == "hjem3000") {
      found = true;
      EXPECT_DOUBLE_EQ(fr.bounds.ll.lat, 41.00);
      EXPECT_DOUBLE_EQ(fr.bounds.ll.lon, -70.50);
      EXPECT_DOUBLE_EQ(fr.bounds.ur.lat, 41.25);
    }
  }
  EXPECT_TRUE(found) << "harbor tile hjem3000 not enumerated";

  // deterministic
  fv::VpfFrameEnumerator e2;
  ASSERT_TRUE(e2.Begin(root).ok());
  for (const auto& expected : frames) {
    ASSERT_TRUE(e2.Next(&f));
    EXPECT_EQ(f.path, expected.path);
  }
}

TEST(VpfEnumerate, RejectsNonVpfDir) {
  fv::VpfFrameEnumerator e;
  EXPECT_EQ(e.Begin("/nonexistent/vpf").code, fv::kNotFound);
  std::string td = getenv("FVW_TESTDATA_DIR") ? getenv("FVW_TESTDATA_DIR") : "";
  if (!td.empty() && fs::exists(td + "/geotiff"))
    EXPECT_EQ(e.Begin(td + "/geotiff").code, fv::kUnsupported);  // no dht/lat
}

TEST(VpfCatalog, ScanAndSelectNantucket) {
  std::string root = Dnc17Root();
  if (root.empty()) GTEST_SKIP();

  fv::ClearFormatRegistryForTest();
  fv::RegisterBuiltinFormats();
  fv::Catalog cat;
  ASSERT_TRUE(cat.Open(":memory:").ok());
  int64_t id;
  int n = 0;
  ASSERT_TRUE(cat.AddDataSource(root, "vpf", 0, &id).ok());
  ASSERT_TRUE(cat.Scan(id, &n).ok());
  EXPECT_GT(n, 40);

  // a point in Nantucket Sound (~41.35 N, 70.1 W) -> harbor coverage
  fv::GeoRect around{{41.3, -70.2}, {41.4, -70.0}};
  std::vector<fv::CoverageRow> rows;
  ASSERT_TRUE(cat.SelectByGeoRect(around, &rows).ok());
  ASSERT_FALSE(rows.empty());
  bool harbor = false;
  for (const auto& r : rows) {
    EXPECT_TRUE(r.bounds.Intersects(around)) << r.path;
    EXPECT_EQ(r.format, "vpf");
    if (r.series_key[0] == 'h') harbor = true;
  }
  EXPECT_TRUE(harbor) << "expected a harbor (h*) library over Nantucket Sound";

  // land far away -> no DNC 17 coverage (this library is Cape Cod only)
  ASSERT_TRUE(cat.SelectByGeoRect({{33.6, -84.5}, {33.9, -84.2}}, &rows).ok());
  EXPECT_TRUE(rows.empty());
  fv::ClearFormatRegistryForTest();
}

}  // namespace
