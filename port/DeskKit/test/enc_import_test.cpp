// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the ENC importer behind Map ▸ Options ▸ ENC, over the
/// Charleston cells in testdata/enc and the real ENC enumerator.

#include "fv_desk_enc_import.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "fv_desk_fake.h"
#include "fv_desk_map_options.h"
#include "fv_enc_format.h"
#include "fv_enc_index.h"
#include "fvkit/catalog/catalog.h"
#include "fvkit/formats/registry.h"
#include "scratch.h"
#include "zip_test_writer.h"

namespace fs = std::filesystem;
using fv::desk::FakeDesk;
using fv::desk::test::Scratch;

namespace {

std::string ReadAll(const fs::path& path) {
  std::ifstream is(path, std::ios::binary);
  std::stringstream ss;
  ss << is.rdbuf();
  return ss.str();
}

class EncImportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* d = std::getenv("FVW_TESTDATA_DIR");
    if (d == nullptr || !fs::exists(fs::path(d) / "enc" / "US5CHSDC"))
      GTEST_SKIP() << "testdata/enc not present";
    enc_ = fs::path(d) / "enc";
    fv::ClearFormatRegistryForTest();
    fv::RegisterEncFormat();
  }
  void TearDown() override { fv::ClearFormatRegistryForTest(); }

  /// A NOAA-shaped zip of the named cells, at `path`.
  std::string MakeZip(const std::string& path, const std::vector<std::string>& cells) {
    fv_test::ZipTestWriter w;
    w.AddDirectory("ENC_ROOT");
    for (const std::string& c : cells)
      for (const auto& f : fs::directory_iterator(enc_ / c))
        w.Add("ENC_ROOT/" + c + "/" + f.path().filename().string(), ReadAll(f.path()));
    EXPECT_TRUE(w.Write(path));
    return path;
  }

  /// A Desk over a new catalog file in `dir`, importing into `<dir>/charts`.
  static void Prepare(FakeDesk& fd, const Scratch& dir) {
    fd.settings().Set(fv::desk::kEncChartsDirKey, dir.Path("charts"));
    fd.shell().save_spec = dir.Path("catalog.sqlite");
    ASSERT_TRUE(fd.Run({"map.sources"}).ok());
  }

  /// Runs the import and the rescan it starts to the end.
  static void Finish(fv::desk::Desk& desk) {
    for (int i = 0; desk.Building() && i < 6000; ++i)
      if (!desk.PollCatalogBuild()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(desk.Building());
  }

  /// Frames the catalog lists under the enc source at `dir`.
  static int EncFrames(fv::desk::Desk& desk, const std::string& dir) {
    std::vector<fv::DataSourceRow> sources;
    EXPECT_TRUE(desk.catalog()->DataSources(&sources).ok());
    int found = -1;
    for (const fv::DataSourceRow& s : sources)
      if (s.format == "enc" && s.path == dir) found = static_cast<int>(s.frames);
    return found;
  }

  fs::path enc_;
};

TEST_F(EncImportTest, SetNamesComeFromTheZipOrTheFolder) {
  Scratch dir;
  fs::create_directories(dir.Path("NY_ENCs/ENC_ROOT"));
  fs::create_directories(dir.Path("my charts"));
  EXPECT_EQ(fv::desk::EncSetName("/x/SC_ENCs.zip"), "SC_ENCs");
  EXPECT_EQ(fv::desk::EncSetName(dir.Path("NY_ENCs/ENC_ROOT")), "NY_ENCs");
  EXPECT_EQ(fv::desk::EncSetName(dir.Path("my charts") + "/"), "my_charts");
  EXPECT_EQ(fv::desk::EncSetName("/x/.hidden.zip"), "hidden");
}

TEST_F(EncImportTest, ZipImportAddsTheSetAndRescansOnlyEnc) {
  Scratch dir;
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  Prepare(fd, dir);
  fd.shell().files_to_open = {MakeZip(dir.Path("SC_ENCs.zip"), {"US5CHSDC", "US4SC1BO"})};
  ASSERT_TRUE(fd.Run({"enc.import_zip"}).ok());
  EXPECT_TRUE(desk.Building());
  EXPECT_FALSE(fd.desk().commands().IsEnabled("enc.import_zip"));
  Finish(desk);

  const std::string charts = fs::path(dir.Path("charts")).lexically_normal().string();
  EXPECT_EQ(desk.ScanRoots(), std::vector<std::string>{charts});
  EXPECT_EQ(EncFrames(desk, charts + "/enc"), 2);
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::ReadEncIndex(charts + "/enc/SC_ENCs/" + fv::kEncIndexFileName, &idx).ok());
  EXPECT_EQ(idx.size(), 2u);
  EXPECT_FALSE(fs::exists(charts + "/.staging"));
  ASSERT_FALSE(fd.shell().notices.empty());
  EXPECT_EQ(fd.shell().notices.back().rfind("SC_ENCs: 2 cells, 3 updates applied\n", 0), 0u)
      << fd.shell().notices.back();
  bool enc_available = false;
  for (const fv::desk::MapGroup* g : desk.AvailableGroups()) enc_available |= g->id == "enc";
  EXPECT_TRUE(enc_available);
}

TEST_F(EncImportTest, ReimportReplacesTheSetAndKeepsOtherSets) {
  Scratch dir;
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  Prepare(fd, dir);
  ASSERT_TRUE(desk.ImportEncCharts({MakeZip(dir.Path("SC_ENCs.zip"), {"US5CHSDC", "US4SC1BO"}),
                                    MakeZip(dir.Path("GA_ENCs.zip"), {"US4SC1CO"})})
                  .ok());
  Finish(desk);
  const std::string enc = fs::path(dir.Path("charts/enc")).lexically_normal().string();
  EXPECT_EQ(EncFrames(desk, enc), 3);

  ASSERT_TRUE(desk.ImportEncCharts({MakeZip(dir.Path("SC_ENCs.zip"), {"US4SC1BO"})}).ok());
  Finish(desk);
  EXPECT_EQ(EncFrames(desk, enc), 2);
  EXPECT_FALSE(fs::exists(enc + "/SC_ENCs/ENC_ROOT/US5CHSDC"));
  EXPECT_TRUE(fs::exists(enc + "/GA_ENCs"));
  EXPECT_NE(fd.shell().notices.back().find("(replaced the earlier set)"), std::string::npos);
}

TEST_F(EncImportTest, AFailedSourceIsReportedAndStartsNoRescan) {
  Scratch dir;
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  Prepare(fd, dir);
  fs::create_directories(dir.Path("empty"));
  ASSERT_TRUE(desk.ImportEncCharts({dir.Path("empty")}).ok());
  Finish(desk);
  EXPECT_EQ(fd.shell().notices.back(), "empty: the folder holds no ENC cells");
  EXPECT_FALSE(fs::exists(dir.Path("charts/enc/empty")));
}

TEST_F(EncImportTest, ChartsDirInsideAScanRootIsRefused) {
  Scratch dir;
  FakeDesk fd;
  fv::desk::Desk& desk = fd.desk();
  Prepare(fd, dir);
  fs::create_directories(dir.Path("data"));
  ASSERT_TRUE(desk.AddScanRoot(dir.Path("data")).ok());
  // Inside data/enc, which data's scan reads: the cells would be catalogued twice.
  fd.settings().Set(fv::desk::kEncChartsDirKey, dir.Path("data/enc/charts"));
  const fv::Status s = desk.ImportEncCharts({MakeZip(dir.Path("SC_ENCs.zip"), {"US4SC1BO"})});
  EXPECT_EQ(s.code, fv::kInvalidArg);
  EXPECT_FALSE(desk.Building());

  // Beside the format folders is fine: it becomes a root of its own.
  fd.settings().Set(fv::desk::kEncChartsDirKey, dir.Path("data/enc-regions"));
  ASSERT_TRUE(desk.ImportEncCharts({dir.Path("SC_ENCs.zip")}).ok());
  Finish(desk);
  EXPECT_EQ(desk.ScanRoots(),
            (std::vector<std::string>{dir.Path("data"), dir.Path("data/enc-regions")}));
  ASSERT_TRUE(desk.RemoveScanRoot(dir.Path("data/enc-regions")).ok());

  // The scan root itself is fine and is not listed twice.
  fd.settings().Set(fv::desk::kEncChartsDirKey, dir.Path("data"));
  ASSERT_TRUE(desk.ImportEncCharts({dir.Path("SC_ENCs.zip")}).ok());
  Finish(desk);
  EXPECT_EQ(desk.ScanRoots().size(), 1u);
}

TEST_F(EncImportTest, NeedsACatalogFile) {
  FakeDesk fd;
  EXPECT_FALSE(fd.desk().commands().IsEnabled("enc.import_folder"));
  EXPECT_EQ(fd.desk().ImportEncCharts({"/x/SC_ENCs.zip"}).code, fv::kInvalidArg);
}

TEST_F(EncImportTest, TheEncPageCarriesTheImportButtons) {
  FakeDesk fd;
  const fv::desk::MapOptionsSource src = fv::desk::EncMapOptions();
  auto page = fv::desk::OptionsPage::ForMapGroup("ENC", src, fd.settings());
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(page->actions().size(), 2u);
  EXPECT_EQ(page->actions()[0].command, "enc.import_zip");
  EXPECT_EQ(page->actions()[1].command, "enc.import_folder");
  EXPECT_NE(page->Field(fv::desk::kEncChartsDirKey), nullptr);
  for (const fv::desk::OptionsAction& a : page->actions())
    EXPECT_NE(fd.desk().commands().Find(a.command), nullptr) << a.command;
}

}  // namespace
