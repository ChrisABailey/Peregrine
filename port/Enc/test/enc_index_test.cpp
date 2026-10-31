// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ENC cell index and header-only dataset read, against the Charleston fixture.

#include "fv_enc_index.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fv_s57.h"

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

#define REQUIRE_ENC(root)                                     \
  const std::string root = EncRoot();                         \
  if (root.empty()) GTEST_SKIP() << "testdata/enc not present"

const fv::EncIndexEntry* ByName(const std::vector<fv::EncIndexEntry>& v,
                                const std::string& name) {
  for (const fv::EncIndexEntry& e : v)
    if (e.name == name) return &e;
  return nullptr;
}

TEST(ReadS57DatasetInfo, MatchesFullOpen) {
  REQUIRE_ENC(root);
  const std::string cell = root + "/US5CHSDC/US5CHSDC.000";
  fv::S57DatasetInfo head;
  ASSERT_TRUE(fv::ReadS57DatasetInfo(cell, &head).ok());
  fv::S57Cell full;
  ASSERT_TRUE(full.Open(cell).ok());
  EXPECT_EQ(head.name, full.info().name);
  EXPECT_EQ(head.edition, "2");
  EXPECT_EQ(head.compilation_scale, 12000);
  EXPECT_EQ(head.intended_usage, full.info().intended_usage);
  EXPECT_DOUBLE_EQ(head.coordinate_multiplier, full.info().coordinate_multiplier);
}

TEST(FindS57UpdateFiles, StopsAtFirstGap) {
  REQUIRE_ENC(root);
  EXPECT_EQ(fv::FindS57UpdateFiles(root + "/US5CHSDC/US5CHSDC.000").size(), 3u);
  EXPECT_EQ(fv::FindS57UpdateFiles(root + "/US4SC1BO/US4SC1BO.000").size(), 0u);
  EXPECT_TRUE(fv::FindS57UpdateFiles(root + "/US5CHSDC/US5CHSDC.001").empty());
}

TEST(BuildEncIndex, CoversEveryBaseCell) {
  REQUIRE_ENC(root);
  std::vector<std::string> cells;
  ASSERT_TRUE(fv::EnumerateEncCells(root, &cells).ok());
  std::vector<fv::EncIndexEntry> idx;
  std::vector<std::string> unreadable;
  size_t calls = 0;
  ASSERT_TRUE(fv::BuildEncIndex(root, &idx, &unreadable, [&](size_t, size_t) {
                ++calls;
                return true;
              }).ok());
  EXPECT_EQ(idx.size() + unreadable.size(), cells.size());
  EXPECT_EQ(calls, cells.size());

  const fv::EncIndexEntry* dc = ByName(idx, "US5CHSDC");
  ASSERT_NE(dc, nullptr);
  EXPECT_EQ(dc->path, "US5CHSDC/US5CHSDC.000");
  EXPECT_EQ(dc->usage_band, 5);
  EXPECT_EQ(dc->edition, "2");
  EXPECT_EQ(dc->update_count, 3);
  EXPECT_EQ(dc->applied_updates, 3);
  EXPECT_FALSE(dc->cancelled);
  EXPECT_EQ(dc->compilation_scale, 12000);
  // Charleston harbour, from CATALOG.031.
  EXPECT_GT(dc->bounds.ll.lat, 32.0);
  EXPECT_LT(dc->bounds.ur.lat, 33.5);
  EXPECT_LT(dc->bounds.ll.lon, -79.0);
  EXPECT_LT(dc->bounds.ll.lon, dc->bounds.ur.lon);
}

TEST(EncIndexFile, RoundTrips) {
  REQUIRE_ENC(root);
  std::vector<fv::EncIndexEntry> idx;
  ASSERT_TRUE(fv::BuildEncIndex(root, &idx).ok());
  const std::string path =
      (fs::temp_directory_path() / "fv_enc_index_roundtrip.tsv").string();
  ASSERT_TRUE(fv::WriteEncIndex(path, idx).ok());
  std::vector<fv::EncIndexEntry> back;
  ASSERT_TRUE(fv::ReadEncIndex(path, &back).ok());
  fs::remove(path);
  ASSERT_EQ(back.size(), idx.size());
  for (size_t i = 0; i < idx.size(); ++i) {
    EXPECT_EQ(back[i].path, idx[i].path);
    EXPECT_EQ(back[i].name, idx[i].name);
    EXPECT_EQ(back[i].usage_band, idx[i].usage_band);
    EXPECT_EQ(back[i].edition, idx[i].edition);
    EXPECT_EQ(back[i].update_count, idx[i].update_count);
    EXPECT_EQ(back[i].applied_updates, idx[i].applied_updates);
    EXPECT_EQ(back[i].cancelled, idx[i].cancelled);
    EXPECT_EQ(back[i].compilation_scale, idx[i].compilation_scale);
    EXPECT_NEAR(back[i].bounds.ll.lat, idx[i].bounds.ll.lat, 1e-9);
    EXPECT_NEAR(back[i].bounds.ur.lon, idx[i].bounds.ur.lon, 1e-9);
  }
}

TEST(EncIndexFile, RejectsOtherFormats) {
  const std::string path = (fs::temp_directory_path() / "fv_enc_index_bad.tsv").string();
  {
    std::ofstream os(path);
    os << "# something else\n";
  }
  std::vector<fv::EncIndexEntry> out;
  EXPECT_EQ(fv::ReadEncIndex(path, &out).code, fv::kUnsupported);
  {
    std::ofstream os(path);  // written before updates were applied
    os << "# fv-enc-index 1\n";
  }
  EXPECT_EQ(fv::ReadEncIndex(path, &out).code, fv::kUnsupported);
  {
    std::ofstream os(path);
    os << "# fv-enc-index 2\nonly\tthree\tcolumns\n";
  }
  EXPECT_EQ(fv::ReadEncIndex(path, &out).code, fv::kIoError);
  fs::remove(path);
}

}  // namespace
