// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// The auto-detect scan plan and the background catalog build.

#include "fv_desk_catalog_build.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "scratch.h"
#include "stub_formats.h"

using fv::GeoRect;
using fv::desk::BuildProgress;
using fv::desk::CatalogBuild;
using fv::desk::PlanScan;
using fv::desk::ScanStep;
using fv::desk::test::Frame;
using fv::desk::test::Scratch;
using fv::desk::test::StubFrames;

namespace {

namespace fs = std::filesystem;

void Touch(const std::string& path) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path) << "x";
}

class CatalogBuildTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fv::ClearFormatRegistryForTest();
    StubFrames()["cadrg"] = {Frame("a", GeoRect{{30, -82}, {34, -78}}, "GNC", 5e6),
                             Frame("b", GeoRect{{31, -81}, {33, -79}}, "JNC", 2e6)};
    StubFrames()["tiros"] = {};
    ASSERT_TRUE(fv::desk::test::RegisterStubFormat("cadrg").ok());
    ASSERT_TRUE(fv::desk::test::RegisterStubFormat("tiros").ok());
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  std::vector<fv::DataSourceRow> Sources(const std::string& catalog) {
    fv::Catalog cat;
    EXPECT_TRUE(cat.Open(catalog).ok());
    std::vector<fv::DataSourceRow> rows;
    EXPECT_TRUE(cat.DataSources(&rows).ok());
    return rows;
  }
};

TEST_F(CatalogBuildTest, PlanProbesTheRootThenTheConventionalSubdirectory) {
  Scratch dir;
  const std::string root = dir.Path("data");
  fs::create_directories(root + "/rpf");
  const std::vector<ScanStep> steps = PlanScan(root);
  // Only the registered formats, in table order; tiros3/ does not exist.
  ASSERT_EQ(steps.size(), 2u);
  EXPECT_EQ(steps[0].format, "cadrg");
  ASSERT_EQ(steps[0].dirs.size(), 2u);
  EXPECT_EQ(steps[0].dirs[0], root);
  EXPECT_EQ(fs::path(steps[0].dirs[1]).filename(), "rpf");
  EXPECT_EQ(steps[1].format, "tiros");
  ASSERT_EQ(steps[1].dirs.size(), 1u);
  EXPECT_FALSE(steps[0].keep_if_empty);

  EXPECT_TRUE(PlanScan(dir.Path("missing")).empty());
}

TEST_F(CatalogBuildTest, VpfDatabasesAreFoundByTheirHeaderTable) {
  Scratch dir;
  const std::string root = dir.Path("data");
  Touch(root + "/dnc17/a/dht");
  Touch(root + "/dnc17/a/nested/dht");  // inside a database: not searched
  Touch(root + "/wvs/DHT.");
  Touch(root + "/.hidden/db/dht");
  Touch(root + "/d1/d2/d3/d4/d5/dht");  // deeper than four levels
  const std::vector<std::string> dbs = fv::desk::FindVpfDatabases(root);
  ASSERT_EQ(dbs.size(), 2u);
  EXPECT_EQ(fs::path(dbs[0]).filename(), "a");
  EXPECT_EQ(fs::path(dbs[1]).filename(), "wvs");

  // A registered vpf format adds one step per database, after the table.
  ASSERT_TRUE(fv::desk::test::RegisterStubFormat("vpf").ok());
  const std::vector<ScanStep> steps = PlanScan(root);
  ASSERT_EQ(steps.size(), 4u);
  EXPECT_EQ(steps[2].format, "vpf");
  EXPECT_EQ(steps[2].dirs, std::vector<std::string>{dbs[0]});
}

TEST_F(CatalogBuildTest, BuildKeepsWhatYieldsFramesAndDropsTheRest) {
  Scratch dir;
  const std::string catalog = dir.Path("catalog.sqlite");
  const std::string root = dir.Path("data");
  fs::create_directories(root + "/rpf");
  CatalogBuild build(catalog, PlanScan(root));
  build.Wait();

  const BuildProgress p = build.Progress();
  EXPECT_TRUE(p.finished);
  EXPECT_FALSE(p.cancelled);
  EXPECT_EQ(p.done, 2);
  EXPECT_EQ(p.total, 2);
  // The stub ignores its path, so the root wins and rpf/ is never tried.
  ASSERT_EQ(build.Results().size(), 1u);
  EXPECT_EQ(build.Results()[0].format, "cadrg");
  EXPECT_EQ(build.Results()[0].path, root);
  EXPECT_EQ(build.Results()[0].frames, 2);
  EXPECT_TRUE(build.Errors().empty());
  EXPECT_EQ(build.Summary(), "Catalogued 2 frames from 1 source.");

  const auto sources = Sources(catalog);
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources[0].format, "cadrg");
}

TEST_F(CatalogBuildTest, ARescanKeepsAnEmptySource) {
  Scratch dir;
  const std::string catalog = dir.Path("catalog.sqlite");
  ScanStep step;
  step.format = "tiros";
  step.dirs = {dir.Path("tiros")};
  step.keep_if_empty = true;
  CatalogBuild build(catalog, {step});
  build.Wait();
  EXPECT_TRUE(build.Results().empty());
  EXPECT_EQ(build.Summary(), "No map data found.");
  ASSERT_EQ(Sources(catalog).size(), 1u);
}

TEST_F(CatalogBuildTest, AnUnscannableSourceIsReportedAndTheBuildGoesOn) {
  Scratch dir;
  const std::string catalog = dir.Path("catalog.sqlite");
  CatalogBuild build(catalog, {ScanStep{"nosuchformat", {dir.Path("x")}},
                               ScanStep{"cadrg", {dir.Path("y")}}});
  build.Wait();
  ASSERT_EQ(build.Errors().size(), 1u);
  EXPECT_NE(build.Errors()[0].find("nosuchformat"), std::string::npos);
  ASSERT_EQ(build.Results().size(), 1u);
  EXPECT_EQ(build.Summary(), "Catalogued 2 frames from 1 source. 1 source failed.");
}

/// An enumerator that holds its scan open until the test releases it.
struct Gate {
  std::mutex mu;
  std::condition_variable cv;
  bool entered = false;
  bool open = false;
};

Gate& TheGate() {
  static Gate g;
  return g;
}

class GateEnumerator : public fv::IFrameEnumerator {
 public:
  fv::Status Begin(const std::string&) override {
    Gate& g = TheGate();
    std::unique_lock<std::mutex> lock(g.mu);
    g.entered = true;
    g.cv.notify_all();
    g.cv.wait(lock, [&g] { return g.open; });
    return fv::Status::Ok();
  }
  bool Next(fv::FrameInfo*) override { return false; }
};

TEST_F(CatalogBuildTest, CancelStopsBetweenSources) {
  fv::FormatFactories f;
  f.format_key = "gate";
  f.make_enumerator = [] { return std::make_shared<GateEnumerator>(); };
  ASSERT_TRUE(fv::RegisterFormat(f).ok());
  Gate& g = TheGate();
  g.entered = false;
  g.open = false;

  Scratch dir;
  CatalogBuild build(dir.Path("catalog.sqlite"),
                     {ScanStep{"gate", {dir.Path("a")}}, ScanStep{"gate", {dir.Path("b")}},
                      ScanStep{"gate", {dir.Path("c")}}});
  {
    std::unique_lock<std::mutex> lock(g.mu);
    g.cv.wait(lock, [&g] { return g.entered; });
  }
  EXPECT_FALSE(build.Progress().current.empty());
  build.Cancel();
  {
    std::lock_guard<std::mutex> lock(g.mu);
    g.open = true;
  }
  g.cv.notify_all();
  build.Wait();

  const BuildProgress p = build.Progress();
  EXPECT_TRUE(p.finished);
  EXPECT_TRUE(p.cancelled);
  EXPECT_EQ(p.done, 1);
  EXPECT_EQ(build.Summary(), "No map data found. Cancelled after 1 of 3 steps.");
}

}  // namespace

namespace {

TEST_F(CatalogBuildTest, ReplaceExistingRemovesEverySourceFirst) {
  Scratch dir;
  const std::string catalog = dir.Path("catalog.sqlite");
  {
    fv::Catalog cat;
    ASSERT_TRUE(cat.Open(catalog).ok());
    int64_t id = 0;
    int frames = 0;
    ASSERT_TRUE(cat.AddDataSource("../relative/data", "cadrg", 0, &id).ok());
    ASSERT_TRUE(cat.Scan(id, &frames).ok());
  }
  const std::string root = dir.Path("data");
  fs::create_directories(root);
  CatalogBuild build(catalog, PlanScan(root), true);
  build.Wait();
  EXPECT_TRUE(build.Errors().empty());
  const auto sources = Sources(catalog);
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources[0].path, root);
  EXPECT_EQ(sources[0].frames, 2);
}

TEST(ScanRoots, AbsolutePathResolvesAgainstTheWorkingDirectory) {
  const std::string cwd = fs::current_path().string();
  EXPECT_EQ(fv::desk::AbsolutePath("a/../b/"), (fs::path(cwd) / "b").string());
  EXPECT_EQ(fv::desk::AbsolutePath("/x/y/"), "/x/y");
  EXPECT_EQ(fv::desk::AbsolutePath("/"), "/");
}

TEST(ScanRoots, NestedSourcePathsFoldIntoTheirParent) {
  EXPECT_EQ(fv::desk::RootsFromSources({{"/x/y/dted", "dted"},
                                         {"/z/w", "cadrg"},
                                         {"/x/y", "cadrg"},
                                         {"/x/y/vpf/dnc17", "vpf"}}),
            (std::vector<std::string>{"/x/y", "/z/w"}));
  // A format's conventional subdirectory stands for the directory above it.
  EXPECT_EQ(fv::desk::RootsFromSources({{"/d/TestData/dted", "dted-shaded"},
                                         {"/d/TestData/OSM", "osm"},
                                         {"/d/TestData/rpf", "cadrg"}}),
            std::vector<std::string>{"/d/TestData"});
  EXPECT_TRUE(fv::desk::PathWithin("/x/y/z", "/x/y"));
  EXPECT_TRUE(fv::desk::PathWithin("/x/y", "/x/y"));
  EXPECT_FALSE(fv::desk::PathWithin("/x/yz", "/x/y"));
}

}  // namespace
