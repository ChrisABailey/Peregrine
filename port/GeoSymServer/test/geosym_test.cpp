// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// GeoSym rule-table layer tests (vpf-geosym plan phase V3).
//
// Covers the headless, non-GDI foundation of GeoSymServer:
//   * CDelimitedParser  — '|'-delimited table tokenizer (CRLF tables)
//   * CSymColors        — COLOR.TXT -> COLORREF table + brightness adjuster
//   * CAttributeExpressions — ATTEXP.TXT rule engine + evaluation
//
// Real-data tests read the GeoSym assets under TestData/GeoSymbol/SymAssign
// (git-ignored). DataDir = TestData; the reader composes
// <DataDir>/GeoSymbol/SymAssign/<table>. Path case/separators are resolved at
// the file-open boundary (CStdioFile -> FvResolveWin32Path), so the Windows
// backslash + mixed case in the ledger works unmodified here.

#include "stdafx.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "DelimitedParser.h"
#include "SymColors.h"
#include "AttributeExpressions.h"

#include <gtest/gtest.h>

namespace {

std::string TestDataDir() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  return d ? std::string(d) : std::string();
}

// Backslash locator on purpose (as FalconView composes it) — resolved
// case-insensitively at CStdioFile::Open. SymAssign holds the rule tables.
std::string SymAssignDir() { return TestDataDir() + "\\GeoSymbol\\SymAssign"; }

bool HaveSymAssets() {
  if (TestDataDir().empty()) return false;
  struct stat sb;
  const std::string dir = TestDataDir() + "/GeoSymbol/SymAssign";
  return ::stat(dir.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

// ---------------------------------------------------------------------------
// CDelimitedParser: tokenize a synthetic pipe-delimited, CRLF-terminated table
// (the exact shape of the GeoSym tables). Exercises ReadLine's CRLF->LF
// translation and the empty-field handling of ParseString.
// ---------------------------------------------------------------------------
TEST(DelimitedParser, PipeTokensAndEmptyFields) {
  const char* path = "geosym_delim_tmp.txt";
  // CRLF line endings; a deliberately empty middle field (||).
  FILE* fp = fopen(path, "wb");
  ASSERT_NE(fp, nullptr);
  fputs("42|hello||-7\r\n", fp);
  fputs("0|zero|world|3\r\n", fp);
  fclose(fp);

  CDelimitedParser parser;  // default delimiter '|'
  ASSERT_TRUE(parser.Open(path));

  ASSERT_TRUE(parser.ReadLine());
  long n = -999;
  ASSERT_TRUE(parser.ParseLong(n));
  EXPECT_EQ(n, 42);
  LPCTSTR s = nullptr;
  ASSERT_TRUE(parser.ParseString(s));
  EXPECT_STREQ(s, "hello");
  ASSERT_TRUE(parser.ParseString(s));  // the empty field between ||
  EXPECT_STREQ(s, "");
  ASSERT_TRUE(parser.ParseLong(n));
  EXPECT_EQ(n, -7);  // last field: CRLF was translated so no trailing \r

  ASSERT_TRUE(parser.ReadLine());
  ASSERT_TRUE(parser.ParseLong(n));
  EXPECT_EQ(n, 0);

  parser.Close();
  ::remove(path);
}

// ---------------------------------------------------------------------------
// CSymColors: pin known entries of the real COLOR.TXT.
// index 0 white, 1 black, 2 yellow, 8 ADINF(204,182,65).
// ---------------------------------------------------------------------------
TEST(SymColors, PinnedColorTable) {
  if (!HaveSymAssets()) GTEST_SKIP() << "GeoSym assets not present";
  const std::string file = SymAssignDir() + "\\COLOR.TXT";

  CSymColors colors;
  ASSERT_TRUE(colors.Open(file.c_str()));

  COLORREF white = colors.GetColor(0);
  EXPECT_EQ(GetRValue(white), 255);
  EXPECT_EQ(GetGValue(white), 255);
  EXPECT_EQ(GetBValue(white), 255);

  COLORREF black = colors.GetColor(1);
  EXPECT_EQ(GetRValue(black), 0);
  EXPECT_EQ(GetGValue(black), 0);
  EXPECT_EQ(GetBValue(black), 0);

  COLORREF yellow = colors.GetColor(2);
  EXPECT_EQ(GetRValue(yellow), 255);
  EXPECT_EQ(GetGValue(yellow), 255);
  EXPECT_EQ(GetBValue(yellow), 0);

  COLORREF adinf = colors.GetColor(8);
  EXPECT_EQ(GetRValue(adinf), 204);
  EXPECT_EQ(GetGValue(adinf), 182);
  EXPECT_EQ(GetBValue(adinf), 65);

  // Out-of-range index returns black per the reader.
  COLORREF oor = colors.GetColor(100000);
  EXPECT_EQ(oor, RGB(0, 0, 0));
}

// CSymColorAdjuster: no adjustment is identity; a positive brightness lightens
// (each channel moves toward 255). Pin the exact table math for brightness=+50.
TEST(SymColors, BrightnessAdjuster) {
  CSymColorAdjuster none;  // brightness 0, contrast 0
  const COLORREF c = RGB(10, 20, 30);
  EXPECT_EQ(none.Adjust(c), c);

  CSymColorAdjuster bright(50 /*brightness*/, 0 /*contrast*/);
  // table[i] = i + (255 - i) * (50/100) for brightness >= 0.
  //   R=10 -> 10 + 122 = 132 ; G=20 -> 20 + 117 = 137 ; B=30 -> 30 + 112 = 142
  COLORREF a = bright.Adjust(c);
  EXPECT_EQ(GetRValue(a), 132);
  EXPECT_EQ(GetGValue(a), 137);
  EXPECT_EQ(GetBValue(a), 142);
}

// ---------------------------------------------------------------------------
// CAttributeExpressions: load the real ATTEXP.TXT rule table and evaluate.
// Row shape: condIndex | seq | attribute | operation | value | connector
//   op 1 = EQUAL, 2 = NOT_EQUAL, 6 = GE.
// Known rows:
//   cond 2 : exs != 6      cond 3 : exs == 6      cond 4 : nam != "UNK"
// GetValue(NULL format handler) does a substring search for "<attr>=" in the
// attribute string (the "old way" branch), with ',' between attributes.
// ---------------------------------------------------------------------------
TEST(AttributeExpressions, PinnedRuleEvaluation) {
  if (!HaveSymAssets()) GTEST_SKIP() << "GeoSym assets not present";
  const std::string file = SymAssignDir() + "\\ATTEXP.TXT";

  CAttributeExpressions attExp;
  ASSERT_TRUE(attExp.Open(file.c_str()));

  CECDISValues values;  // default mariner knobs

  // exs=5  -> cond 2 (exs != 6) true ; cond 3 (exs == 6) false
  {
    const char* attrs = "exs=5,nam=FOO";
    CAEAttributeList list(values, attrs, nullptr, ',');
    EXPECT_TRUE(attExp.Evaluate(2, list));
    EXPECT_FALSE(attExp.Evaluate(3, list));
  }

  // exs=6  -> cond 2 (exs != 6) false ; cond 3 (exs == 6) true
  {
    const char* attrs = "exs=6,nam=FOO";
    CAEAttributeList list(values, attrs, nullptr, ',');
    EXPECT_FALSE(attExp.Evaluate(2, list));
    EXPECT_TRUE(attExp.Evaluate(3, list));
  }

  // nam="UNK" comparison (string, op NOT_EQUAL).
  {
    const char* known = "exs=5,nam=UNK";
    CAEAttributeList list(values, known, nullptr, ',');
    EXPECT_FALSE(attExp.Evaluate(4, list));  // nam != "UNK" -> false when UNK
  }
  {
    const char* other = "exs=5,nam=LAKE";
    CAEAttributeList list(values, other, nullptr, ',');
    EXPECT_TRUE(attExp.Evaluate(4, list));  // nam != "UNK" -> true when LAKE
  }

  // A negative index and an out-of-range index both "pass" (documented in
  // CAttributeExpressions::Evaluate: no rule means no restriction).
  {
    const char* attrs = "exs=5";
    CAEAttributeList list(values, attrs, nullptr, ',');
    EXPECT_TRUE(attExp.Evaluate(-1, list));
    EXPECT_TRUE(attExp.Evaluate(999999, list));
  }
}

// CECDISValues supplies the special ISDM/IDSM/SSDC/MSDC/MSSC pseudo-attributes
// that GetValue falls back to when a name is not in the feature's attributes.
TEST(AttributeExpressions, EcdisPseudoAttributes) {
  CECDISValues values;  // defaults: ISDM=1, IDSM=0, SSDC=10, MSDC=30, MSSC=2
  double v = -1.0;
  EXPECT_TRUE(values.GetValueForString("isdm", v));
  EXPECT_DOUBLE_EQ(v, 1.0);
  EXPECT_TRUE(values.GetValueForString("ssdc", v));
  EXPECT_DOUBLE_EQ(v, 10.0);
  EXPECT_FALSE(values.GetValueForString("bogus", v));
}

}  // namespace
