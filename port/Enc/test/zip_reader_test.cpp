// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::ZipReader tests against archives written by zip_test_writer.h.

#include "fv_zip_reader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "zip_test_writer.h"

namespace fs = std::filesystem;

namespace {

class ZipReaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = fs::temp_directory_path() /
           ("fv_zip_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
            "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(dir_);
    fs::create_directories(dir_);
  }
  void TearDown() override { fs::remove_all(dir_); }

  std::string Path(const std::string& name) const { return (dir_ / name).string(); }

  static std::string ReadAll(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    std::stringstream ss;
    ss << is.rdbuf();
    return ss.str();
  }

  /// A payload that deflates well but is not trivially uniform.
  static std::string Pattern(size_t n) {
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>((i * 7 + i / 13) & 0xFF);
    return s;
  }

  fs::path dir_;
};

TEST(IsSafeZipPath, RejectsEscapes) {
  EXPECT_TRUE(fv::IsSafeZipPath("ENC_ROOT/US5CHSDC/US5CHSDC.000"));
  EXPECT_TRUE(fv::IsSafeZipPath("README.TXT"));
  EXPECT_FALSE(fv::IsSafeZipPath(""));
  EXPECT_FALSE(fv::IsSafeZipPath("/etc/passwd"));
  EXPECT_FALSE(fv::IsSafeZipPath("../up.txt"));
  EXPECT_FALSE(fv::IsSafeZipPath("a/../../b"));
  EXPECT_FALSE(fv::IsSafeZipPath("a//b"));
  EXPECT_FALSE(fv::IsSafeZipPath("a\\b"));
  EXPECT_FALSE(fv::IsSafeZipPath("C:/x"));
}

TEST_F(ZipReaderTest, ListsAndExtractsStoredAndDeflated) {
  const std::string big = Pattern(700 * 1024);  // spans several inflate chunks
  fv_test::ZipTestWriter w;
  w.AddDirectory("ENC_ROOT");
  w.Add("ENC_ROOT/a.txt", "hello, chart", false);
  w.Add("ENC_ROOT/sub/big.bin", big, true);
  w.Add("empty.txt", "", true);
  ASSERT_TRUE(w.Write(Path("t.zip")));

  fv::ZipReader z;
  ASSERT_TRUE(z.Open(Path("t.zip")).ok());
  ASSERT_EQ(z.entries().size(), 4u);
  EXPECT_TRUE(z.entries()[0].is_directory());
  EXPECT_EQ(z.entries()[1].method, 0);
  EXPECT_EQ(z.entries()[2].method, 8);
  EXPECT_LT(z.entries()[2].compressed_size, big.size());
  EXPECT_EQ(z.total_uncompressed_size(), 12u + big.size());

  const std::string out = Path("out");
  uint64_t last_done = 0, last_total = 0;
  ASSERT_TRUE(z.ExtractAll(out, [&](uint64_t d, uint64_t t) {
                 last_done = d;
                 last_total = t;
                 return true;
               }).ok());
  EXPECT_EQ(last_done, last_total);
  EXPECT_EQ(ReadAll(out + "/ENC_ROOT/a.txt"), "hello, chart");
  EXPECT_EQ(ReadAll(out + "/ENC_ROOT/sub/big.bin"), big);
  EXPECT_TRUE(fs::is_regular_file(out + "/empty.txt"));
  EXPECT_EQ(fs::file_size(out + "/empty.txt"), 0u);
}

TEST_F(ZipReaderTest, CorruptPayloadFailsAndLeavesNoFile) {
  fv_test::ZipTestWriter w;
  w.Add("x.txt", "stored payload with a flipped byte", false);
  std::string bytes = w.Bytes();
  bytes[30 + 5 + 3] ^= 0x20;  // local header (30) + name (5), inside the data
  {
    std::ofstream os(Path("bad.zip"), std::ios::binary);
    os.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  fv::ZipReader z;
  ASSERT_TRUE(z.Open(Path("bad.zip")).ok());
  const fv::Status s = z.Extract(z.entries()[0], Path("x.txt"));
  EXPECT_EQ(s.code, fv::kIoError);
  EXPECT_NE(s.message.find("CRC"), std::string::npos);
  EXPECT_FALSE(fs::exists(Path("x.txt")));
}

TEST_F(ZipReaderTest, UnsafeNameAbortsBeforeWriting) {
  fv_test::ZipTestWriter w;
  w.Add("fine.txt", "ok");
  w.Add("../escape.txt", "no");
  ASSERT_TRUE(w.Write(Path("evil.zip")));
  fv::ZipReader z;
  ASSERT_TRUE(z.Open(Path("evil.zip")).ok());
  EXPECT_EQ(z.ExtractAll(Path("out")).code, fv::kInvalidArg);
  EXPECT_FALSE(fs::exists(Path("out/fine.txt")));
  EXPECT_FALSE(fs::exists(Path("escape.txt")));
}

TEST_F(ZipReaderTest, ProgressCanCancel) {
  fv_test::ZipTestWriter w;
  w.Add("1.txt", "one");
  w.Add("2.txt", "two");
  ASSERT_TRUE(w.Write(Path("c.zip")));
  fv::ZipReader z;
  ASSERT_TRUE(z.Open(Path("c.zip")).ok());
  EXPECT_EQ(z.ExtractAll(Path("out"), [](uint64_t, uint64_t) { return false; }).code,
            fv::kInterrupted);
  EXPECT_FALSE(fs::exists(Path("out/2.txt")));
}

TEST_F(ZipReaderTest, RejectsNonZip) {
  {
    std::ofstream os(Path("not.zip"), std::ios::binary);
    os << std::string(100, 'x');
  }
  fv::ZipReader z;
  EXPECT_EQ(z.Open(Path("not.zip")).code, fv::kIoError);
  EXPECT_EQ(z.Open(Path("missing.zip")).code, fv::kNotFound);
}

}  // namespace
