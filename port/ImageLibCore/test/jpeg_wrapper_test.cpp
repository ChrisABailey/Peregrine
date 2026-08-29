// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for CJpeg (ImageLib/jpeg/jpeg.cpp) — FalconView's JPEG reader class.
// Real-data tests decode TIROS "*.WLD" files from TestData/tiros3 (JPEGs
// with a FalconView JFIF comment; per Chris they're just renamed geojpegs).

#include <gtest/gtest.h>

#include <cstdlib>
#include <dirent.h>
#include <string>
#include <vector>

#include "fv_compat.h"
#include "fv_cstring.h"
#include "jpeg.h"

namespace {

// C++14 (jpeg.h needs std::auto_ptr), so a POSIX walk instead of
// std::filesystem
std::string FindWldUnder(const std::string& dir) {
  DIR* dp = opendir(dir.c_str());
  if (!dp) return {};
  std::string found;
  while (dirent* e = readdir(dp)) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    std::string full = dir + "/" + name;
    if (e->d_type == DT_DIR) {
      found = FindWldUnder(full);
    } else if (name.size() > 4 &&
               name.compare(name.size() - 4, 4, ".WLD") == 0) {
      found = full;  // uppercase .WLD = the JPEGs
    }
    if (!found.empty()) break;
  }
  closedir(dp);
  return found;
}

std::string FirstTirosJpeg() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (!d) return {};
  return FindWldUnder(std::string(d) + "/tiros3");
}

TEST(CJpegTiros, LoadReportsDimensions) {
  std::string path = FirstTirosJpeg();
  if (path.empty()) GTEST_SKIP() << "no TIROS samples";

  CJpeg j;
  int w = 0, h = 0;
  CString err;
  ASSERT_EQ(0, j.load(path.c_str(), w, h, err)) << (LPCSTR)err;
  EXPECT_GT(w, 100);
  EXPECT_GT(h, 100);
}

TEST(CJpegTiros, DecodesToPlausibleRgb) {
  std::string path = FirstTirosJpeg();
  if (path.empty()) GTEST_SKIP() << "no TIROS samples";

  CJpeg j;
  int w = 0, h = 0;
  CString err;
  ASSERT_EQ(0, j.load(path.c_str(), w, h, err));

  int tw = w < 256 ? w : 256, th = h < 256 ? h : 256;
  std::vector<unsigned char> r(tw * th), g(tw * th), b(tw * th);
  ASSERT_EQ(0, j.get_jpeg_image(0, 0, tw, th, r.data(), g.data(), b.data(),
                                err))
      << (LPCSTR)err;

  long long sum = 0;
  int distinct[256] = {0};
  for (int i = 0; i < tw * th; ++i) {
    sum += r[i] + g[i] + b[i];
    distinct[g[i]] = 1;
  }
  double mean = (double)sum / (3.0 * tw * th);
  EXPECT_GT(mean, 2.0);
  EXPECT_LT(mean, 253.0);
  int nd = 0;
  for (int v : distinct) nd += v;
  EXPECT_GT(nd, 4) << "too few distinct green values - decode suspect";
}

TEST(CJpegTiros, MissingFileFails) {
  CJpeg j;
  int w = 0, h = 0;
  CString err;
  EXPECT_NE(0, j.load("/nonexistent/x.WLD", w, h, err));
  EXPECT_FALSE(err.IsEmpty());
}

// fv_cstring behaviors CJpeg depends on
TEST(FvCString, FormatRightLeftAndVarargsLayout) {
  CString s;
  s.Format("file %s line %d", "abc.jpg", 42);
  EXPECT_STREQ((LPCSTR)s, "file abc.jpg line 42");

  CString t("HelloWorld");
  EXPECT_STREQ((LPCSTR)t.Right(5), "World");
  EXPECT_STREQ((LPCSTR)t.Left(5), "Hello");
  EXPECT_EQ(t.Find('W'), 5);

  // the (LPCSTR) cast idiom used at varargs call sites
  CString u;
  u.Format("[%s]", (LPCSTR)t);
  EXPECT_STREQ((LPCSTR)u, "[HelloWorld]");
}

}  // namespace
