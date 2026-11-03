// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for ImportEncSet over the Charleston cells in testdata/enc.

#include "fv_enc_import.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "fv_enc_index.h"
#include "zip_test_writer.h"

namespace fs = std::filesystem;

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
    dir_ = fs::temp_directory_path() /
           (std::string("fv_enc_import_") +
            ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override {
    if (!dir_.empty()) fs::remove_all(dir_);
  }

  /// A NOAA-shaped zip holding the named cells and their updates.
  std::string MakeZip(const std::vector<std::string>& cells) {
    fv_test::ZipTestWriter w;
    w.AddDirectory("ENC_ROOT");
    for (const std::string& c : cells)
      for (const auto& f : fs::directory_iterator(enc_ / c))
        w.Add("ENC_ROOT/" + c + "/" + f.path().filename().string(), ReadAll(f.path()));
    const std::string path = (dir_ / "SC_ENCs.zip").string();
    EXPECT_TRUE(w.Write(path));
    return path;
  }

  /// The same cells as a directory tree.
  std::string MakeFolder(const std::vector<std::string>& cells) {
    const fs::path root = dir_ / "src" / "ENC_ROOT";
    for (const std::string& c : cells) {
      fs::create_directories(root / c);
      for (const auto& f : fs::directory_iterator(enc_ / c))
        fs::copy_file(f.path(), root / c / f.path().filename());
    }
    return root.string();
  }

  fs::path enc_, dir_;
};

TEST_F(EncImportTest, ZipAndFolderGiveTheSameIndex) {
  fv::EncImportResult from_zip, from_dir;
  const std::string a = (dir_ / "out" / "a").string();
  const std::string b = (dir_ / "out" / "b").string();
  ASSERT_TRUE(fv::ImportEncSet(MakeZip({"US5CHSDC", "US4SC1BO"}), a, "", &from_zip).ok());
  ASSERT_TRUE(fv::ImportEncSet(MakeFolder({"US5CHSDC", "US4SC1BO"}), b, "", &from_dir).ok());
  EXPECT_EQ(from_zip.cell_count, 2u);
  EXPECT_EQ(from_zip.updates_applied, 3u);
  EXPECT_EQ(from_dir.cell_count, 2u);
  EXPECT_EQ(from_dir.bands, (std::vector<int>{4, 5}));

  std::vector<fv::EncIndexEntry> ia, ib;
  ASSERT_TRUE(fv::ReadEncIndex(a + "/" + fv::kEncIndexFileName, &ia).ok());
  ASSERT_TRUE(fv::ReadEncIndex(b + "/" + fv::kEncIndexFileName, &ib).ok());
  ASSERT_EQ(ia.size(), ib.size());
  for (size_t i = 0; i < ia.size(); ++i) {
    // The folder copy is rooted at ENC_ROOT itself, the zip one above it.
    EXPECT_EQ(ia[i].path, "ENC_ROOT/" + ib[i].path);
    EXPECT_EQ(ia[i].applied_updates, ib[i].applied_updates);
  }
}

TEST_F(EncImportTest, CancelRemovesTheSet) {
  const std::string out = (dir_ / "out").string();
  fv::EncImportResult r;
  const fv::Status s =
      fv::ImportEncSet(MakeZip({"US5CHSDC"}), out, "", &r, [](double f) { return f < 0.5; });
  EXPECT_EQ(s.code, fv::kInterrupted);
  EXPECT_FALSE(fs::exists(out));
}

TEST_F(EncImportTest, RefusesAnExistingDestinationOrItsOwnSource) {
  fv::EncImportResult r;
  const std::string src = MakeFolder({"US4SC1BO"});
  EXPECT_EQ(fv::ImportEncSet(src, src, "", &r).code, fv::kInvalidArg);
  EXPECT_EQ(fv::ImportEncSet(src, src + "/copy", "", &r).code, fv::kInvalidArg);
  EXPECT_FALSE(fs::exists(src + "/copy"));
}

TEST_F(EncImportTest, FolderWithoutCellsIsNotFound) {
  fs::create_directories(dir_ / "empty");
  std::ofstream(dir_ / "empty" / "README.TXT") << "nothing\n";
  fv::EncImportResult r;
  const std::string out = (dir_ / "out").string();
  const fv::Status s = fv::ImportEncSet((dir_ / "empty").string(), out, "", &r);
  EXPECT_EQ(s.code, fv::kNotFound);
  EXPECT_EQ(s.message, "the folder holds no ENC cells");
  EXPECT_FALSE(fs::exists(out));
}

}  // namespace
