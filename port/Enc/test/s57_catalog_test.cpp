// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv::S57ObjectCatalog tests (ENC phase E2) — the S-57 Appendix A catalogue
// E1 refused to write from memory.
//
// Row counts are DERIVED from the files rather than pinned as literals (the
// 2026-07-23 TestData lesson): the test counts the data lines itself and
// requires the loader to have kept them all, which catches a dropped row
// without breaking when the tables are updated. What IS pinned is semantics —
// the exact codes the ledger cited as unresolvable in E1.

#include "fv_s57_catalog.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string EncRoot() {
  const char* d = getenv("FVW_TESTDATA_DIR");
  if (d == nullptr) return {};
  const std::string p = std::string(d) + "/enc";
  return fs::is_directory(p) ? p : std::string();
}

// Data lines (non-blank, excluding the header) of a CSV.
size_t DataLines(const std::string& path) {
  std::ifstream in(path);
  if (!in) return 0;
  std::string line;
  size_t n = 0;
  bool first = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (first) {
      first = false;
      continue;
    }
    if (line.find_first_not_of(" \t") != std::string::npos) ++n;
  }
  return n;
}

#define LOAD_CATALOG(var)                                     \
  const std::string root = EncRoot();                         \
  if (root.empty()) GTEST_SKIP() << "no ENC test data";       \
  fv::S57ObjectCatalog var;                                   \
  ASSERT_TRUE(var.Load(root).ok()) << var.Load(root).message

// --- the CSV reader, hermetic -----------------------------------------------

TEST(CsvLine, SplitsPlainFields) {
  const auto f = fv::ParseCsvLine("1,Agency responsible for production,AGENCY,A,F");
  ASSERT_EQ(f.size(), 5u);
  EXPECT_EQ(f[0], "1");
  EXPECT_EQ(f[1], "Agency responsible for production");
  EXPECT_EQ(f[4], "F");
}

TEST(CsvLine, QuotedFieldKeepsItsCommas) {
  const auto f = fv::ParseCsvLine("2,1,\"stake, pole, perch, post\"");
  ASSERT_EQ(f.size(), 3u);
  EXPECT_EQ(f[2], "stake, pole, perch, post");
}

TEST(CsvLine, DoubledQuoteIsALiteralQuote) {
  const auto f = fv::ParseCsvLine("1,\"a \"\"quoted\"\" word\",X");
  ASSERT_EQ(f.size(), 3u);
  EXPECT_EQ(f[1], "a \"quoted\" word");
}

TEST(CsvLine, TrailingEmptyFieldSurvives) {
  const auto f = fv::ParseCsvLine("7,\"Name\",ACRO,,");
  ASSERT_EQ(f.size(), 5u);
  EXPECT_EQ(f[3], "");
  EXPECT_EQ(f[4], "");
}

// --- the real catalogue -----------------------------------------------------

TEST(S57Catalog, KeepsEveryRowInTheFiles) {
  LOAD_CATALOG(cat);
  // One row short by design: OpenCPN's pseudo-class `_texto` reuses code 135,
  // which S-57 gives to TESARE, and the real class keeps the code.
  EXPECT_EQ(cat.object_class_count(),
            DataLines(root + "/s57objectclasses.csv") - 1);
  const fv::S57ObjectClass* c135 = cat.ObjectClass(135);
  ASSERT_NE(c135, nullptr);
  EXPECT_EQ(c135->acronym, "TESARE");
  EXPECT_EQ(cat.attribute_count(), DataLines(root + "/s57attributes.csv"));
  EXPECT_EQ(cat.enumerated_value_count(),
            DataLines(root + "/s57expectedinput.csv"));
}

// The two codes the E1 decision named as unresolvable without the catalogue.
TEST(S57Catalog, ResolvesTheCodesE1CouldNot) {
  LOAD_CATALOG(cat);
  const fv::S57ObjectClass* depare = cat.ObjectClass(42);
  ASSERT_NE(depare, nullptr);
  EXPECT_EQ(depare->acronym, "DEPARE");
  EXPECT_EQ(depare->name, "Depth area");
  EXPECT_EQ(depare->record_class, fv::S57RecordClass::kGeo);

  const fv::S57AttributeDef* drval1 = cat.Attribute(87);
  ASSERT_NE(drval1, nullptr);
  EXPECT_EQ(drval1->acronym, "DRVAL1");
  EXPECT_EQ(drval1->name, "Depth range value 1");
  EXPECT_EQ(drval1->type, fv::S57AttributeType::kFloat);
}

TEST(S57Catalog, AcronymLookupIsTheInverse) {
  LOAD_CATALOG(cat);
  const fv::S57ObjectClass* c = cat.ObjectClassByAcronym("BOYLAT");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(cat.ObjectClass(c->code), c);
  const fv::S57AttributeDef* a = cat.AttributeByAcronym("OBJNAM");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(cat.Attribute(a->code), a);
}

// The `Class` column is the DSSI meta/cartographic/geo/collection split E1 had
// to leave open — that is the whole reason it is carried here.
TEST(S57Catalog, ClassColumnGivesTheDssiRecordSplit) {
  LOAD_CATALOG(cat);
  const fv::S57ObjectClass* meta = cat.ObjectClassByAcronym("M_COVR");
  ASSERT_NE(meta, nullptr);
  EXPECT_EQ(meta->record_class, fv::S57RecordClass::kMeta);

  // 'C' is the COLLECTION class and '$' the CARTOGRAPHIC one — the opposite of
  // what the letters suggest, and worth pinning for exactly that reason.
  const fv::S57ObjectClass* coll = cat.ObjectClassByAcronym("C_AGGR");
  ASSERT_NE(coll, nullptr);
  EXPECT_EQ(coll->record_class, fv::S57RecordClass::kCollection);

  const fv::S57ObjectClass* carto = cat.ObjectClassByAcronym("$AREAS");
  ASSERT_NE(carto, nullptr);
  EXPECT_EQ(carto->record_class, fv::S57RecordClass::kCartographic);

  size_t geo = 0, other = 0;
  for (int code = 1; code < 1000; ++code) {
    const fv::S57ObjectClass* c = cat.ObjectClass(code);
    if (c == nullptr) continue;
    (c->record_class == fv::S57RecordClass::kGeo ? geo : other)++;
  }
  EXPECT_GT(geo, 150u);    // the catalogue is mostly charted objects
  EXPECT_GT(other, 10u);   // ... but the M_*/C_* rows are there
}

TEST(S57Catalog, AttributeSetsAreSplitOnSemicolons) {
  LOAD_CATALOG(cat);
  const fv::S57ObjectClass* c = cat.ObjectClassByAcronym("DEPARE");
  ASSERT_NE(c, nullptr);
  EXPECT_NE(std::find(c->attributes_a.begin(), c->attributes_a.end(), "DRVAL1"),
            c->attributes_a.end());
  EXPECT_NE(std::find(c->primitives.begin(), c->primitives.end(), "Area"),
            c->primitives.end());
  // No empty fragments from the tables' trailing separators.
  for (const std::string& a : c->attributes_a) EXPECT_FALSE(a.empty());
}

// The expected-input table is what gives ENC a real Describe() (R1 sense):
// enumerated attribute values decode to text, exactly as VPF's INT.VDT does.
TEST(S57Catalog, DecodesEnumeratedValues) {
  LOAD_CATALOG(cat);
  const fv::S57AttributeDef* catach = cat.AttributeByAcronym("CATACH");
  ASSERT_NE(catach, nullptr);
  EXPECT_EQ(catach->type, fv::S57AttributeType::kList);
  const std::string* v = cat.EnumeratedValue(catach->code, 1);
  ASSERT_NE(v, nullptr);
  EXPECT_FALSE(v->empty());
  EXPECT_EQ(cat.DescribeValue(catach->code, "1"), *v);
}

TEST(S57Catalog, DescribesAListValueMemberByMember) {
  LOAD_CATALOG(cat);
  const fv::S57AttributeDef* colour = cat.AttributeByAcronym("COLOUR");
  ASSERT_NE(colour, nullptr);
  ASSERT_EQ(colour->type, fv::S57AttributeType::kList);
  const std::string* c1 = cat.EnumeratedValue(colour->code, 1);
  const std::string* c3 = cat.EnumeratedValue(colour->code, 3);
  ASSERT_NE(c1, nullptr);
  ASSERT_NE(c3, nullptr);
  EXPECT_EQ(cat.DescribeValue(colour->code, "1,3"), *c1 + ", " + *c3);
}

// An unexpected value is information, not noise: it is labelled, never dropped.
TEST(S57Catalog, UnknownEnumeratedValueIsLabelledNotDropped) {
  LOAD_CATALOG(cat);
  const fv::S57AttributeDef* colour = cat.AttributeByAcronym("COLOUR");
  ASSERT_NE(colour, nullptr);
  EXPECT_EQ(cat.DescribeValue(colour->code, "999"), "999 (undefined)");
}

// Free text and numbers pass through untouched — decoding them would be a lie.
TEST(S57Catalog, NonEnumeratedValuesPassThrough) {
  LOAD_CATALOG(cat);
  const fv::S57AttributeDef* objnam = cat.AttributeByAcronym("OBJNAM");
  ASSERT_NE(objnam, nullptr);
  EXPECT_EQ(cat.DescribeValue(objnam->code, "Ashley River"), "Ashley River");
  const fv::S57AttributeDef* drval1 = cat.AttributeByAcronym("DRVAL1");
  ASSERT_NE(drval1, nullptr);
  EXPECT_EQ(cat.DescribeValue(drval1->code, "3.4"), "3.4");
}

TEST(S57Catalog, MissingDirectoryIsAnErrorNotAnEmptyCatalogue) {
  fv::S57ObjectCatalog cat;
  const fv::Status st = cat.Load("/nonexistent/enc/catalogue");
  EXPECT_FALSE(st.ok());
  EXPECT_FALSE(cat.is_loaded());
  EXPECT_EQ(cat.ObjectClass(42), nullptr);
}

}  // namespace
