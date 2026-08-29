// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ISO 8211 reader tests (ENC phase E1) — hermetic.
//
// No test data: every file here is built byte by byte, so these tests pin the
// container rules themselves (leader, per-record entry map, format controls,
// LSB-first binary subfields, repeating fields, S-57's all-bits-set null) and
// fail on a decoder change rather than on a data refresh. The format-control
// strings used below are the real ones from a NOAA cell's DDR.

#include "fv_iso8211.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using fv::iso8211::Field;
using fv::iso8211::ParseFormatControls;
using fv::iso8211::Reader;
using fv::iso8211::Record;
using fv::iso8211::SubfieldSpec;
using fv::iso8211::ValueKind;

// --- a minimal ISO 8211 writer, enough to exercise the reader ---------------

struct FieldDef {
  std::string tag;
  std::string name;
  std::string labels;  // '!'-separated; leading '*' = repeating
  std::string format;  // "(b11,A)"
};

void AppendNum(std::string* s, size_t value, int width) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%0*llu", width, static_cast<unsigned long long>(value));
  *s += buf;
}

// Builds one record (DDR when `is_ddr`) from tag/body pairs. `len_width` and
// `pos_width` go into the leader's entry map, so a test can give each record a
// different one — real cells do, which is exactly the trap this catches.
std::string BuildRecord(bool is_ddr,
                        const std::vector<std::pair<std::string, std::string>>& fields,
                        int len_width = 3, int pos_width = 4) {
  std::string directory, area;
  for (const auto& f : fields) {
    const std::string body = f.second + std::string(1, 0x1e);  // FT
    directory += f.first;
    AppendNum(&directory, body.size(), len_width);
    AppendNum(&directory, area.size(), pos_width);
    area += body;
  }
  directory += std::string(1, 0x1e);  // end of directory

  const size_t base = 24 + directory.size();
  const size_t total = base + area.size();
  std::string leader;
  AppendNum(&leader, total, 5);
  leader += "3";                 // interchange level
  leader += is_ddr ? "L" : "D";  // leader identifier
  leader += "E";                 // inline code extension
  leader += "1";                 // version
  leader += " ";                 // application indicator
  leader += "09";                // field control length
  AppendNum(&leader, base, 5);
  leader += "   ";  // extended character set indicator
  AppendNum(&leader, static_cast<size_t>(len_width), 1);
  AppendNum(&leader, static_cast<size_t>(pos_width), 1);
  leader += "0";  // reserved
  leader += "4";  // size of field tag
  return leader + directory + area;
}

std::string DdrBody(const FieldDef& d) {
  const char ut = 0x1f;
  return "1600;&   " + d.name + ut + d.labels + ut + d.format;
}

// A whole file: DDR built from `defs`, then the data records.
std::vector<uint8_t> BuildFile(const std::vector<FieldDef>& defs,
                               const std::vector<std::string>& records) {
  std::vector<std::pair<std::string, std::string>> ddr_fields;
  std::string tree = "0000;&   ";
  ddr_fields.emplace_back("0000", tree);
  for (const FieldDef& d : defs) ddr_fields.emplace_back(d.tag, DdrBody(d));
  std::string out = BuildRecord(true, ddr_fields);
  for (const std::string& r : records) out += r;
  return std::vector<uint8_t>(out.begin(), out.end());
}

std::string LE(uint64_t v, int width) {
  std::string s;
  for (int i = 0; i < width; ++i) s += static_cast<char>((v >> (8 * i)) & 0xff);
  return s;
}

// --- format controls -------------------------------------------------------

TEST(Iso8211Format, ParsesTheRealS57DsidFormat) {
  // Verbatim from US5CHSDC.000's DDR.
  std::vector<SubfieldSpec> specs;
  ASSERT_TRUE(ParseFormatControls(
                  "(b11,b14,2b11,3A,2A(8),R(4),b11,2A,b11,b12,A)",
                  "RCNM!RCID!EXPP!INTU!DSNM!EDTN!UPDN!UADT!ISDT!STED!PRSP!PSDN!"
                  "PRED!PROF!AGEN!COMT",
                  &specs)
                  .ok());
  ASSERT_EQ(16u, specs.size());
  EXPECT_EQ("RCNM", specs[0].label);
  EXPECT_EQ('b', specs[0].format);
  EXPECT_EQ(1, specs[0].width);
  EXPECT_EQ("RCID", specs[1].label);
  EXPECT_EQ(4, specs[1].width);
  EXPECT_EQ("EXPP", specs[2].label);  // 2b11 expanded
  EXPECT_EQ("INTU", specs[3].label);
  EXPECT_EQ('A', specs[4].format);    // 3A: variable-width text
  EXPECT_EQ(0, specs[4].width);
  EXPECT_EQ("UADT", specs[7].label);  // 2A(8): fixed 8
  EXPECT_EQ(8, specs[7].width);
  EXPECT_EQ("STED", specs[9].label);  // R(4)
  EXPECT_EQ('R', specs[9].format);
  EXPECT_EQ(4, specs[9].width);
  EXPECT_EQ("AGEN", specs[14].label);
  EXPECT_EQ(2, specs[14].width);
  EXPECT_EQ("COMT", specs[15].label);
}

TEST(Iso8211Format, BitStringWidthIsInBits) {
  std::vector<SubfieldSpec> specs;
  ASSERT_TRUE(ParseFormatControls("(B(40),4b11)", "*NAME!ORNT!USAG!TOPI!MASK",
                                  &specs)
                  .ok());
  ASSERT_EQ(5u, specs.size());
  EXPECT_EQ('B', specs[0].format);
  EXPECT_EQ(5, specs[0].width) << "B(40) is 40 bits = 5 bytes";
  EXPECT_EQ("MASK", specs[4].label);
}

TEST(Iso8211Format, UnlabelledFieldsGetPositionalLabels) {
  std::vector<SubfieldSpec> specs;
  ASSERT_TRUE(ParseFormatControls("(b12)", "", &specs).ok());
  ASSERT_EQ(1u, specs.size());
  EXPECT_EQ("1", specs[0].label);
}

TEST(Iso8211Format, LabelCountMustMatchFormatCount) {
  std::vector<SubfieldSpec> specs;
  const fv::Status s = ParseFormatControls("(b11,b14)", "A!B!C", &specs);
  EXPECT_EQ(fv::kIoError, s.code);
  EXPECT_NE(std::string::npos, s.message.find("3 subfield labels"));
}

TEST(Iso8211Format, NestedGroupsAreRefusedNotGuessed) {
  std::vector<SubfieldSpec> specs;
  EXPECT_EQ(fv::kUnsupported, ParseFormatControls("(3(A,I))", "A!B", &specs).code);
}

TEST(Iso8211Format, UnknownFormatLetterIsUnsupported) {
  std::vector<SubfieldSpec> specs;
  EXPECT_EQ(fv::kUnsupported, ParseFormatControls("(X(4))", "A", &specs).code);
}

// --- record decoding -------------------------------------------------------

TEST(Iso8211Reader, DecodesBinarySubfieldsLsbFirst) {
  const std::vector<FieldDef> defs = {
      {"TST1", "test", "U1!U2!U4!S4", "(b11,b12,b14,b24)"}};
  // 0x7f; 0x1234; 0x89abcdef; -799543459 (a Charleston longitude x 1e7)
  const std::string body =
      LE(0x7f, 1) + LE(0x1234, 2) + LE(0x89abcdefu, 4) +
      LE(static_cast<uint32_t>(static_cast<int32_t>(-799543459)), 4);
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"TST1", body}})}),
                           "mem")
                  .ok());
  Record rec;
  ASSERT_TRUE(r.Next(&rec).ok());
  const Field* f = rec.Find("TST1");
  ASSERT_NE(nullptr, f);
  EXPECT_EQ(0x7f, f->Int(0, "U1"));
  EXPECT_EQ(0x1234, f->Int(0, "U2"));
  EXPECT_EQ(0x89abcdef, f->Int(0, "U4")) << "b14 is unsigned";
  EXPECT_EQ(-799543459, f->Int(0, "S4")) << "b24 is signed and sign-extended";
  EXPECT_DOUBLE_EQ(-79.9543459, static_cast<double>(f->Int(0, "S4")) / 1e7);
}

TEST(Iso8211Reader, AllBitsSetIsTheS57NullNotZero) {
  const std::vector<FieldDef> defs = {{"TST1", "test", "A!B", "(b12,b12)"}};
  const std::string body = LE(0xffff, 2) + LE(0, 2);
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"TST1", body}})}),
                           "mem")
                  .ok());
  Record rec;
  ASSERT_TRUE(r.Next(&rec).ok());
  const Field* f = rec.Find("TST1");
  ASSERT_NE(nullptr, f);
  ASSERT_NE(nullptr, f->Get(0, "A"));
  EXPECT_TRUE(f->Get(0, "A")->null);
  EXPECT_EQ(0xffff, f->Get(0, "A")->integer) << "the raw value stays verbatim";
  EXPECT_EQ(-1, f->Int(0, "A", -1)) << "Int() substitutes for a null subfield";
  EXPECT_FALSE(f->Get(0, "B")->null);
  EXPECT_EQ(0, f->Int(0, "B", -1));
}

TEST(Iso8211Reader, RepeatingBinaryFieldYieldsOneRowPerTuple) {
  // SG2D's real shape: an array of (YCOO, XCOO) with no delimiters.
  const std::vector<FieldDef> defs = {{"SG2D", "2-D coordinate field",
                                       "*YCOO!XCOO", "(2b24)"}};
  std::string body;
  for (int i = 0; i < 4; ++i)
    body += LE(static_cast<uint32_t>(327000000 + i), 4) +
            LE(static_cast<uint32_t>(static_cast<int32_t>(-800000000 + i)), 4);
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"SG2D", body}})}),
                           "mem")
                  .ok());
  Record rec;
  ASSERT_TRUE(r.Next(&rec).ok());
  const Field* f = rec.Find("SG2D");
  ASSERT_NE(nullptr, f);
  ASSERT_EQ(4u, f->row_count());
  EXPECT_EQ(327000003, f->Int(3, "YCOO"));
  EXPECT_EQ(-799999997, f->Int(3, "XCOO"));
}

TEST(Iso8211Reader, RepeatingFieldWithVariableTextSplitsOnUnitTerminator) {
  // ATTF's real shape: (attribute code, value) pairs, values UT-delimited.
  const std::vector<FieldDef> defs = {
      {"ATTF", "Feature record attribute field", "*ATTL!ATVL", "(b12,A)"}};
  const char ut = 0x1f;
  const std::string body = LE(87, 2) + "-2" + ut + LE(88, 2) + "0" + ut +
                           LE(148, 2) + "US,US,graph,Chart 11518";
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"ATTF", body}})}),
                           "mem")
                  .ok());
  Record rec;
  ASSERT_TRUE(r.Next(&rec).ok());
  const Field* f = rec.Find("ATTF");
  ASSERT_NE(nullptr, f);
  ASSERT_EQ(3u, f->row_count()) << "the last value has no UT, only the FT";
  EXPECT_EQ(87, f->Int(0, "ATTL"));
  EXPECT_EQ("-2", f->Text(0, "ATVL"));
  EXPECT_EQ("0", f->Text(1, "ATVL"));
  EXPECT_EQ(148, f->Int(2, "ATTL"));
  EXPECT_EQ("US,US,graph,Chart 11518", f->Text(2, "ATVL"));
}

TEST(Iso8211Reader, BitStringSubfieldsAreKeptVerbatim) {
  const std::vector<FieldDef> defs = {
      {"FSPT", "spatial pointer", "*NAME!ORNT!USAG!MASK", "(B(40),3b11)"}};
  const std::string body =
      std::string("\x82\xfd\x01\x00\x00", 5) + LE(1, 1) + LE(255, 1) + LE(2, 1);
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"FSPT", body}})}),
                           "mem")
                  .ok());
  Record rec;
  ASSERT_TRUE(r.Next(&rec).ok());
  const Field* f = rec.Find("FSPT");
  ASSERT_NE(nullptr, f);
  const fv::iso8211::Value* name = f->Get(0, "NAME");
  ASSERT_NE(nullptr, name);
  EXPECT_EQ(ValueKind::kBytes, name->kind);
  ASSERT_EQ(5u, name->bytes.size());
  EXPECT_EQ(0x82, name->bytes[0]) << "RCNM 130 = edge";
  EXPECT_EQ("82fd010000", name->AsText());
  // A 1-byte binary subfield holding 255 IS the S-57 null — ORNT/USAG/MASK use
  // that deliberately, so there is no ambiguity to resolve, but a caller asking
  // for such a subfield must supply the missing value it wants back.
  ASSERT_NE(nullptr, f->Get(0, "USAG"));
  EXPECT_TRUE(f->Get(0, "USAG")->null);
  EXPECT_EQ(255, f->Get(0, "USAG")->integer) << "the raw value stays verbatim";
  EXPECT_EQ(255, f->Int(0, "USAG", 255));
  EXPECT_EQ(1, f->Int(0, "ORNT")) << "a non-null neighbour is unaffected";
}

TEST(Iso8211Reader, FixedWidthTextIsBlankTrimmedAndAsciiNumbersParse) {
  // CATALOG.031's shape: ASCII I and R subfields, not binary.
  const std::vector<FieldDef> defs = {
      {"CATD", "Catalogue Directory Field", "RCNM!RCID!SLAT", "(A(2),I(10),R)"}};
  const std::string body = "CD" + std::string("0000000004") + "32.775000";
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"CATD", body}})}),
                           "mem")
                  .ok());
  Record rec;
  ASSERT_TRUE(r.Next(&rec).ok());
  const Field* f = rec.Find("CATD");
  ASSERT_NE(nullptr, f);
  EXPECT_EQ("CD", f->Text(0, "RCNM"));
  EXPECT_EQ(4, f->Int(0, "RCID"));
  EXPECT_DOUBLE_EQ(32.775, f->Real(0, "SLAT"));
}

TEST(Iso8211Reader, EntryMapWidthsComeFromEachRecordsOwnLeader) {
  // Real cells vary this per record (the DDR uses 3/4 while a short data record
  // uses 3/2); reading the DDR's widths for every record shifts every field.
  const std::vector<FieldDef> defs = {{"TST1", "test", "A!B", "(b12,b12)"}};
  const std::string body = LE(0x1111, 2) + LE(0x2222, 2);
  std::vector<uint8_t> file = BuildFile(defs, {});
  const std::string wide = BuildRecord(false, {{"TST1", body}}, 5, 5);
  const std::string narrow = BuildRecord(false, {{"TST1", body}}, 2, 1);
  file.insert(file.end(), wide.begin(), wide.end());
  file.insert(file.end(), narrow.begin(), narrow.end());

  Reader r;
  ASSERT_TRUE(r.OpenMemory(file, "mem").ok());
  for (int i = 0; i < 2; ++i) {
    Record rec;
    ASSERT_TRUE(r.Next(&rec).ok()) << "record " << i;
    const Field* f = rec.Find("TST1");
    ASSERT_NE(nullptr, f) << "record " << i;
    EXPECT_EQ(0x1111, f->Int(0, "A")) << "record " << i;
    EXPECT_EQ(0x2222, f->Int(0, "B")) << "record " << i;
  }
  EXPECT_TRUE(r.eof());
  Record rec;
  EXPECT_EQ(fv::kNotFound, r.Next(&rec).code);
}

TEST(Iso8211Reader, DdrDefinitionsAreExposed) {
  const std::vector<FieldDef> defs = {
      {"SG2D", "2-D coordinate field", "*YCOO!XCOO", "(2b24)"},
      {"VRID", "Vector record identifier field", "RCNM!RCID", "(b11,b14)"}};
  Reader r;
  ASSERT_TRUE(r.OpenMemory(BuildFile(defs, {}), "mem").ok());
  ASSERT_EQ(2u, r.definitions().size()) << "the 0000 field-pair tree is skipped";
  const fv::iso8211::FieldDefinition* sg2d = r.Definition("SG2D");
  ASSERT_NE(nullptr, sg2d);
  EXPECT_TRUE(sg2d->repeating) << "the '*' prefix marks an array field";
  EXPECT_EQ("2-D coordinate field", sg2d->name);
  const fv::iso8211::FieldDefinition* vrid = r.Definition("VRID");
  ASSERT_NE(nullptr, vrid);
  EXPECT_FALSE(vrid->repeating);
  EXPECT_EQ(3, r.interchange_level());
}

// --- malformed input ------------------------------------------------------

TEST(Iso8211Reader, TruncatedFieldIsAnErrorNotAnOverread) {
  const std::vector<FieldDef> defs = {{"TST1", "test", "A!B", "(b14,b14)"}};
  // Only 4 of the 8 bytes the format demands.
  Reader r;
  ASSERT_TRUE(
      r.OpenMemory(BuildFile(defs, {BuildRecord(false, {{"TST1", LE(1, 4)}})}),
                   "mem")
          .ok());
  Record rec;
  const fv::Status s = r.Next(&rec);
  EXPECT_EQ(fv::kIoError, s.code);
  EXPECT_NE(std::string::npos, s.message.find("truncated"));
}

TEST(Iso8211Reader, FieldWithNoDdrDefinitionIsAnError) {
  const std::vector<FieldDef> defs = {{"TST1", "test", "A", "(b11)"}};
  std::vector<uint8_t> file = BuildFile(defs, {});
  const std::string rec = BuildRecord(false, {{"NOPE", LE(1, 1)}});
  file.insert(file.end(), rec.begin(), rec.end());
  Reader r;
  ASSERT_TRUE(r.OpenMemory(file, "mem").ok());
  Record out;
  EXPECT_EQ(fv::kIoError, r.Next(&out).code);
}

TEST(Iso8211Reader, RecordLengthPastEndOfFileIsRejected) {
  const std::vector<FieldDef> defs = {{"TST1", "test", "A", "(b11)"}};
  std::vector<uint8_t> file = BuildFile(defs, {});
  std::string rec = BuildRecord(false, {{"TST1", LE(1, 1)}});
  rec.resize(rec.size() - 3);  // clip the tail off the last record
  file.insert(file.end(), rec.begin(), rec.end());
  Reader r;
  ASSERT_TRUE(r.OpenMemory(file, "mem").ok());
  Record out;
  const fv::Status s = r.Next(&out);
  EXPECT_EQ(fv::kIoError, s.code);
  EXPECT_NE(std::string::npos, s.message.find("overruns"));
}

TEST(Iso8211Reader, FirstRecordMustBeADdr) {
  const std::vector<FieldDef> defs = {{"TST1", "test", "A", "(b11)"}};
  std::vector<uint8_t> file = BuildFile(defs, {});
  file[6] = 'D';  // claim the DDR is a data record
  Reader r;
  EXPECT_EQ(fv::kIoError, r.OpenMemory(file, "mem").code);
}

TEST(Iso8211Reader, MissingFileIsAnIoError) {
  Reader r;
  EXPECT_EQ(fv::kIoError, r.Open("/nonexistent/enc/nope.000").code);
  EXPECT_FALSE(r.is_open());
}

}  // namespace
