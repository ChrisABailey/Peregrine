// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// pippin::ChartStore: import a chart zip, swap sets, and open the result.

#include "PPChartStore.h"

#include <gtest/gtest.h>

#include <algorithm>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

#include "PPEncChart.h"
#include "fv_enc_index.h"
#include "fv_enc_names.h"
#include "zip_test_writer.h"

namespace fs = std::filesystem;

namespace {

std::string TestData(const std::string& sub) {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/" + sub;
  return fs::exists(p) ? p : std::string();
}

std::string ReadAll(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  std::stringstream ss;
  ss << is.rdbuf();
  return ss.str();
}

class ChartStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    enc_ = TestData("enc");
    s52_ = enc_;  // the fixture carries the S-52 library beside its cells
    if (enc_.empty()) GTEST_SKIP() << "testdata/enc not present";
    dir_ = fs::temp_directory_path() /
           (std::string("pippin_chart_store_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override {
    if (!dir_.empty()) fs::remove_all(dir_);
  }

  /// A NOAA-shaped zip ("ENC_ROOT/<cell>/<cell>.000" plus updates) holding
  /// the named Charleston cells.
  std::string MakeZip(const std::string& name, const std::vector<std::string>& cells) {
    fv_test::ZipTestWriter w;
    w.AddDirectory("ENC_ROOT");
    for (const std::string& c : cells) {
      for (const auto& f : fs::directory_iterator(enc_ + "/" + c))
        w.Add("ENC_ROOT/" + c + "/" + f.path().filename().string(),
              ReadAll(f.path().string()));
    }
    const std::string path = (dir_ / name).string();
    EXPECT_TRUE(w.Write(path));
    return path;
  }

  std::string enc_, s52_;
  fs::path dir_;
};

TEST_F(ChartStoreTest, EmptyStoreHasNoSet) {
  pippin::ChartStore store((dir_ / "Charts").string());
  EXPECT_TRUE(store.CurrentSet().empty());
}

TEST_F(ChartStoreTest, ImportInstallsAnIndexedSet) {
  const std::string zip = MakeZip("chs.zip", {"US5CHSDC", "US4SC1BO"});
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  double last = -1.0;
  bool monotonic = true;
  ASSERT_TRUE(store.Import(zip, &r, [&](double f) {
                     monotonic = monotonic && f >= last;
                     last = f;
                     return true;
                   }).ok());
  EXPECT_TRUE(monotonic);
  EXPECT_DOUBLE_EQ(last, 1.0);
  EXPECT_EQ(store.CurrentSet(), r.set_dir);
  EXPECT_EQ(r.cell_count, 2u);
  EXPECT_EQ(r.bands, (std::vector<int>{4, 5}));
  EXPECT_TRUE(r.unreadable.empty());
  EXPECT_LT(r.bounds.ll.lat, r.bounds.ur.lat);

  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::ReadEncIndex(r.set_dir + "/" + fv::kEncIndexFileName, &idx).ok());
  ASSERT_EQ(idx.size(), 2u);
  EXPECT_EQ(idx[1].path, "ENC_ROOT/US5CHSDC/US5CHSDC.000");
  EXPECT_EQ(idx[1].update_count, 3);
  EXPECT_EQ(idx[1].applied_updates, 3);
  EXPECT_EQ(r.updates_applied, 3u);
  EXPECT_EQ(r.updates_failed, 0u);
  EXPECT_TRUE(r.outdated.empty());
  EXPECT_TRUE(r.cancelled.empty());
}

/// A product catalog in NOAA's layout listing `cells` as {name, status, edtn, updn}, all in SC.
std::string WriteCatalog(const fs::path& path,
                         const std::vector<std::tuple<std::string, std::string, int, int>>& cells) {
  std::ofstream os(path);
  os << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<EncProductCatalog>\n"
        "<Header><dt_valid>2026-10-26T04:55:10Z</dt_valid></Header>\n";
  for (const auto& [name, status, edtn, updn] : cells)
    os << "<cell><name>" << name << "</name><status>" << status << "</status>"
       << "<coast_guard_districts><coast_guard_district>7</coast_guard_district>"
          "</coast_guard_districts><states><state>SC</state></states>"
       << "<edtn>" << edtn << "</edtn><updn>" << updn << "</updn></cell>\n";
  os << "</EncProductCatalog>\n";
  return path.string();
}

TEST_F(ChartStoreTest, RegionComesFromTheZipName) {
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(MakeZip("SC_ENCs.zip", {"US4SC1BO"}), &r).ok());
  EXPECT_EQ(store.CurrentRegion(), "SC");
  ASSERT_TRUE(store.Import(MakeZip("charleston.zip", {"US4SC1BO"}), &r).ok());
  EXPECT_EQ(store.CurrentRegion(), "");
}

TEST_F(ChartStoreTest, CheckForUpdatesAgainstTheCatalog) {
  pippin::ChartStore store((dir_ / "Charts").string());
  const std::string catalog = WriteCatalog(dir_ / "ENCProdCat.xml",
                                           {{"US5CHSDC", "Active", 2, 3},
                                            {"US4SC1BO", "Active", 1, 1},
                                            {"US5CHSDD", "Active", 2, 2}});
  fv::EncCatalogComparison c;
  EXPECT_EQ(store.CheckForUpdates(catalog, &c).code, fv::kNotFound);

  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(MakeZip("SC_ENCs.zip", {"US5CHSDC", "US4SC1BO"}), &r).ok());
  ASSERT_TRUE(store.CheckForUpdates(catalog, &c).ok());
  EXPECT_EQ(c.region, "SC");
  EXPECT_FALSE(c.up_to_date());
  EXPECT_TRUE(c.new_editions.empty());
  EXPECT_EQ(c.new_updates, (std::vector<std::string>{"US4SC1BO"}));
  EXPECT_EQ(c.added, (std::vector<std::string>{"US5CHSDD"}));
  EXPECT_FALSE(store.CheckForUpdates((dir_ / "missing.xml").string(), &c).ok());
}

TEST_F(ChartStoreTest, UpdateThatDoesNotApplyIsCounted) {
  // US5CHSDC.003 replaced by a second copy of .002: its UPDN is out of sequence.
  fv_test::ZipTestWriter w;
  const std::string cell = enc_ + "/US5CHSDC/US5CHSDC";
  w.Add("ENC_ROOT/US5CHSDC/US5CHSDC.000", ReadAll(cell + ".000"));
  w.Add("ENC_ROOT/US5CHSDC/US5CHSDC.001", ReadAll(cell + ".001"));
  w.Add("ENC_ROOT/US5CHSDC/US5CHSDC.002", ReadAll(cell + ".002"));
  w.Add("ENC_ROOT/US5CHSDC/US5CHSDC.003", ReadAll(cell + ".002"));
  const std::string zip = (dir_ / "stale.zip").string();
  ASSERT_TRUE(w.Write(zip));

  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(zip, &r).ok());
  EXPECT_EQ(r.cell_count, 1u);
  EXPECT_EQ(r.updates_applied, 2u);
  EXPECT_EQ(r.updates_failed, 1u);
  EXPECT_EQ(r.outdated, (std::vector<std::string>{"US5CHSDC"}));

  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(r.set_dir, s52_).ok());
  EXPECT_EQ(chart.outdated_cell_count(), 1u);
}

TEST_F(ChartStoreTest, CancelledCellsAreNotDrawnOrCovered) {
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(MakeZip("chs.zip", {"US5CHSDC", "US5CHSEC"}), &r).ok());
  const std::string index = r.set_dir + "/" + fv::kEncIndexFileName;
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::ReadEncIndex(index, &idx).ok());
  ASSERT_EQ(idx.size(), 2u);
  idx[0].cancelled = true;
  ASSERT_TRUE(fv::WriteEncIndex(index, idx).ok());

  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(r.set_dir, s52_).ok());
  EXPECT_EQ(chart.cell_count(), 1u);
  const fv::GeoRect c = chart.coverage();
  EXPECT_DOUBLE_EQ(c.ll.lat, idx[1].bounds.ll.lat);
  EXPECT_DOUBLE_EQ(c.ur.lat, idx[1].bounds.ur.lat);
}

TEST_F(ChartStoreTest, OlderIndexIsRebuiltOnOpen) {
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(MakeZip("chs.zip", {"US5CHSDC"}), &r).ok());
  const std::string index = r.set_dir + "/" + fv::kEncIndexFileName;
  {
    std::ofstream os(index, std::ios::trunc);
    os << "# fv-enc-index 1\n"
       << "ENC_ROOT/US5CHSDC/US5CHSDC.000\tUS5CHSDC\t5\t2\t3\t12000\t32.7\t-80\t32.8\t-79.9\n";
  }
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(r.set_dir, s52_).ok());
  EXPECT_EQ(chart.cell_count(), 1u);
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::ReadEncIndex(index, &idx).ok()) << "rewritten in the current format";
  ASSERT_EQ(idx.size(), 1u);
  EXPECT_EQ(idx[0].applied_updates, 3);
}

TEST_F(ChartStoreTest, ImportWritesTheNameTableWhenGivenACatalogue) {
  const std::string zip = MakeZip("chs.zip", {"US5CHSDC", "US5CHSEC"});
  pippin::ChartStore store((dir_ / "Charts").string());
  store.SetCatalogDir(s52_);
  pippin::ChartImportResult r;
  double last = -1.0;
  ASSERT_TRUE(store.Import(zip, &r, [&](double f) {
                     EXPECT_GE(f, last);
                     last = f;
                     return true;
                   }).ok());
  EXPECT_DOUBLE_EQ(last, 1.0);
  std::vector<fv::EncNameEntry> names;
  ASSERT_TRUE(fv::ReadEncNames(r.set_dir + "/" + fv::kEncNamesFileName, &names).ok());
  EXPECT_TRUE(std::any_of(names.begin(), names.end(), [](const fv::EncNameEntry& e) {
    return e.name == "Municipal Marina";
  }));
}

TEST_F(ChartStoreTest, ChartOpensFromTheIndex) {
  const std::string zip = MakeZip("chs.zip", {"US5CHSDC", "US5CHSDD", "US4SC1BO"});
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(zip, &r).ok());
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(store.CurrentSet(), s52_).ok());
  EXPECT_EQ(chart.cell_count(), 3u);
  EXPECT_EQ(chart.bands(), (std::vector<int>{4, 5}));
  // With a cell folder gone, the count still comes from the index.
  fs::remove_all(fs::path(r.set_dir) / "ENC_ROOT" / "US4SC1BO");
  pippin::EncChart again;
  ASSERT_TRUE(again.Open(store.CurrentSet(), s52_).ok());
  EXPECT_EQ(again.cell_count(), 3u);
}

TEST_F(ChartStoreTest, CoverageLeavesOutOverviewCells) {
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult r;
  ASSERT_TRUE(store.Import(MakeZip("mixed.zip", {"US2EC02M", "US5CHSDC"}), &r).ok());
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::ReadEncIndex(r.set_dir + "/" + fv::kEncIndexFileName, &idx).ok());
  ASSERT_EQ(idx.size(), 2u);
  const fv::EncIndexEntry& harbour = idx[0].usage_band == 5 ? idx[0] : idx[1];
  const fv::EncIndexEntry& general = idx[0].usage_band == 5 ? idx[1] : idx[0];

  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(r.set_dir, s52_).ok());
  const fv::GeoRect c = chart.coverage();
  EXPECT_DOUBLE_EQ(c.ll.lat, harbour.bounds.ll.lat);
  EXPECT_DOUBLE_EQ(c.ur.lon, harbour.bounds.ur.lon);
  EXPECT_GT(general.bounds.ur.lat - general.bounds.ll.lat, c.ur.lat - c.ll.lat);

  // With only general cells, they are the coverage.
  pippin::ChartImportResult g;
  ASSERT_TRUE(store.Import(MakeZip("general.zip", {"US2EC02M"}), &g).ok());
  pippin::EncChart only_general;
  ASSERT_TRUE(only_general.Open(g.set_dir, s52_).ok());
  EXPECT_DOUBLE_EQ(only_general.coverage().ll.lat, general.bounds.ll.lat);
  EXPECT_DOUBLE_EQ(only_general.coverage().ur.lat, general.bounds.ur.lat);
}

TEST_F(ChartStoreTest, SecondImportReplacesTheFirst) {
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult a, b;
  ASSERT_TRUE(store.Import(MakeZip("a.zip", {"US4SC1BO"}), &a).ok());
  ASSERT_TRUE(store.Import(MakeZip("b.zip", {"US5CHSDC"}), &b).ok());
  EXPECT_NE(a.set_dir, b.set_dir);
  EXPECT_EQ(store.CurrentSet(), b.set_dir);
  // The old set stays until its reader has let go.
  EXPECT_TRUE(fs::is_directory(a.set_dir));
  store.RemoveStaleSets();
  EXPECT_FALSE(fs::exists(a.set_dir));
  EXPECT_TRUE(fs::is_directory(b.set_dir));
}

TEST_F(ChartStoreTest, FailedImportKeepsTheCurrentSet) {
  pippin::ChartStore store((dir_ / "Charts").string());
  pippin::ChartImportResult a, bad;
  ASSERT_TRUE(store.Import(MakeZip("a.zip", {"US4SC1BO"}), &a).ok());

  fv_test::ZipTestWriter w;
  w.Add("README.TXT", "no cells here");
  const std::string empty_zip = (dir_ / "empty.zip").string();
  ASSERT_TRUE(w.Write(empty_zip));
  EXPECT_EQ(store.Import(empty_zip, &bad).code, fv::kNotFound);

  EXPECT_EQ(store.Import(MakeZip("c.zip", {"US5CHSDC"}), &bad,
                         [](double f) { return f < 0.5; })
                .code,
            fv::kInterrupted);
  EXPECT_EQ(store.Import((dir_ / "missing.zip").string(), &bad).code, fv::kNotFound);

  EXPECT_EQ(store.CurrentSet(), a.set_dir);
  size_t sets = 0;
  for (const auto& e : fs::directory_iterator(dir_ / "Charts"))
    if (e.is_directory()) ++sets;
  EXPECT_EQ(sets, 1u) << "a failed import leaves no directory behind";
}

}  // namespace

/** Import time for a real NOAA zip named by FVW_CHART_ZIP, then the open
 *  time from its index. Run with --gtest_also_run_disabled_tests. */
TEST(ChartStoreBench, DISABLED_ImportRealZip) {
  const char* zip = std::getenv("FVW_CHART_ZIP");
  const std::string s52 = TestData("enc");
  if (zip == nullptr || s52.empty()) GTEST_SKIP() << "set FVW_CHART_ZIP";
  const fs::path dir = fs::temp_directory_path() / "pippin_chart_store_bench";
  fs::remove_all(dir);
  pippin::ChartStore store(dir.string());
  pippin::ChartImportResult r;
  const auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(store.Import(zip, &r).ok());
  const auto t1 = std::chrono::steady_clock::now();
  pippin::EncChart chart;
  ASSERT_TRUE(chart.Open(store.CurrentSet(), s52).ok());
  const auto t2 = std::chrono::steady_clock::now();
  auto ms = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  std::printf("import: %zu cells, %zu unreadable, %zu cancelled, %zu updates applied, "
              "%zu not; %.0f ms; open from index %.1f ms\n",
              r.cell_count, r.unreadable.size(), r.cancelled.size(), r.updates_applied,
              r.updates_failed, ms(t0, t1), ms(t1, t2));
  fs::remove_all(dir);
}
